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
// Main Function: Read CSR Binary and Execute
// ---------------------------------------------------------
int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <dataset.csr>\n", argv[0]);
        return 1;
    }

    FILE *file = fopen(argv[1], "rb");
    if (!file) {
        printf("Error: Could not open CSR binary file %s\n", argv[1]);
        return 1;
    }

    int num_nodes, num_edges;
    fread(&num_nodes, sizeof(int), 1, file);
    fread(&num_edges, sizeof(int), 1, file);

    printf("Loading Graph: %d Nodes, %d Edges\n", num_nodes, num_edges);

    int *row_ptr = (int *)malloc((num_nodes + 1) * sizeof(int));
    int *col_ind = (int *)malloc(num_edges * sizeof(int));

    fread(row_ptr, sizeof(int), num_nodes + 1, file);
    fread(col_ind, sizeof(int), num_edges, file);
    fclose(file);

    int source_node = 0; 
    
    // Execute OpenMP BFS
    openmp_bfs(source_node, num_nodes, row_ptr, col_ind);

    free(row_ptr);
    free(col_ind);

    return 0;
}