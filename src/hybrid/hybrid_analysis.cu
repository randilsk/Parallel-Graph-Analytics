#include <stdio.h>
#include <stdlib.h>
#include <chrono>
#include <omp.h>
#include <cuda_runtime.h>
#include <device_launch_parameters.h>

// -------------------------------------------------------------------------
// CUDA Error Checking Utility Macro
// -------------------------------------------------------------------------
#define cudaCheckError(ans) { gpuAssert((ans), __FILE__, __LINE__); }
inline void gpuAssert(cudaError_t code, const char *file, int line, bool abort=true) {
    if (code != cudaSuccess) {
        fprintf(stderr, "CUDA Error: %s at %s:%d\n", cudaGetErrorString(code), file, line);
        if (abort) exit(code);
    }
}

// -------------------------------------------------------------------------
// Utility: High-precision timer using C++ std::chrono
// -------------------------------------------------------------------------
double get_time() {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration<double>(duration).count();
}

// =========================================================================
// SERIAL BASELINE IMPLEMENTATIONS (For Accuracy Validation)
// =========================================================================

// Serial BFS
int* run_serial_bfs(int source, int num_nodes, int *row_ptr, int *col_ind, 
                    double &cpu_time, long long &cpu_edges, int &cpu_nodes_reached) {
    int *level = (int *)malloc(num_nodes * sizeof(int));
    int *parent = (int *)malloc(num_nodes * sizeof(int));
    for (int i = 0; i < num_nodes; i++) {
        level[i] = -1;
        parent[i] = -1;
    }

    int *queue = (int *)malloc(num_nodes * sizeof(int));
    int front = 0, rear = 0;

    level[source] = 0;
    parent[source] = source;
    queue[rear++] = source;

    cpu_edges = 0;

    double start_time = get_time();
    while (front < rear) {
        int current_node = queue[front++];
        int start_edge = row_ptr[current_node];
        int end_edge = row_ptr[current_node + 1];

        for (int i = start_edge; i < end_edge; i++) {
            int neighbor = col_ind[i];
            cpu_edges++;

            if (level[neighbor] == -1) {
                level[neighbor] = level[current_node] + 1;
                parent[neighbor] = current_node;
                queue[rear++] = neighbor;
            }
        }
    }
    double end_time = get_time();
    cpu_time = end_time - start_time;
    cpu_nodes_reached = rear;

    free(parent);
    free(queue);
    return level;
}

// Serial CC
int run_serial_cc(int num_nodes, int *row_ptr, int *col_ind, double &cpu_time) {
    int *components = (int *)malloc(num_nodes * sizeof(int));
    for (int i = 0; i < num_nodes; i++) {
        components[i] = -1;
    }

    int *queue = (int *)malloc(num_nodes * sizeof(int));
    int num_components = 0;

    double start_time = get_time();
    for (int i = 0; i < num_nodes; i++) {
        if (components[i] == -1) {
            int current_component = num_components++;
            int front = 0, rear = 0;

            components[i] = current_component;
            queue[rear++] = i;

            while (front < rear) {
                int curr = queue[front++];
                int start_edge = row_ptr[curr];
                int end_edge = row_ptr[curr + 1];

                for (int e = start_edge; e < end_edge; e++) {
                    int neighbor = col_ind[e];
                    if (components[neighbor] == -1) {
                        components[neighbor] = current_component;
                        queue[rear++] = neighbor;
                    }
                }
            }
        }
    }
    double end_time = get_time();
    cpu_time = end_time - start_time;

    free(components);
    free(queue);
    return num_components;
}

// =========================================================================
// CUDA GPU KERNELS
// =========================================================================

__global__ void hybrid_bfs_init_kernel(int *level, int *parent, int num_nodes) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < num_nodes) {
        level[idx] = -1;
        parent[idx] = -1;
    }
}

__global__ void hybrid_bfs_gpu_kernel(
    const int *current_frontier, int frontier_size,
    const int *row_ptr, const int *col_ind,
    int *level, int *parent,
    int *next_frontier, int *next_frontier_size,
    int curr_level, unsigned long long *edges_examined) 
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= frontier_size) return;

    int current_node = current_frontier[idx];
    int start_edge = row_ptr[current_node];
    int end_edge = row_ptr[current_node + 1];

    int local_edges = end_edge - start_edge;
    if (local_edges > 0) {
        atomicAdd(edges_examined, (unsigned long long)local_edges);
    }

    for (int i = start_edge; i < end_edge; i++) {
        int neighbor = col_ind[i];

        int old_level = atomicCAS(&level[neighbor], -1, curr_level + 1);
        if (old_level == -1) {
            parent[neighbor] = current_node;
            int insert_pos = atomicAdd(next_frontier_size, 1);
            next_frontier[insert_pos] = neighbor;
        }
    }
}

