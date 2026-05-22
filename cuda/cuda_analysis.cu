#include <stdio.h>
#include <stdlib.h>
#include <chrono>
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
// Utility: High-precision timer using C++ std::chrono (Windows/Linux compatible)
// -------------------------------------------------------------------------
double get_time() {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration<double>(duration).count();
}

// =========================================================================
// SERIAL BASELINE IMPLEMENTATIONS (For Accuracy Validation & Speedup comparison)
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
    return level; // caller must free
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
// CUDA GPU IMPLEMENTATIONS
// =========================================================================

// -------------------------------------------------------------------------
// 1. CUDA Breadth-First Search (BFS) Kernels
// -------------------------------------------------------------------------

__global__ void bfs_init_kernel(int *level, int *parent, int num_nodes) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < num_nodes) {
        level[idx] = -1;
        parent[idx] = -1;
    }
}

__global__ void cuda_bfs_kernel(
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

    // Thread-local performance counter: Accumulate and write atomically once to reduce contention
    int local_edges = end_edge - start_edge;
    if (local_edges > 0) {
        atomicAdd(edges_examined, (unsigned long long)local_edges);
    }

    for (int i = start_edge; i < end_edge; i++) {
        int neighbor = col_ind[i];

        // ATOMIC VISITATION CONTROL: Set level atomically if currently -1
        int old_level = atomicCAS(&level[neighbor], -1, curr_level + 1);
        if (old_level == -1) {
            // Successfully visited and set the level!
            parent[neighbor] = current_node;
            int insert_pos = atomicAdd(next_frontier_size, 1);
            next_frontier[insert_pos] = neighbor;
        }
    }
}

// -------------------------------------------------------------------------
// 2. CUDA Connected Components (CC) Kernels (Hooking & Pointer-Jumping)
// -------------------------------------------------------------------------

__global__ void cc_init_kernel(int *parent, int num_nodes) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < num_nodes) {
        parent[idx] = idx;
    }
}

__global__ void cc_hook_kernel(const int *row_ptr, const int *col_ind, int *parent, int num_nodes, int *changed) {
    int u = blockIdx.x * blockDim.x + threadIdx.x;
    if (u >= num_nodes) return;

    int p_u = parent[u];
    int start_edge = row_ptr[u];
    int end_edge = row_ptr[u + 1];

    for (int i = start_edge; i < end_edge; i++) {
        int v = col_ind[i];
        int p_v = parent[v];

        // If the components are different, hook the larger component root to the smaller one
        if (p_u < p_v) {
            int old_val = atomicMin(&parent[p_v], p_u);
            if (old_val != p_u) {
                *changed = 1;
            }
        }
    }
}

__global__ void cc_jump_kernel(int *parent, int num_nodes, int *changed) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= num_nodes) return;

    int p = parent[idx];
    int gp = parent[p];
    if (p != gp) {
        parent[idx] = gp;
        *changed = 1;
    }
}

// =========================================================================
// HOST CONTROL RUNNERS
// =========================================================================

