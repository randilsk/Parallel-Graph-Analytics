/*
 * openmp_bfs.c — OpenMP Parallel BFS
 *
 * Build:
 *   gcc -O3 -Wall -fopenmp -o openmp_bfs src/openmp_bfs.c
 *
 * Run:
 *   ./bin/openmp_bfs outputs/csr_format_web-google
 *   ./bin/openmp_bfs outputs/csr_format_web-google 5     (custom source node)
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <omp.h>

// ---------------------------------------------------------
// Utility: wall time
// ---------------------------------------------------------
double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec * 1e-6;
}

// ---------------------------------------------------------
// OpenMP Parallel BFS (unchanged — only main() was fixed)
// ---------------------------------------------------------
void openmp_bfs(int source, int num_nodes, int *row_ptr, int *col_ind) {
    int *level = (int *)malloc(num_nodes * sizeof(int));

    #pragma omp parallel for
    for (int i = 0; i < num_nodes; i++)
        level[i] = -1;

    int *current_frontier = (int *)malloc(num_nodes * sizeof(int));
    int *next_frontier    = (int *)malloc(num_nodes * sizeof(int));

    int current_frontier_size = 0;
    int next_frontier_size    = 0;

    level[source] = 0;
    current_frontier[current_frontier_size++] = source;

    long long edges_examined   = 0;
    int       total_nodes_reached = 1;

    printf("\n--- OpenMP Parallel BFS from node %d (%d threads) ---\n",
           source, omp_get_max_threads());
    double start_time = get_time();

    while (current_frontier_size > 0) {
        next_frontier_size = 0;

        #pragma omp parallel for schedule(dynamic, 64) reduction(+:edges_examined)
        for (int i = 0; i < current_frontier_size; i++) {
            int node       = current_frontier[i];
            int start_edge = row_ptr[node];
            int end_edge   = row_ptr[node + 1];

            edges_examined += (end_edge - start_edge);

            for (int e = start_edge; e < end_edge; e++) {
                int neighbor = col_ind[e];
                if (level[neighbor] == -1) {
                    if (__sync_bool_compare_and_swap(&level[neighbor], -1,
                                                     level[node] + 1)) {
                        int idx = __sync_fetch_and_add(&next_frontier_size, 1);
                        next_frontier[idx] = neighbor;
                    }
                }
            }
        }

        int *temp = current_frontier;
        current_frontier = next_frontier;
        next_frontier    = temp;

        total_nodes_reached  += next_frontier_size;
        current_frontier_size = next_frontier_size;
    }

    double runtime = get_time() - start_time;

    printf("OpenMP BFS Runtime : %.6f seconds\n", runtime);
    printf("Nodes reached      : %d / %d\n", total_nodes_reached, num_nodes);
    printf("Edges examined     : %lld\n", edges_examined);
    if (runtime > 0)
        printf("Performance        : %.2f MTEPS\n", edges_examined / runtime / 1e6);

    free(level);
    free(current_frontier);
    free(next_frontier);
}

// ---------------------------------------------------------
// Main
// ---------------------------------------------------------
int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <graph.csr> [source_node]\n", argv[0]);
        return 1;
    }

    int source_node = (argc == 3) ? atoi(argv[2]) : 0;

    FILE *file = fopen(argv[1], "rb");
    if (!file) {
        fprintf(stderr, "Error: Could not open %s\n", argv[1]);
        return 1;
    }

    int num_nodes, num_edges;
    if (fread(&num_nodes, sizeof(int), 1, file) != 1 ||
        fread(&num_edges, sizeof(int), 1, file) != 1) {
        fprintf(stderr, "Error reading header\n");
        fclose(file);
        return 1;
    }

    printf("Graph: %d nodes, %d edges\n", num_nodes, num_edges);

    int *row_ptr = (int *)malloc((num_nodes + 1) * sizeof(int));
    int *col_ind = (int *)malloc(num_edges * sizeof(int));

    if (fread(row_ptr, sizeof(int), num_nodes + 1, file) != (size_t)(num_nodes + 1) ||
        fread(col_ind, sizeof(int), num_edges,      file) != (size_t)num_edges) {
        fprintf(stderr, "Error reading graph data\n");
        fclose(file);
        return 1;
    }
    fclose(file);

    if (source_node < 0 || source_node >= num_nodes) {
        fprintf(stderr, "Invalid source node %d (graph has %d nodes)\n",
                source_node, num_nodes);
        return 1;
    }

    openmp_bfs(source_node, num_nodes, row_ptr, col_ind);

    free(row_ptr);
    free(col_ind);
    return 0;
}