__global__ void hybrid_cc_hook_kernel(const int *row_ptr, const int *col_ind, int *parent, int num_nodes, int *changed) {
    int u = blockIdx.x * blockDim.x + threadIdx.x;
    if (u >= num_nodes) return;

    // Direct parent lookups (path compression happens on host and final flatten)
    int root_u = parent[u];
    while (parent[root_u] != root_u) {
        root_u = parent[root_u];
    }
    
    int start_edge = row_ptr[u];
    int end_edge = row_ptr[u + 1];

    for (int i = start_edge; i < end_edge; i++) {
        int v = col_ind[i];
        int root_v = parent[v];
        while (parent[root_v] != root_v) {
            root_v = parent[root_v];
        }

        if (root_u != root_v) {
            int small = min(root_u, root_v);
            int large = max(root_u, root_v);
            int old = atomicMin(&parent[large], small);
            if (old != small) {
                *changed = 1;
            }
        }
    }
}

__global__ void hybrid_cc_jump_kernel(int *parent, int num_nodes) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= num_nodes) return;

    int p = parent[idx];
    int gp = parent[p];
    if (p != gp) {
        parent[idx] = gp;
    }
}

// =========================================================================
// COOPERATIVE HYBRID DRIVERS (OpenMP + CUDA)
// =========================================================================

// Threshold to switch between CPU (OpenMP) and GPU (CUDA) BFS
#define HYBRID_THRESHOLD 1024

