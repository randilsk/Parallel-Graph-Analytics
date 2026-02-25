#include <stdio.h>
#include <stdlib.h>

int main() {

    FILE *file = fopen("data-sets/web-Google.txt", "r");
    if (file == NULL) {
        printf("Error opening file\n");
        return 1;
    }

    int u, v;
    int max_node = -1;
    int num_edges = 0;

    // -------- FIRST PASS --------
    // count edges and find max node id
    char line[256];

    while (fgets(line, sizeof(line), file)) {

        if (line[0] == '#') continue;   // skip comments

        sscanf(line, "%d %d", &u, &v);

        if (u > max_node) max_node = u;
        if (v > max_node) max_node = v;

        num_edges++;
    }

    int num_nodes = max_node + 1;

    printf("Nodes: %d\n", num_nodes);
    printf("Edges: %d\n", num_edges);

    // -------- ALLOCATE DEGREE ARRAY --------
    int *degree = calloc(num_nodes, sizeof(int));

    // -------- SECOND PASS --------
    // count degree of each node
    rewind(file);

    while (fgets(line, sizeof(line), file)) {

        if (line[0] == '#') continue;

        sscanf(line, "%d %d", &u, &v);
        degree[u]++;
    }

    // -------- BUILD ROW_PTR --------
    int *row_ptr = malloc((num_nodes + 1) * sizeof(int));
    row_ptr[0] = 0;

    for (int i = 0; i < num_nodes; i++) {
        row_ptr[i + 1] = row_ptr[i] + degree[i];
    }

    // -------- BUILD COL_IND --------
    int *col_ind = malloc(num_edges * sizeof(int));

    // temporary copy to track insert positions
    int *current = malloc(num_nodes * sizeof(int));
    for (int i = 0; i < num_nodes; i++)
        current[i] = row_ptr[i];

    rewind(file);

    while (fgets(line, sizeof(line), file)) {

        if (line[0] == '#') continue;

        sscanf(line, "%d %d", &u, &v);

        int pos = current[u]++;
        col_ind[pos] = v;
    }

    fclose(file);

    printf("CSR construction complete.\n");

    // -------- OPTIONAL: TEST PRINT --------
    printf("\nNeighbors of node 0:\n");
    for (int i = row_ptr[0]; i < row_ptr[1]; i++) {
        printf("%d ", col_ind[i]);
    }
    printf("\n");

    
    // -------- OPTIONAL: TEST PRINT --------
    printf("\nNeighbors of node 1:\n");
    for (int i = row_ptr[1]; i < row_ptr[2]; i++) {
        printf("%d ", col_ind[i]);
    }
    printf("\n");

    printf("CSR construction complete.\n");



    // -------- FREE MEMORY --------
    free(degree);
    free(row_ptr);
    free(col_ind);
    free(current);

    return 0;
}