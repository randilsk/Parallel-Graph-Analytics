/*
 * mpi_bfs.c — MPI Parallel BFS with Partitioned CSR
 *
 * Design:
 *   - Each rank owns a contiguous range of vertices (1D row partition).
 *   - Only the local rows of row_ptr and col_ind are stored per rank.
 *   - Frontier is sparse: each rank tracks only locally-owned frontier nodes.
 *   - Each level, ranks exchange only newly discovered nodes via
 *     MPI_Alltoallv (sparse), not a full level[] Allreduce.
 *   - level[] array is distributed: rank r stores level[local_start..local_end).
 *
 * Build:
 *   mpicc -O3 -o mpi_bfs mpi_bfs.c
 *
 * Run:
 *   mpirun -np 4 ./mpi_bfs outputs/csr_format_web-google [source_node]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>

/* ------------------------------------------------------------------ */
/* Utility                                                              */
/* ------------------------------------------------------------------ */

/* Owner rank for a given global node id */
static inline int owner(int node, int num_nodes, int num_ranks) {
    return (long long)node * num_ranks / num_nodes;
}

/* Local index of a global node on its owner rank */
static inline int local_idx(int node, int num_nodes, int num_ranks) {
    int r = owner(node, num_nodes, num_ranks);
    int local_start = (long long)num_nodes * r / num_ranks;
    return node - local_start;
}