int* run_hybrid_bfs(int source, int num_nodes, int num_edges, 
                    const int *h_row_ptr, const int *h_col_ind,
                    double &hybrid_time, unsigned long long &hybrid_edges, int &hybrid_nodes_reached) {
    
    int *h_level = (int *)malloc(num_nodes * sizeof(int));

    // Device pointers
    int *d_row_ptr, *d_col_ind, *d_level, *d_parent;
    int *d_current_frontier, *d_next_frontier, *d_next_frontier_size;
    unsigned long long *d_edges_examined;

    // Allocate GPU memory
    cudaCheckError(cudaMalloc(&d_row_ptr, (num_nodes + 1) * sizeof(int)));
    cudaCheckError(cudaMalloc(&d_col_ind, num_edges * sizeof(int)));
    cudaCheckError(cudaMalloc(&d_level, num_nodes * sizeof(int)));
    cudaCheckError(cudaMalloc(&d_parent, num_nodes * sizeof(int)));
    cudaCheckError(cudaMalloc(&d_current_frontier, num_nodes * sizeof(int)));
    cudaCheckError(cudaMalloc(&d_next_frontier, num_nodes * sizeof(int)));
    cudaCheckError(cudaMalloc(&d_next_frontier_size, sizeof(int)));
    cudaCheckError(cudaMalloc(&d_edges_examined, sizeof(unsigned long long)));

    // Copy CSR graph structure to GPU
    cudaCheckError(cudaMemcpy(d_row_ptr, h_row_ptr, (num_nodes + 1) * sizeof(int), cudaMemcpyHostToDevice));
    cudaCheckError(cudaMemcpy(d_col_ind, h_col_ind, num_edges * sizeof(int), cudaMemcpyHostToDevice));

    int block_size = 256;
    int grid_size = (num_nodes + block_size - 1) / block_size;

    // Initialize levels on GPU
    hybrid_bfs_init_kernel<<<grid_size, block_size>>>(d_level, d_parent, num_nodes);
    cudaCheckError(cudaDeviceSynchronize());

    // Setup source node
    int h_source_level = 0;
    cudaCheckError(cudaMemcpy(&d_level[source], &h_source_level, sizeof(int), cudaMemcpyHostToDevice));
    cudaCheckError(cudaMemcpy(&d_parent[source], &source, sizeof(int), cudaMemcpyHostToDevice));

    // Host copy of levels and parent for CPU processing (Unified-like replication)
    int *host_level = (int *)malloc(num_nodes * sizeof(int));
    int *host_parent = (int *)malloc(num_nodes * sizeof(int));
    for (int i = 0; i < num_nodes; i++) {
        host_level[i] = -1;
        host_parent[i] = -1;
    }
    host_level[source] = 0;
    host_parent[source] = source;

    // Frontiers on host (for CPU OpenMP processing)
    int *host_current_frontier = (int *)malloc(num_nodes * sizeof(int));
    int *host_next_frontier = (int *)malloc(num_nodes * sizeof(int));
    host_current_frontier[0] = source;
    int current_frontier_size = 1;

    unsigned long long h_edges_examined = 0;
    cudaCheckError(cudaMemcpy(d_edges_examined, &h_edges_examined, sizeof(unsigned long long), cudaMemcpyHostToDevice));

    hybrid_nodes_reached = 1;
    int curr_level = 0;

    cudaCheckError(cudaDeviceSynchronize());
    double start_time = get_time();

    while (current_frontier_size > 0) {
        
        if (current_frontier_size < HYBRID_THRESHOLD) {
            // -------------------------------------------------------------
            // CPU SPARSE PHASE: OpenMP Multi-threaded processing
            // -------------------------------------------------------------
            // Synchronize device levels to host (if we just transitioned from GPU)
            cudaCheckError(cudaMemcpy(host_level, d_level, num_nodes * sizeof(int), cudaMemcpyDeviceToHost));
            cudaCheckError(cudaMemcpy(host_parent, d_parent, num_nodes * sizeof(int), cudaMemcpyDeviceToHost));

            int host_next_size = 0;
            long long cpu_edges_accumulated = 0;

            #pragma omp parallel for schedule(dynamic, 64) reduction(+:cpu_edges_accumulated)
            for (int i = 0; i < current_frontier_size; i++) {
                int u = host_current_frontier[i];
                int start = h_row_ptr[u];
                int end = h_row_ptr[u + 1];
                cpu_edges_accumulated += (end - start);

                for (int e = start; e < end; e++) {
                    int v = h_col_ind[e];
                    if (host_level[v] == -1) {
                        if (__sync_bool_compare_and_swap(&host_level[v], -1, curr_level + 1)) {
                            host_parent[v] = u;
                            int pos = __sync_fetch_and_add(&host_next_size, 1);
                            host_next_frontier[pos] = v;
                        }
                    }
                }
            }

            h_edges_examined += cpu_edges_accumulated;
            current_frontier_size = host_next_size;

            // Swap frontiers on host
            int *temp = host_current_frontier;
            host_current_frontier = host_next_frontier;
            host_next_frontier = temp;

            // Push processed values back to GPU to keep it in sync
            cudaCheckError(cudaMemcpy(d_level, host_level, num_nodes * sizeof(int), cudaMemcpyHostToDevice));
            cudaCheckError(cudaMemcpy(d_parent, host_parent, num_nodes * sizeof(int), cudaMemcpyHostToDevice));

            hybrid_nodes_reached += current_frontier_size;
        } 
        else {
            // -------------------------------------------------------------
            // GPU DENSE PHASE: Offload to CUDA
            // -------------------------------------------------------------
            // Sync host frontier to GPU
            cudaCheckError(cudaMemcpy(d_current_frontier, host_current_frontier, current_frontier_size * sizeof(int), cudaMemcpyHostToDevice));

            int h_next_size = 0;
            cudaCheckError(cudaMemcpy(d_next_frontier_size, &h_next_size, sizeof(int), cudaMemcpyHostToDevice));

            int frontier_grid_size = (current_frontier_size + block_size - 1) / block_size;
            hybrid_bfs_gpu_kernel<<<frontier_grid_size, block_size>>>(
                d_current_frontier, current_frontier_size,
                d_row_ptr, d_col_ind,
                d_level, d_parent,
                d_next_frontier, d_next_frontier_size,
                curr_level, d_edges_examined
            );
            cudaCheckError(cudaDeviceSynchronize());

            // Retrieve next frontier size and frontier nodes back to host
            cudaCheckError(cudaMemcpy(&current_frontier_size, d_next_frontier_size, sizeof(int), cudaMemcpyDeviceToHost));
            if (current_frontier_size > 0) {
                cudaCheckError(cudaMemcpy(host_current_frontier, d_next_frontier, current_frontier_size * sizeof(int), cudaMemcpyDeviceToHost));
            }

            hybrid_nodes_reached += current_frontier_size;
        }

        curr_level++;
    }

    cudaCheckError(cudaDeviceSynchronize());
    double end_time = get_time();
    hybrid_time = end_time - start_time;

    // Retrieve final results
    cudaCheckError(cudaMemcpy(&hybrid_edges, d_edges_examined, sizeof(unsigned long long), cudaMemcpyDeviceToHost));
    // Add CPU-accumulated edges to GPU total
    hybrid_edges += h_edges_examined;

    cudaCheckError(cudaMemcpy(h_level, d_level, num_nodes * sizeof(int), cudaMemcpyDeviceToHost));

    // Cleanup
    cudaFree(d_row_ptr);
    cudaFree(d_col_ind);
    cudaFree(d_level);
    cudaFree(d_parent);
    cudaFree(d_current_frontier);
    cudaFree(d_next_frontier);
    cudaFree(d_next_frontier_size);
    cudaFree(d_edges_examined);

    free(host_level);
    free(host_parent);
    free(host_current_frontier);
    free(host_next_frontier);

    return h_level;
}

