/*
 * serial_analysis.c — Serial BFS and Connected Components baseline
 *
 * Build:
 *   gcc -O3 -Wall -o serial_analysis serial_analysis.c
 *
 * Run:
 *   ./serial_analysis outputs/csr_format_web-google
 *   ./serial_analysis outputs/csr_format_web-google 5     (custom source node)
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

// ---------------------------------------------------------
// Utility: wall time
// ---------------------------------------------------------
double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec * 1e-6;
}

// ---------------------------------------------------------
// 1. Serial BFS
// ---------------------------------------------------------
void serial_bfs(int source, int num_nodes, int *row_ptr, int *col_ind) {
    int *level  = (int *)malloc(num_nodes * sizeof(int));
    int *parent = (int *)malloc(num_nodes * sizeof(int));

    for (int i = 0; i < num_nodes; i++) {
        level[i]  = -1;
        parent[i] = -1;
    }

    int *queue = (int *)malloc(num_nodes * sizeof(int));
    int front = 0, rear = 0;

    level[source]  = 0;
    parent[source] = source;
    queue[rear++]  = source;

    long long edges_examined = 0;

    printf("\n--- Serial BFS from node %d ---\n", source);
    double start_time = get_time();

    while (front < rear) {
        int node  = queue[front++];
        int start = row_ptr[node];
        int end   = row_ptr[node + 1];

        for (int i = start; i < end; i++) {
            int neighbor = col_ind[i];
            edges_examined++;
            if (level[neighbor] == -1) {
                level[neighbor]  = level[node] + 1;
                parent[neighbor] = node;
                queue[rear++]    = neighbor;
            }
        }
    }

    double runtime = get_time() - start_time;

    printf("BFS Runtime     : %.6f seconds\n", runtime);
    printf("Nodes reached   : %d / %d\n", rear, num_nodes);
    printf("Edges examined  : %lld\n", edges_examined);
    if (runtime > 0)
        printf("Performance     : %.2f MTEPS\n", edges_examined / runtime / 1e6);

    free(level);
    free(parent);
    free(queue);
}

// ---------------------------------------------------------
// 2. Serial Connected Components (BFS-based)
// ---------------------------------------------------------
void serial_cc(int num_nodes, int *row_ptr, int *col_ind) {
    int *components = (int *)malloc(num_nodes * sizeof(int));
    for (int i = 0; i < num_nodes; i++)
        components[i] = -1;

    int *queue = (int *)malloc(num_nodes * sizeof(int));
    int num_components = 0;
    int largest_size   = 0;

    printf("\n--- Serial Connected Components ---\n");
    double start_time = get_time();

    for (int i = 0; i < num_nodes; i++) {
        if (components[i] != -1) continue;

        int comp  = num_components++;
        int front = 0, rear = 0;
        int size  = 0;

        components[i] = comp;
        queue[rear++] = i;

        while (front < rear) {
            int curr  = queue[front++];
            size++;
            int start = row_ptr[curr];
            int end   = row_ptr[curr + 1];

            for (int e = start; e < end; e++) {
                int neighbor = col_ind[e];
                if (components[neighbor] == -1) {
                    components[neighbor] = comp;
                    queue[rear++] = neighbor;
                }
            }
        }

        if (size > largest_size) largest_size = size;
    }

    double runtime = get_time() - start_time;

    printf("CC Runtime      : %.6f seconds\n", runtime);
    printf("Components found: %d\n", num_components);
    printf("Largest component: %d nodes\n", largest_size);

    free(components);
    free(queue);
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

    serial_bfs(source_node, num_nodes, row_ptr, col_ind);
    serial_cc(num_nodes, row_ptr, col_ind);

    free(row_ptr);
    free(col_ind);
    return 0;
}