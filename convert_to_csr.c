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
    int num_directed_edges = 0;

    // -------- FIRST PASS --------
    // count directed edges and find max node id
    char line[256];

    while (fgets(line, sizeof(line), file)) {

        if (line[0] == '#') continue;   // skip comments

        sscanf(line, "%d %d", &u, &v);

        if (u > max_node) max_node = u;
        if (v > max_node) max_node = v;

        num_directed_edges++;
    }

    int num_nodes = max_node + 1;

    printf("Nodes: %d\n", num_nodes);
    printf("Original Directed Edges: %d\n", num_directed_edges);

    // -------- ALLOCATE DEGREE ARRAY --------
    int *degree = calloc(num_nodes, sizeof(int));

    // -------- SECOND PASS --------
    // count degree of each node (undirected: u->v and v->u)
    rewind(file);
    int num_undirected_edges = 0;

    while (fgets(line, sizeof(line), file)) {

        if (line[0] == '#') continue;

        sscanf(line, "%d %d", &u, &v);
        if (u == v) {
            degree[u]++;
            num_undirected_edges++;
        } else {
            degree[u]++;
            degree[v]++;
            num_undirected_edges += 2;
        }
    }

    // -------- BUILD ROW_PTR --------
    int *row_ptr = malloc((num_nodes + 1) * sizeof(int));
    row_ptr[0] = 0;

    for (int i = 0; i < num_nodes; i++) {
        row_ptr[i + 1] = row_ptr[i] + degree[i];
    }

    // -------- BUILD COL_IND --------
    int *col_ind = malloc(num_undirected_edges * sizeof(int));

    // temporary copy to track insert positions
    int *current = malloc(num_nodes * sizeof(int));
    for (int i = 0; i < num_nodes; i++)
        current[i] = row_ptr[i];

    rewind(file);

    while (fgets(line, sizeof(line), file)) {

        if (line[0] == '#') continue;

        sscanf(line, "%d %d", &u, &v);

        if (u == v) {
            int pos = current[u]++;
            col_ind[pos] = v;
        } else {
            int pos_u = current[u]++;
            col_ind[pos_u] = v;

            int pos_v = current[v]++;
            col_ind[pos_v] = u;
        }
    }

    fclose(file);

    printf("CSR construction (Undirected) complete.\n");

    // -------- OPTIONAL: TEST PRINT --------
    printf("\nNeighbors of node 0:\n");
    for (int i = row_ptr[0]; i < row_ptr[1]; i++) {
        printf("%d ", col_ind[i]);
    }
    printf("\n");

    printf("\nNeighbors of node 1:\n");
    for (int i = row_ptr[1]; i < row_ptr[2]; i++) {
        printf("%d ", col_ind[i]);
    }
    printf("\n");

    // -------- WRITE CSR TO BINARY FILE --------
    FILE *out = fopen("outputs/csr_format_web-google", "wb");
    if (out == NULL) {
        printf("Error: Could not open output file for writing\n");
        free(degree);
        free(row_ptr);
        free(col_ind);
        free(current);
        return 1;
    }

    fwrite(&num_nodes, sizeof(int), 1, out);
    fwrite(&num_undirected_edges, sizeof(int), 1, out);
    fwrite(row_ptr, sizeof(int), num_nodes + 1, out);
    fwrite(col_ind, sizeof(int), num_undirected_edges, out);
    fclose(out);

    printf("\nCSR binary written to outputs/csr_format_web-google\n");
    printf("  num_nodes = %d\n", num_nodes);
    printf("  num_edges (undirected) = %d\n", num_undirected_edges);
    printf("  row_ptr size = %d ints\n", num_nodes + 1);
    printf("  col_ind size = %d ints\n", num_undirected_edges);

    // -------- FREE MEMORY --------
    free(degree);
    free(row_ptr);
    free(col_ind);
    free(current);

    return 0;
}