// Cooperative Hybrid Connected Components
int run_hybrid_cc(int num_nodes, int num_edges, 
                  const int *h_row_ptr, const int *h_col_ind, 
                  double &hybrid_time, int &hybrid_iterations) {
    
    // Allocate host mapping array
    int *h_parent = (int *)malloc(num_nodes * sizeof(int));

    // -------------------------------------------------------------
    // 1. Host OpenMP Initialization & Isolation Filter
    // -------------------------------------------------------------
    // Pre-process and identify isolated nodes on CPU to avoid launching threads on GPU
    int isolated_nodes_count = 0;
    #pragma omp parallel for reduction(+:isolated_nodes_count)
    for (int i = 0; i < num_nodes; i++) {
        h_parent[i] = i;
        if (h_row_ptr[i] == h_row_ptr[i+1]) {
            isolated_nodes_count++;
        }
    }

    // Device pointers
    int *d_row_ptr, *d_col_ind, *d_parent, *d_changed;

    // Allocate GPU memory
    cudaCheckError(cudaMalloc(&d_row_ptr, (num_nodes + 1) * sizeof(int)));
    cudaCheckError(cudaMalloc(&d_col_ind, num_edges * sizeof(int)));
    cudaCheckError(cudaMalloc(&d_parent, num_nodes * sizeof(int)));
    cudaCheckError(cudaMalloc(&d_changed, sizeof(int)));

    // Copy graph representation and pre-initialized parents to GPU
    cudaCheckError(cudaMemcpy(d_row_ptr, h_row_ptr, (num_nodes + 1) * sizeof(int), cudaMemcpyHostToDevice));
    cudaCheckError(cudaMemcpy(d_col_ind, h_col_ind, num_edges * sizeof(int), cudaMemcpyHostToDevice));
    cudaCheckError(cudaMemcpy(d_parent, h_parent, num_nodes * sizeof(int), cudaMemcpyHostToDevice));

    int block_size = 256;
    int grid_size = (num_nodes + block_size - 1) / block_size;

    int h_changed = 1;
    hybrid_iterations = 0;

    cudaCheckError(cudaDeviceSynchronize());
    double start_time = get_time();

    // -------------------------------------------------------------
    // 2. GPU Hook-and-Jump Convergence Loop
    // -------------------------------------------------------------
    while (h_changed) {
        h_changed = 0;
        cudaCheckError(cudaMemcpy(d_changed, &h_changed, sizeof(int), cudaMemcpyHostToDevice));

        // GPU Parallel Hook step
        hybrid_cc_hook_kernel<<<grid_size, block_size>>>(d_row_ptr, d_col_ind, d_parent, num_nodes, d_changed);
        cudaCheckError(cudaDeviceSynchronize());

        // GPU Parallel Grandparent Jump step
        hybrid_cc_jump_kernel<<<grid_size, block_size>>>(d_parent, num_nodes);
        cudaCheckError(cudaDeviceSynchronize());

        cudaCheckError(cudaMemcpy(&h_changed, d_changed, sizeof(int), cudaMemcpyDeviceToHost));
        hybrid_iterations++;
    }

    // Copy parent mappings back to Host CPU
    cudaCheckError(cudaMemcpy(h_parent, d_parent, num_nodes * sizeof(int), cudaMemcpyDeviceToHost));

    // -------------------------------------------------------------
    // 3. Host OpenMP Path Compression & Counter
    // -------------------------------------------------------------
    // Use multi-threaded CPU OpenMP to flatten the parent trees and count components
    int num_components = 0;
    #pragma omp parallel for reduction(+:num_components)
    for (int i = 0; i < num_nodes; i++) {
        // Flatten
        int root = i;
        while (h_parent[root] != root) {
            root = h_parent[root];
        }
        
        // Path compress
        int curr = i;
        while (h_parent[curr] != root) {
            int next = h_parent[curr];
            h_parent[curr] = root;
            curr = next;
        }

        // Representative node counts
        if (h_parent[i] == i) {
            num_components++;
        }
    }

    double end_time = get_time();
    hybrid_time = end_time - start_time;

    // Cleanup
    cudaFree(d_row_ptr);
    cudaFree(d_col_ind);
    cudaFree(d_parent);
    cudaFree(d_changed);
    free(h_parent);

    return num_components;
}