// GPU BFS Driver
int* run_cuda_bfs(int source, int num_nodes, int num_edges, 
                 const int *h_row_ptr, const int *h_col_ind,
                 double &gpu_time, unsigned long long &gpu_edges, int &gpu_nodes_reached) {
    
    // Allocate host memory for the result
    int *h_level = (int *)malloc(num_nodes * sizeof(int));

    // Device pointers
    int *d_row_ptr, *d_col_ind, *d_level, *d_parent;
    int *d_current_frontier, *d_next_frontier, *d_next_frontier_size;
    unsigned long long *d_edges_examined;

    // Allocate GPU global memory
    cudaCheckError(cudaMalloc(&d_row_ptr, (num_nodes + 1) * sizeof(int)));
    cudaCheckError(cudaMalloc(&d_col_ind, num_edges * sizeof(int)));
    cudaCheckError(cudaMalloc(&d_level, num_nodes * sizeof(int)));
    cudaCheckError(cudaMalloc(&d_parent, num_nodes * sizeof(int)));
    cudaCheckError(cudaMalloc(&d_current_frontier, num_nodes * sizeof(int)));
    cudaCheckError(cudaMalloc(&d_next_frontier, num_nodes * sizeof(int)));
    cudaCheckError(cudaMalloc(&d_next_frontier_size, sizeof(int)));
    cudaCheckError(cudaMalloc(&d_edges_examined, sizeof(unsigned long long)));

    // Copy CSR representation to GPU
    cudaCheckError(cudaMemcpy(d_row_ptr, h_row_ptr, (num_nodes + 1) * sizeof(int), cudaMemcpyHostToDevice));
    cudaCheckError(cudaMemcpy(d_col_ind, h_col_ind, num_edges * sizeof(int), cudaMemcpyHostToDevice));

    int block_size = 256;
    int grid_size = (num_nodes + block_size - 1) / block_size;

    // Initialize level and parent arrays on the GPU
    bfs_init_kernel<<<grid_size, block_size>>>(d_level, d_parent, num_nodes);
    cudaCheckError(cudaDeviceSynchronize());

    // Setup source node on GPU
    int h_source_level = 0;
    cudaCheckError(cudaMemcpy(&d_level[source], &h_source_level, sizeof(int), cudaMemcpyHostToDevice));
    cudaCheckError(cudaMemcpy(&d_parent[source], &source, sizeof(int), cudaMemcpyHostToDevice));

    // Initialize frontier queue on GPU
    cudaCheckError(cudaMemcpy(&d_current_frontier[0], &source, sizeof(int), cudaMemcpyHostToDevice));
    int current_frontier_size = 1;

    unsigned long long h_edges_examined = 0;
    cudaCheckError(cudaMemcpy(d_edges_examined, &h_edges_examined, sizeof(unsigned long long), cudaMemcpyHostToDevice));

    gpu_nodes_reached = 1;
    int curr_level = 0;

    // Sync device before starting chronometer
    cudaCheckError(cudaDeviceSynchronize());
    double start_time = get_time();

    // Work-efficient level-synchronous traversal loop
    while (current_frontier_size > 0) {
        int h_next_size = 0;
        cudaCheckError(cudaMemcpy(d_next_frontier_size, &h_next_size, sizeof(int), cudaMemcpyHostToDevice));

        int frontier_grid_size = (current_frontier_size + block_size - 1) / block_size;
        cuda_bfs_kernel<<<frontier_grid_size, block_size>>>(
            d_current_frontier, current_frontier_size,
            d_row_ptr, d_col_ind,
            d_level, d_parent,
            d_next_frontier, d_next_frontier_size,
            curr_level, d_edges_examined
        );
        cudaCheckError(cudaDeviceSynchronize());

        // Copy size of next frontier back to host
        cudaCheckError(cudaMemcpy(&current_frontier_size, d_next_frontier_size, sizeof(int), cudaMemcpyHostToDevice));

        // Swap frontier buffers for the next level
        int *temp = d_current_frontier;
        d_current_frontier = d_next_frontier;
        d_next_frontier = temp;

        gpu_nodes_reached += current_frontier_size;
        curr_level++;
    }

    cudaCheckError(cudaDeviceSynchronize());
    double end_time = get_time();
    gpu_time = end_time - start_time;

    // Retrieve stats and final results
    cudaCheckError(cudaMemcpy(&gpu_edges, d_edges_examined, sizeof(unsigned long long), cudaMemcpyDeviceToHost));
    cudaCheckError(cudaMemcpy(h_level, d_level, num_nodes * sizeof(int), cudaMemcpyDeviceToHost));

    // Cleanup GPU resources
    cudaFree(d_row_ptr);
    cudaFree(d_col_ind);
    cudaFree(d_level);
    cudaFree(d_parent);
    cudaFree(d_current_frontier);
    cudaFree(d_next_frontier);
    cudaFree(d_next_frontier_size);
    cudaFree(d_edges_examined);

    return h_level;
}

