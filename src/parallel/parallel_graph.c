#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <omp.h> // Include OpenMP library

// ---------------------------------------------------------
// Utility: Get exact wall time for performance measurement
// ---------------------------------------------------------
double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec * 1e-6;
}

// ---------------------------------------------------------
// OpenMP Parallel Breadth-First Search (BFS)
// ---------------------------------------------------------
void openmp_bfs(int source, int num_nodes, int *row_ptr, int *col_ind) {
    int *level = (int *)malloc(num_nodes * sizeof(int));
    
    // Initialize levels in parallel
    #pragma omp parallel for
    for (int i = 0; i < num_nodes; i++) {
        level[i] = -1;
    }

    // Two frontiers for level-synchronous traversal
    int *current_frontier = (int *)malloc(num_nodes * sizeof(int));
    int *next_frontier = (int *)malloc(num_nodes * sizeof(int));
    
    int current_frontier_size = 0;
    int next_frontier_size = 0;

    // Setup source node
    level[source] = 0;
    current_frontier[current_frontier_size++] = source;

    long long edges_examined = 0; 
    int total_nodes_reached = 1;
    
    printf("\n--- Starting OpenMP Parallel BFS from node %d ---\n", source);
    double start_time = get_time();

    // While there are nodes to process in the current frontier
    while (current_frontier_size > 0) {
        next_frontier_size = 0; // Reset next frontier for the new level

        // PARALLEL REGION: Distribute nodes in the current frontier among threads
        #pragma omp parallel for schedule(dynamic, 64) reduction(+:edges_examined)
        for (int i = 0; i < current_frontier_size; i++) {
            int current_node = current_frontier[i];
            int start_edge = row_ptr[current_node];
            int end_edge = row_ptr[current_node + 1];

            edges_examined += (end_edge - start_edge);

            for (int e = start_edge; e < end_edge; e++) {
                int neighbor = col_ind[e];

                // Check if unvisited
                if (level[neighbor] == -1) {
                    
                    // ATOMIC VISITATION CONTROL: 
                    if (__sync_bool_compare_and_swap(&level[neighbor], -1, level[current_node] + 1)) {
                        
                        // Safely get an index in the next_frontier array and increment it
                        int idx = __sync_fetch_and_add(&next_frontier_size, 1);
                        next_frontier[idx] = neighbor;
                    }
                }
            }
        }

        // Swap frontiers for the next level
        int *temp = current_frontier;
        current_frontier = next_frontier;
        next_frontier = temp;
        
        total_nodes_reached += next_frontier_size;
        current_frontier_size = next_frontier_size;
    }

    double end_time = get_time();
    double runtime = end_time - start_time;
    double teps = edges_examined / runtime;

    printf("OpenMP BFS Runtime: %.6f seconds\n", runtime);
    printf("Nodes reached: %d\n", total_nodes_reached);
    printf("Edges examined: %lld\n", edges_examined);
    printf("Performance: %.2f TEPS\n", teps);

    free(level);
    free(current_frontier);
    free(next_frontier);
}