// -------------------------------------------------------------------------
// Main Function
// -------------------------------------------------------------------------
int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 4) {
        printf("Usage: %s <dataset.csr> [source_vertex] [num_threads]\n", argv[0]);
        return 1;
    }

    int source_vertex = 0;
    if (argc >= 3) {
        source_vertex = atoi(argv[2]);
    }

    int num_threads = omp_get_max_threads();
    if (argc == 4) {
        num_threads = atoi(argv[3]);
        omp_set_num_threads(num_threads);
    }

    // Open CSR binary
    FILE *file = fopen(argv[1], "rb");
    if (!file) {
        printf("Error: Could not open CSR binary file %s\n", argv[1]);
        return 1;
    }

    int num_nodes, num_edges;
    if (fread(&num_nodes, sizeof(int), 1, file) != 1 ||
        fread(&num_edges, sizeof(int), 1, file) != 1) {
        printf("Error reading graph header.\n");
        fclose(file);
        return 1;
    }

    printf("=========================================================================\n");
    printf("   CUDA & OpenMP Cooperative Hybrid Graph Analytics Suite\n");
    printf("=========================================================================\n");
    printf("Graph Dataset : %s\n", argv[1]);
    printf("Number of Nodes: %d\n", num_nodes);
    printf("Number of Edges: %d\n", num_edges);
    printf("Source Vertex  : %d\n", source_vertex);
    printf("OMP CPU Threads: %d\n\n", num_threads);

    int *row_ptr = (int *)malloc((num_nodes + 1) * sizeof(int));
    int *col_ind = (int *)malloc(num_edges * sizeof(int));

    if (fread(row_ptr, sizeof(int), num_nodes + 1, file) != (size_t)(num_nodes + 1) ||
        fread(col_ind, sizeof(int), num_edges, file) != (size_t)num_edges) {
        printf("Error reading graph CSR structure.\n");
        fclose(file);
        free(row_ptr);
        free(col_ind);
        return 1;
    }
    fclose(file);

    // Get GPU Device Info
    int device_id = 0;
    cudaDeviceProp props;
    cudaGetDevice(&device_id);
    cudaGetDeviceProperties(&props, device_id);
    printf("Using GPU Device: %s (Compute Capability %d.%d)\n\n", 
           props.name, props.major, props.minor);

    // =========================================================================
    // SECTION 1: BREADTH-FIRST SEARCH (BFS)
    // =========================================================================
    printf("-------------------------------------------------------------------------\n");
    printf(" 1. Breadth-First Search (BFS) Benchmark\n");
    printf("-------------------------------------------------------------------------\n");

    // A. CPU Serial BFS
    double cpu_bfs_time = 0.0;
    long long cpu_bfs_edges = 0;
    int cpu_bfs_reached = 0;
    printf("[CPU] Running Serial BFS... ");
    fflush(stdout);
    int *cpu_bfs_levels = run_serial_bfs(source_vertex, num_nodes, row_ptr, col_ind, 
                                         cpu_bfs_time, cpu_bfs_edges, cpu_bfs_reached);
    printf("Done.\n");
    double cpu_bfs_teps = (cpu_bfs_time > 0) ? (cpu_bfs_edges / cpu_bfs_time) : 0.0;

    // B. GPU-CPU Hybrid BFS
    double hybrid_bfs_time = 0.0;
    unsigned long long hybrid_bfs_edges = 0;
    int hybrid_bfs_reached = 0;
    printf("[Hybrid] Running Cooperative BFS... ");
    fflush(stdout);
    int *hybrid_bfs_levels = run_hybrid_bfs(source_vertex, num_nodes, num_edges, row_ptr, col_ind,
                                            hybrid_bfs_time, hybrid_bfs_edges, hybrid_bfs_reached);
    printf("Done.\n");
    double hybrid_bfs_teps = (hybrid_bfs_time > 0) ? (hybrid_bfs_edges / hybrid_bfs_time) : 0.0;

    // C. Validation
    bool bfs_correct = true;
    for (int i = 0; i < num_nodes; i++) {
        if (cpu_bfs_levels[i] != hybrid_bfs_levels[i]) {
            bfs_correct = false;
            if (i < 10) {
                printf("  Mismatch at node %d: CPU Level = %d, Hybrid Level = %d\n", 
                       i, cpu_bfs_levels[i], hybrid_bfs_levels[i]);
            }
        }
    }

    printf("\n[BFS Results Summary]\n");
    printf("  %-15s %-15s %-15s\n", "Metric", "CPU (Serial)", "Hybrid (CPU+GPU)");
    printf("  %-15s %-15.6f %-15.6f\n", "Runtime (s)", cpu_bfs_time, hybrid_bfs_time);
    printf("  %-15s %-15d %-15d\n", "Nodes Reached", cpu_bfs_reached, hybrid_bfs_reached);
    printf("  %-15s %-15lld %-15llu\n", "Edges Examined", cpu_bfs_edges, hybrid_bfs_edges);
    printf("  %-15s %-15.2f %-15.2f\n", "Throughput (TEPS)", cpu_bfs_teps, hybrid_bfs_teps);
    printf("  Validation:      %s\n", bfs_correct ? "SUCCESS (Outputs match exactly!)" : "FAILURE (Outputs differ!)");
    printf("  Hybrid Speedup:  \033[1;32m%.2fx\033[0m\n\n", cpu_bfs_time / hybrid_bfs_time);

    free(cpu_bfs_levels);
    free(hybrid_bfs_levels);

    // =========================================================================
    // SECTION 2: CONNECTED COMPONENTS (CC)
    // =========================================================================
    printf("-------------------------------------------------------------------------\n");
    printf(" 2. Connected Components (CC) Benchmark\n");
    printf("-------------------------------------------------------------------------\n");

    // A. CPU Serial CC
    double cpu_cc_time = 0.0;
    printf("[CPU] Running Serial Connected Components... ");
    fflush(stdout);
    int cpu_cc_count = run_serial_cc(num_nodes, row_ptr, col_ind, cpu_cc_time);
    printf("Done.\n");

    // B. GPU-CPU Hybrid CC
    double hybrid_cc_time = 0.0;
    int hybrid_cc_iterations = 0;
    printf("[Hybrid] Running Cooperative Hook-and-Jump CC... ");
    fflush(stdout);
    int hybrid_cc_count = run_hybrid_cc(num_nodes, num_edges, row_ptr, col_ind, hybrid_cc_time, hybrid_cc_iterations);
    printf("Done.\n");

    // C. Validation
    bool cc_correct = (cpu_cc_count == hybrid_cc_count);

    printf("\n[CC Results Summary]\n");
    printf("  %-15s %-15s %-15s\n", "Metric", "CPU (Serial)", "Hybrid (CPU+GPU)");
    printf("  %-15s %-15.6f %-15.6f\n", "Runtime (s)", cpu_cc_time, hybrid_cc_time);
    printf("  %-15s %-15d %-15d\n", "Components Found", cpu_cc_count, hybrid_cc_count);
    if (hybrid_cc_iterations > 0) {
        printf("  %-15s %-15s %-15d\n", "GPU Iterations", "N/A", hybrid_cc_iterations);
    }
    printf("  Validation:      %s\n", cc_correct ? "SUCCESS (Component counts match exactly!)" : "FAILURE (Component counts differ!)");
    printf("  Hybrid Speedup:  \033[1;32m%.2fx\033[0m\n", cpu_cc_time / hybrid_cc_time);
    printf("=========================================================================\n");

    free(row_ptr);
    free(col_ind);

    return 0;
}