// GPU CC Driver
int run_cuda_cc(int num_nodes, int num_edges, 
                const int *h_row_ptr, const int *h_col_ind, 
                double &gpu_time, int &gpu_iterations) {
    
    int *h_parent = (int *)malloc(num_nodes * sizeof(int));

    // Device pointers
    int *d_row_ptr, *d_col_ind, *d_parent, *d_changed;

    // Allocate GPU global memory
    cudaCheckError(cudaMalloc(&d_row_ptr, (num_nodes + 1) * sizeof(int)));
    cudaCheckError(cudaMalloc(&d_col_ind, num_edges * sizeof(int)));
    cudaCheckError(cudaMalloc(&d_parent, num_nodes * sizeof(int)));
    cudaCheckError(cudaMalloc(&d_changed, sizeof(int)));

    // Copy CSR representation to GPU
    cudaCheckError(cudaMemcpy(d_row_ptr, h_row_ptr, (num_nodes + 1) * sizeof(int), cudaMemcpyHostToDevice));
    cudaCheckError(cudaMemcpy(d_col_ind, h_col_ind, num_edges * sizeof(int), cudaMemcpyHostToDevice));

    int block_size = 256;
    int grid_size = (num_nodes + block_size - 1) / block_size;

    // Initialize component parents: parent[i] = i
    cc_init_kernel<<<grid_size, block_size>>>(d_parent, num_nodes);
    cudaCheckError(cudaDeviceSynchronize());

    int h_changed = 1;
    gpu_iterations = 0;

    // Sync device before starting chronometer
    cudaCheckError(cudaDeviceSynchronize());
    double start_time = get_time();

    // Hook-and-Jump loop until parent mapping converges
    while (h_changed) {
        h_changed = 0;
        cudaCheckError(cudaMemcpy(d_changed, &h_changed, sizeof(int), cudaMemcpyHostToDevice));

        // 1. Hook step: Map larger component tree root to smaller component tree root
        cc_hook_kernel<<<grid_size, block_size>>>(d_row_ptr, d_col_ind, d_parent, num_nodes, d_changed);
        cudaCheckError(cudaDeviceSynchronize());

        // 2. Jump step: Flatten tree structure by pointer jumping to grandparent
        cc_jump_kernel<<<grid_size, block_size>>>(d_parent, num_nodes, d_changed);
        cudaCheckError(cudaDeviceSynchronize());

        // Check if convergence was reached in this round
        cudaCheckError(cudaMemcpy(&h_changed, d_changed, sizeof(int), cudaMemcpyDeviceToHost));
        gpu_iterations++;
    }

    cudaCheckError(cudaDeviceSynchronize());
    double end_time = get_time();
    gpu_time = end_time - start_time;

    // Copy parents mapping back to count unique components
    cudaCheckError(cudaMemcpy(h_parent, d_parent, num_nodes * sizeof(int), cudaMemcpyDeviceToHost));

    // A node 'i' is the root/representative of a component if parent[i] == i
    int num_components = 0;
    for (int i = 0; i < num_nodes; i++) {
        if (h_parent[i] == i) {
            num_components++;
        }
    }

    // Cleanup GPU resources
    cudaFree(d_row_ptr);
    cudaFree(d_col_ind);
    cudaFree(d_parent);
    cudaFree(d_changed);
    free(h_parent);

    return num_components;
}

