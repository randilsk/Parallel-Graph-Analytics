/*
 * convert_to_csr.c — Convert edge-list text file to CSR binary format
 *
 * Build:
 *   gcc -O3 -Wall -o convert_to_csr convert_to_csr.c
 *
 * Run:
 *   ./convert_to_csr data/web-Google.txt outputs/csr_format_web-google
 */

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input_edgelist.txt> <output.csr>\n", argv[0]);
        return 1;
    }

    const char *input_path  = argv[1];
    const char *output_path = argv[2];

    FILE *file = fopen(input_path, "r");
    if (!file) {
        fprintf(stderr, "Error: Could not open input file: %s\n", input_path);
        return 1;
    }

    int u, v;
    int max_node = -1;
    int num_edges = 0;
    char line[256];

    /* ---- First pass: find max node id and edge count ---- */
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#') continue;
        if (sscanf(line, "%d %d", &u, &v) != 2) continue;
        if (u > max_node) max_node = u;
        if (v > max_node) max_node = v;
        num_edges++;
    }

    if (max_node < 0 || num_edges == 0) {
        fprintf(stderr, "Error: No valid edges found in %s\n", input_path);
        fclose(file);
        return 1;
    }

    int num_nodes = max_node + 1;
    printf("Nodes : %d\n", num_nodes);
    printf("Edges : %d\n", num_edges);

    /* ---- Allocate degree array ---- */
    int *degree = calloc(num_nodes, sizeof(int));
    if (!degree) { fprintf(stderr, "Out of memory\n"); fclose(file); return 1; }

    /* ---- Second pass: compute out-degrees ---- */
    rewind(file);
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#') continue;
        if (sscanf(line, "%d %d", &u, &v) != 2) continue;
        degree[u]++;
    }

    /* ---- Build row_ptr ---- */
    int *row_ptr = malloc((num_nodes + 1) * sizeof(int));
    if (!row_ptr) { fprintf(stderr, "Out of memory\n"); fclose(file); return 1; }
    row_ptr[0] = 0;
    for (int i = 0; i < num_nodes; i++)
        row_ptr[i + 1] = row_ptr[i] + degree[i];

    /* ---- Build col_ind ---- */
    int *col_ind = malloc(num_edges * sizeof(int));
    int *current = malloc(num_nodes * sizeof(int));
    if (!col_ind || !current) { fprintf(stderr, "Out of memory\n"); fclose(file); return 1; }

    for (int i = 0; i < num_nodes; i++)
        current[i] = row_ptr[i];

    rewind(file);
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#') continue;
        if (sscanf(line, "%d %d", &u, &v) != 2) continue;
        col_ind[current[u]++] = v;
    }

    fclose(file);

    /* ---- Write CSR binary ---- */
    FILE *out = fopen(output_path, "wb");
    if (!out) {
        fprintf(stderr, "Error: Could not open output file: %s\n", output_path);
        free(degree); free(row_ptr); free(col_ind); free(current);
        return 1;
    }

    fwrite(&num_nodes, sizeof(int), 1,            out);
    fwrite(&num_edges, sizeof(int), 1,            out);
    fwrite(row_ptr,    sizeof(int), num_nodes + 1, out);
    fwrite(col_ind,    sizeof(int), num_edges,     out);
    fclose(out);

    printf("CSR binary written to: %s\n", output_path);
    printf("  row_ptr : %d ints\n", num_nodes + 1);
    printf("  col_ind : %d ints\n", num_edges);

    free(degree); free(row_ptr); free(col_ind); free(current);
    return 0;
}