// ---------------------------------------------------------
// 2. Serial Connected Components (CC) - Baseline for Verification
// ---------------------------------------------------------
int run_serial_cc(int num_nodes, int *row_ptr, int *col_ind, double *cpu_time) {
    int *components = (int *)malloc(num_nodes * sizeof(int));
    for (int i = 0; i < num_nodes; i++) {
        components[i] = -1; // -1 means unassigned
    }

    int *queue = (int *)malloc(num_nodes * sizeof(int));
    int num_components = 0;

    double start_time = get_time();
    for (int i = 0; i < num_nodes; i++) {
        if (components[i] == -1) {
            // Found an unvisited node, start a new component traversal
            int current_component = num_components++;
            int front = 0, rear = 0;

            components[i] = current_component;
            queue[rear++] = i;

            // Inner BFS to label all mutually reachable vertices
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
    *cpu_time = end_time - start_time;

    free(components);
    free(queue);
    return num_components;
}

// ---------------------------------------------------------
// Connected Components (CC) Helpers
// ---------------------------------------------------------
static inline int find_root(const int *parent, int i) {
    while (parent[i] != i) {
        i = parent[i];
    }
    return i;
}

static inline int atomic_min(int *address, int val) {
    int old = *address;
    while (val < old) {
        int actual = __sync_val_compare_and_swap(address, old, val);
        if (actual == old) {
            return old;
        }
        old = actual;
    }
    return old;
}

// ---------------------------------------------------------
// 3. OpenMP Parallel Hook-and-Jump Connected Components (CC)
// ---------------------------------------------------------
int openmp_cc(int num_nodes, int *row_ptr, int *col_ind, double *omp_time, int *omp_iterations) {
    int *parent = (int *)malloc(num_nodes * sizeof(int));

    // Initialize component parents: parent[i] = i
    #pragma omp parallel for
    for (int i = 0; i < num_nodes; i++) {
        parent[i] = i;
    }

    int changed = 1;
    int iterations = 0;

    double start_time = get_time();

    // Hook-and-Jump convergence loop
    while (changed) {
        changed = 0;

        // 1. Hook step: Map larger component tree root to smaller component tree root
        #pragma omp parallel for reduction(+:changed) schedule(dynamic, 64)
        for (int u = 0; u < num_nodes; u++) {
            int root_u = find_root(parent, u);
            int start_edge = row_ptr[u];
            int end_edge = row_ptr[u + 1];

            for (int i = start_edge; i < end_edge; i++) {
                int v = col_ind[i];
                int root_v = find_root(parent, v);

                if (root_u != root_v) {
                    int small = root_u < root_v ? root_u : root_v;
                    int large = root_u > root_v ? root_u : root_v;

                    // Thread-safe update using lock-free CAS-based atomic minimum
                    int old = atomic_min(&parent[large], small);
                    if (old != small) {
                        changed = 1;
                    }
                }
            }
        }

        // 2. Jump step: Flatten tree structure by pointer jumping to grandparent
        #pragma omp parallel for schedule(static)
        for (int i = 0; i < num_nodes; i++) {
            int p = parent[i];
            int gp = parent[p];
            if (p != gp) {
                parent[i] = gp;
            }
        }

        iterations++;
    }

    // 3. Final path compression to ensure all nodes point directly to their root
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < num_nodes; i++) {
        int root = find_root(parent, i);
        int curr = i;
        while (parent[curr] != root) {
            int next = parent[curr];
            parent[curr] = root;
            curr = next;
        }
    }

    double end_time = get_time();
    *omp_time = end_time - start_time;
    *omp_iterations = iterations;

    // A node 'i' is the root/representative of a component if parent[i] == i
    int num_components = 0;
    #pragma omp parallel for reduction(+:num_components)
    for (int i = 0; i < num_nodes; i++) {
        if (parent[i] == i) {
            num_components++;
        }
    }

    free(parent);
    return num_components;
}

// ---------------------------------------------------------
// Main Function: Read CSR Binary and Execute Benchmarks
// ---------------------------------------------------------
int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        printf("Usage: %s <dataset.csr> [num_threads]\n", argv[0]);
        return 1;
    }

    if (argc == 3) {
        int threads = atoi(argv[2]);
        omp_set_num_threads(threads);
    }

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
    printf("   OpenMP Shared-Memory Parallel Graph Analytics Suite\n");
    printf("=========================================================================\n");
    printf("Graph Dataset : %s\n", argv[1]);
    printf("Number of Nodes: %d\n", num_nodes);
    printf("Number of Edges: %d\n", num_edges);
    printf("Max Threads    : %d\n\n", omp_get_max_threads());

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

    int source_node = 0;

    // =========================================================================
    // SECTION 1: BREADTH-FIRST SEARCH (BFS)
    // =========================================================================
    printf("-------------------------------------------------------------------------\n");
    printf(" 1. Breadth-First Search (BFS) Benchmark\n");
    printf("-------------------------------------------------------------------------\n");
    openmp_bfs(source_node, num_nodes, row_ptr, col_ind);

    // =========================================================================
    // SECTION 2: CONNECTED COMPONENTS (CC)
    // =========================================================================
    printf("\n-------------------------------------------------------------------------\n");
    printf(" 2. Connected Components (CC) Benchmark\n");
    printf("-------------------------------------------------------------------------\n");

    // A. CPU Serial Baseline
    double cpu_cc_time = 0.0;
    printf("[CPU] Running Serial Connected Components... ");
    fflush(stdout);
    int cpu_cc_count = run_serial_cc(num_nodes, row_ptr, col_ind, &cpu_cc_time);
    printf("Done.\n");

    // B. OpenMP Parallel Hook-and-Jump
    double omp_cc_time = 0.0;
    int omp_iterations = 0;
    printf("[OpenMP] Running Parallel Hook-and-Jump CC... ");
    fflush(stdout);
    int omp_cc_count = openmp_cc(num_nodes, row_ptr, col_ind, &omp_cc_time, &omp_iterations);
    printf("Done.\n");

    // C. Validation & Comparison
    int cc_correct = (cpu_cc_count == omp_cc_count);

    printf("\n[CC Results Summary]\n");
    printf("  %-15s %-15s %-15s\n", "Metric", "CPU (Serial)", "OpenMP (Parallel)");
    printf("  %-15s %-15.6f %-15.6f\n", "Runtime (s)", cpu_cc_time, omp_cc_time);
    printf("  %-15s %-15d %-15d\n", "Components Found", cpu_cc_count, omp_cc_count);
    if (omp_iterations > 0) {
        printf("  %-15s %-15s %-15d\n", "Iterations", "N/A", omp_iterations);
    }
    printf("  Validation:      %s\n", cc_correct ? "SUCCESS (Component counts match exactly!)" : "FAILURE (Component counts differ!)");
    if (omp_cc_time > 0) {
        printf("  Speedup:         \033[1;32m%.2fx\033[0m\n", cpu_cc_time / omp_cc_time);
    }
    printf("=========================================================================\n");

    free(row_ptr);
    free(col_ind);

    return 0;
}