// -------------------------------------------------------------------------
// Main Function: Load Graph, Execute Benchmarks, Print Comparisons
// -------------------------------------------------------------------------
int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        printf("Usage: %s <dataset.csr> [source_vertex]\n", argv[0]);
        return 1;
    }

    int source_vertex = 0;
    if (argc == 3) {
        source_vertex = atoi(argv[2]);
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
    printf("   CUDA Parallel Graph Analytics Suite\n");
    printf("=========================================================================\n");
    printf("Graph Dataset : %s\n", argv[1]);
    printf("Number of Nodes: %d\n", num_nodes);
    printf("Number of Edges: %d\n", num_edges);
    printf("Source Vertex  : %d\n\n", source_vertex);

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

    // B. GPU CUDA BFS
    double gpu_bfs_time = 0.0;
    unsigned long long gpu_bfs_edges = 0;
    int gpu_bfs_reached = 0;
    printf("[GPU] Running CUDA Parallel BFS... ");
    fflush(stdout);
    int *gpu_bfs_levels = run_cuda_bfs(source_vertex, num_nodes, num_edges, row_ptr, col_ind,
                                        gpu_bfs_time, gpu_bfs_edges, gpu_bfs_reached);
    printf("Done.\n");
    double gpu_bfs_teps = (gpu_bfs_time > 0) ? (gpu_bfs_edges / gpu_bfs_time) : 0.0;

    // C. Validation & Comparison
    bool bfs_correct = true;
    for (int i = 0; i < num_nodes; i++) {
        if (cpu_bfs_levels[i] != gpu_bfs_levels[i]) {
            bfs_correct = false;
            // Print a few mismatches for debugging if any
            if (i < 100) {
                printf("  Mismatch at node %d: CPU Level = %d, GPU Level = %d\n", 
                       i, cpu_bfs_levels[i], gpu_bfs_levels[i]);
            }
        }
    }

    printf("\n[BFS Results Summary]\n");
    printf("  %-15s %-15s %-15s\n", "Metric", "CPU (Serial)", "GPU (CUDA)");
    printf("  %-15s %-15.6f %-15.6f\n", "Runtime (s)", cpu_bfs_time, gpu_bfs_time);
    printf("  %-15s %-15d %-15d\n", "Nodes Reached", cpu_bfs_reached, gpu_bfs_reached);
    printf("  %-15s %-15lld %-15llu\n", "Edges Examined", cpu_bfs_edges, gpu_bfs_edges);
    printf("  %-15s %-15.2f %-15.2f\n", "Throughput (TEPS)", cpu_bfs_teps, gpu_bfs_teps);
    printf("  Validation:      %s\n", bfs_correct ? "SUCCESS (Outputs match exactly!)" : "FAILURE (Outputs differ!)");
    printf("  GPU Speedup:     \033[1;32m%.2fx\033[0m\n\n", cpu_bfs_time / gpu_bfs_time);

    free(cpu_bfs_levels);
    free(gpu_bfs_levels);

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

    // B. GPU CUDA CC
    double gpu_cc_time = 0.0;
    int gpu_cc_iterations = 0;
    printf("[GPU] Running CUDA Parallel Hook-and-Jump CC... ");
    fflush(stdout);
    int gpu_cc_count = run_cuda_cc(num_nodes, num_edges, row_ptr, col_ind, gpu_cc_time, gpu_cc_iterations);
    printf("Done.\n");

    // C. Validation & Comparison
    bool cc_correct = (cpu_cc_count == gpu_cc_count);

    printf("\n[CC Results Summary]\n");
    printf("  %-15s %-15s %-15s\n", "Metric", "CPU (Serial)", "GPU (CUDA)");
    printf("  %-15s %-15.6f %-15.6f\n", "Runtime (s)", cpu_cc_time, gpu_cc_time);
    printf("  %-15s %-15d %-15d\n", "Components Found", cpu_cc_count, gpu_cc_count);
    if (gpu_cc_iterations > 0) {
        printf("  %-15s %-15s %-15d\n", "GPU Iterations", "N/A", gpu_cc_iterations);
    }
    printf("  Validation:      %s\n", cc_correct ? "SUCCESS (Component counts match exactly!)" : "FAILURE (Component counts differ!)");
    printf("  GPU Speedup:     \033[1;32m%.2fx\033[0m\n", cpu_cc_time / gpu_cc_time);
    printf("=========================================================================\n");

    free(row_ptr);
    free(col_ind);

    return 0;
}
