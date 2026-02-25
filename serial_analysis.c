#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

// ---------------------------------------------------------
// Utility: Get exact wall time for performance measurement
// ---------------------------------------------------------
double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec * 1e-6;
}

// ---------------------------------------------------------
// 1. Serial Breadth-First Search (BFS)
// ---------------------------------------------------------
void serial_bfs(int source, int num_nodes, int *row_ptr, int *col_ind) {
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

    long long edges_examined = 0; 
    
    printf("\n--- Starting Serial BFS from node %d ---\n", source);
    double start_time = get_time();

    while (front < rear) {
        int current_node = queue[front++];
        int start_edge = row_ptr[current_node];
        int end_edge = row_ptr[current_node + 1];

        for (int i = start_edge; i < end_edge; i++) {
            int neighbor = col_ind[i];
            edges_examined++;

            if (level[neighbor] == -1) {
                level[neighbor] = level[current_node] + 1;
                parent[neighbor] = current_node;
                queue[rear++] = neighbor;
            }
        }
    }

    double end_time = get_time();
    double runtime = end_time - start_time;
    double teps = edges_examined / runtime;

    printf("BFS Runtime: %.6f seconds\n", runtime);
    printf("Nodes reached: %d\n", rear);
    printf("Edges examined: %lld\n", edges_examined);
    printf("Performance: %.2f TEPS\n", teps);

    free(level);
    free(parent);
    free(queue);
}

// ---------------------------------------------------------
// 2. Serial Connected Components (CC)
// ---------------------------------------------------------
void serial_cc(int num_nodes, int *row_ptr, int *col_ind) {
    int *components = (int *)malloc(num_nodes * sizeof(int));
    for (int i = 0; i < num_nodes; i++) {
        components[i] = -1; // -1 means unassigned
    }

    int *queue = (int *)malloc(num_nodes * sizeof(int));
    int num_components = 0;

    printf("\n--- Starting Serial Connected Components ---\n");
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
    
    printf("CC Runtime: %.6f seconds\n", end_time - start_time);
    printf("Total Connected Components found: %d\n", num_components);

    free(components);
    free(queue);
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

    // Run the baseline serial algorithms
    int source_node = 0; // You can change this or pass it as an argument
    serial_bfs(source_node, num_nodes, row_ptr, col_ind);
    
    serial_cc(num_nodes, row_ptr, col_ind);

    free(row_ptr);
    free(col_ind);

    return 0;
}