/* ------------------------------------------------------------------ */
/* MPI BFS — partitioned CSR, sparse frontier exchange                 */
/* ------------------------------------------------------------------ */
void mpi_bfs(int source,
             int num_nodes, int num_edges_local,
             int *row_ptr,   /* size: local_count + 1 */
             int *col_ind,   /* size: num_edges_local  */
             int local_start, int local_end, /* [local_start, local_end) */
             int num_ranks, int rank)
{
    int local_count = local_end - local_start;

    /* Local level array — only for owned vertices; -1 = unvisited */
    int *level = (int *)malloc(local_count * sizeof(int));
    for (int i = 0; i < local_count; i++) level[i] = -1;

    /* Per-rank send buffers for frontier exchange */
    int **send_buf   = (int **)malloc(num_ranks * sizeof(int *));
    int  *send_count = (int  *)calloc(num_ranks, sizeof(int));
    int  *send_cap   = (int  *)malloc(num_ranks * sizeof(int));
    for (int r = 0; r < num_ranks; r++) {
        send_cap[r] = 64;
        send_buf[r] = (int *)malloc(send_cap[r] * sizeof(int));
    }

    /* Current local frontier (global node ids owned by this rank) */
    int *local_frontier     = (int *)malloc(local_count * sizeof(int));
    int  local_frontier_sz  = 0;

    /* Seed: rank that owns source initialises it */
    if (owner(source, num_nodes, num_ranks) == rank) {
        int li = local_idx(source, num_nodes, num_ranks);
        level[li] = 0;
        local_frontier[local_frontier_sz++] = source;
    }

    long long total_edges = 0;
    int       total_nodes = 0;
    int       cur_level   = 0;

    if (rank == 0)
        printf("\n--- MPI BFS from node %d  (%d ranks) ---\n", source, num_ranks);

    double t_start = MPI_Wtime();

    /* ---- Level-synchronous BFS ---- */
    while (1) {
        /* Check globally whether any rank has frontier nodes */
        int local_has = (local_frontier_sz > 0) ? 1 : 0;
        int global_has = 0;
        MPI_Allreduce(&local_has, &global_has, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
        if (global_has == 0) break;

        /* Reset send buffers */
        for (int r = 0; r < num_ranks; r++) send_count[r] = 0;

        long long local_edges = 0;

        /* Expand local frontier: for each owned frontier node, inspect neighbours */
        for (int fi = 0; fi < local_frontier_sz; fi++) {
            int u        = local_frontier[fi];
            int li_u     = u - local_start;          /* local row index */
            int edge_s   = row_ptr[li_u];
            int edge_e   = row_ptr[li_u + 1];
            local_edges += (edge_e - edge_s);

            for (int e = edge_s; e < edge_e; e++) {
                int v       = col_ind[e];
                int v_owner = owner(v, num_nodes, num_ranks);

                /* Add v to the outgoing send buffer for v's owner.
                   We send every candidate; the owner decides if it is new. */
                if (send_count[v_owner] == send_cap[v_owner]) {
                    send_cap[v_owner] *= 2;
                    send_buf[v_owner]  = (int *)realloc(send_buf[v_owner],
                                         send_cap[v_owner] * sizeof(int));
                }
                send_buf[v_owner][send_count[v_owner]++] = v;
            }
        }

        /* ---- Sparse all-to-all exchange ---- */
        /* Step 1: tell every rank how many nodes we're sending it */
        int *recv_count = (int *)calloc(num_ranks, sizeof(int));
        MPI_Alltoall(send_count, 1, MPI_INT,
                     recv_count, 1, MPI_INT, MPI_COMM_WORLD);

        /* Step 2: build flat send buffer and displacements */
        int *sdispls = (int *)malloc(num_ranks * sizeof(int));
        int *rdispls = (int *)malloc(num_ranks * sizeof(int));
        int  stotal  = 0, rtotal = 0;
        for (int r = 0; r < num_ranks; r++) {
            sdispls[r] = stotal; stotal += send_count[r];
            rdispls[r] = rtotal; rtotal += recv_count[r];
        }

        int *flat_send = (int *)malloc(stotal * sizeof(int));
        for (int r = 0; r < num_ranks; r++)
            memcpy(flat_send + sdispls[r], send_buf[r],
                   send_count[r] * sizeof(int));

        int *flat_recv = (int *)malloc(rtotal * sizeof(int));
        MPI_Alltoallv(flat_send, send_count, sdispls, MPI_INT,
                      flat_recv, recv_count, rdispls, MPI_INT,
                      MPI_COMM_WORLD);

        /* Step 3: process received nodes — mark unvisited ones */
        local_frontier_sz = 0;
        for (int i = 0; i < rtotal; i++) {
            int v  = flat_recv[i];
            int li = v - local_start;   /* always owned by this rank */
            if (level[li] == -1) {
                level[li] = cur_level + 1;
                local_frontier[local_frontier_sz++] = v;
            }
        }

        /* Accumulate edge/node stats */
        long long g_edges = 0;
        MPI_Allreduce(&local_edges, &g_edges, 1, MPI_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);
        total_edges += g_edges;

        free(flat_send); free(flat_recv);
        free(recv_count); free(sdispls); free(rdispls);

        cur_level++;
    }

    double runtime = MPI_Wtime() - t_start;

    /* Count reached nodes (local, then reduce) */
    int local_reached = 0;
    for (int i = 0; i < local_count; i++)
        if (level[i] != -1) local_reached++;
    MPI_Reduce(&local_reached, &total_nodes, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        printf("MPI BFS Runtime : %.6f seconds\n", runtime);
        printf("BFS Levels      : %d\n", cur_level);
        printf("Nodes reached   : %d / %d\n", total_nodes, num_nodes);
        printf("Edges examined  : %lld\n", total_edges);
        if (runtime > 0)
            printf("Performance     : %.2f MTEPS\n", total_edges / runtime / 1e6);
    }

    /* Cleanup */
    free(level);
    free(local_frontier);
    for (int r = 0; r < num_ranks; r++) free(send_buf[r]);
    free(send_buf); free(send_count); free(send_cap);
}

/* ------------------------------------------------------------------ */
/* Main: read CSR, scatter rows, run BFS                               */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);

    int rank, num_ranks;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &num_ranks);

    if (argc < 2) {
        if (rank == 0)
            fprintf(stderr, "Usage: %s <graph.csr> [source_node]\n", argv[0]);
        MPI_Finalize();
        return 1;
    }

    int source_node = (argc >= 3) ? atoi(argv[2]) : 0;

    /* ---- Read header on rank 0, broadcast ---- */
    int num_nodes = 0, num_edges_total = 0;
    int *full_row_ptr = NULL;
    int *full_col_ind = NULL;

    if (rank == 0) {
        FILE *f = fopen(argv[1], "rb");
        if (!f) { fprintf(stderr, "Cannot open %s\n", argv[1]); MPI_Abort(MPI_COMM_WORLD, 1); }

        if (fread(&num_nodes,       sizeof(int), 1, f) != 1 ||
            fread(&num_edges_total, sizeof(int), 1, f) != 1) {
            fprintf(stderr, "Error reading CSR header\n");
            fclose(f); MPI_Abort(MPI_COMM_WORLD, 1);
        }
        printf("Graph: %d nodes, %d edges\n", num_nodes, num_edges_total);

        full_row_ptr = (int *)malloc((num_nodes + 1) * sizeof(int));
        full_col_ind = (int *)malloc(num_edges_total  * sizeof(int));

        if (fread(full_row_ptr, sizeof(int), num_nodes + 1,    f) != (size_t)(num_nodes + 1) ||
            fread(full_col_ind, sizeof(int), num_edges_total, f) != (size_t)num_edges_total) {
            fprintf(stderr, "Error reading CSR data\n");
            fclose(f); MPI_Abort(MPI_COMM_WORLD, 1);
        }
        fclose(f);
    }

    MPI_Bcast(&num_nodes,       1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&num_edges_total, 1, MPI_INT, 0, MPI_COMM_WORLD);

    /* ---- Compute partition boundaries ---- */
    int local_start = (long long)num_nodes * rank       / num_ranks;
    int local_end   = (long long)num_nodes * (rank + 1) / num_ranks;
    int local_count = local_end - local_start;

    /* ---- Scatter row_ptr ---- */
    /*
     * Each rank needs row_ptr[local_start .. local_end] (local_count+1 values).
     * We use MPI_Scatterv with counts = local_count+1 per rank except the
     * last element of each block overlaps with the first of the next, so we
     * broadcast the full row_ptr (it's only (N+1)*4 bytes ≈ 3 MB for web-Google).
     * For truly huge graphs replace with Scatterv; for this project size Bcast is fine.
     */
    if (rank != 0)
        full_row_ptr = (int *)malloc((num_nodes + 1) * sizeof(int));
    MPI_Bcast(full_row_ptr, num_nodes + 1, MPI_INT, 0, MPI_COMM_WORLD);

    /* Local row_ptr: re-based to 0 */
    int *local_row_ptr = (int *)malloc((local_count + 1) * sizeof(int));
    int  edge_offset   = full_row_ptr[local_start];
    for (int i = 0; i <= local_count; i++)
        local_row_ptr[i] = full_row_ptr[local_start + i] - edge_offset;

    int local_edges = full_row_ptr[local_end] - full_row_ptr[local_start];

    /* ---- Scatter col_ind (only local edges) via Scatterv ---- */
    int *send_counts_e = NULL;
    int *send_displs_e = NULL;
    if (rank == 0) {
        send_counts_e = (int *)malloc(num_ranks * sizeof(int));
        send_displs_e = (int *)malloc(num_ranks * sizeof(int));
        for (int r = 0; r < num_ranks; r++) {
            int rs = (long long)num_nodes * r       / num_ranks;
            int re = (long long)num_nodes * (r + 1) / num_ranks;
            send_counts_e[r] = full_row_ptr[re] - full_row_ptr[rs];
            send_displs_e[r] = full_row_ptr[rs];
        }
    }

    int *local_col_ind = (int *)malloc(local_edges * sizeof(int));
    MPI_Scatterv(full_col_ind,   send_counts_e, send_displs_e, MPI_INT,
                 local_col_ind, local_edges,                   MPI_INT,
                 0, MPI_COMM_WORLD);

    /* Rank 0 can free full arrays now */
    if (rank == 0) {
        free(full_col_ind);
        free(send_counts_e);
        free(send_displs_e);
    }
    free(full_row_ptr);   /* all ranks have this copy */

    /* Validate source node */
    if (source_node < 0 || source_node >= num_nodes) {
        if (rank == 0)
            fprintf(stderr, "Invalid source node %d (graph has %d nodes)\n",
                    source_node, num_nodes);
        MPI_Finalize();
        return 1;
    }

    /* ---- Run BFS ---- */
    mpi_bfs(source_node,
            num_nodes, local_edges,
            local_row_ptr, local_col_ind,
            local_start, local_end,
            num_ranks, rank);

    free(local_row_ptr);
    free(local_col_ind);

    MPI_Finalize();
    return 0;
}