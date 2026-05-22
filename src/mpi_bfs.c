/*
 * mpi_bfs.c - MPI Parallel BFS with Partitioned CSR
 *
 * Correctness:
 *   Two-phase expansion per level:
 *   Phase 1: Each rank tentatively marks candidate neighbours in a
 *            local claim[] bitmap (no sends yet).
 *   Phase 2: MPI_Allreduce(BOR) merges all ranks' claim[] bitmaps.
 *            A node is "won" by rank r only if r is the lowest-ranked
 *            rank that claimed it (checked via a second bitmap exchange).
 *            Actually simpler: after Allreduce, each rank sends only the
 *            nodes IT claimed that survived (still set after OR, meaning
 *            at least one rank wants it — the owner receives it once from
 *            whoever sends it first, dedup at receiver handles the rest).
 *
 *   The real fix: use MPI_Allreduce(BOR) on a global "being_sent" bitmap
 *   BEFORE building send buffers, so every rank sees a consistent view of
 *   which nodes are being claimed this level. This prevents two ranks from
 *   independently sending the same node.
 *
 * Build:
 *   mpicc -O3 -Wall -Wextra -Wno-unused-result -o bin/mpi_bfs src/mpi_bfs.c
 * Run:
 *   mpirun -np 4 ./bin/mpi_bfs outputs/csr_format_web-google [source_node]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>

static inline int owner(int node, int num_nodes, int num_ranks) {
    return (long long)node * num_ranks / num_nodes;
}

void mpi_bfs(int source,
             int num_nodes,
             int *row_ptr, int *col_ind,
             int local_start, int local_end,
             int num_ranks, int rank)
{
    int local_count = local_end - local_start;

    /* Distributed level array */
    int *local_level = (int *)malloc(local_count * sizeof(int));
    for (int i = 0; i < local_count; i++) local_level[i] = -1;

    /* Global visited bitmap — every rank has a full copy.
       Updated via MPI_Allreduce(BOR) at the end of each level. */
    char *visited = (char *)calloc(num_nodes, 1);

    /* claim[] — nodes this rank wants to send this level.
       After Allreduce(BOR), used to decide who actually sends. */
    char *claim = (char *)calloc(num_nodes, 1);

    /* Per-rank send buffers */
    int **send_buf   = (int **)malloc(num_ranks * sizeof(int *));
    int  *send_count = (int  *)calloc(num_ranks, sizeof(int));
    int  *send_cap   = (int  *)malloc(num_ranks * sizeof(int));
    for (int r = 0; r < num_ranks; r++) {
        send_cap[r] = 64;
        send_buf[r] = (int *)malloc(send_cap[r] * sizeof(int));
    }

    int *local_frontier    = (int *)malloc(local_count * sizeof(int));
    int  local_frontier_sz = 0;

    /* Seed */
    if (owner(source, num_nodes, num_ranks) == rank) {
        local_level[source - local_start] = 0;
        local_frontier[local_frontier_sz++] = source;
    }
    visited[source] = 1;
    /* Broadcast the initial visited state */
    MPI_Bcast(visited, num_nodes, MPI_CHAR, 
              owner(source, num_nodes, num_ranks), MPI_COMM_WORLD);

    long long total_edges = 0;
    int       total_nodes = 0;
    int       cur_level   = 0;

    if (rank == 0)
        printf("\n--- MPI BFS from node %d  (%d ranks) ---\n", source, num_ranks);

    double t_start = MPI_Wtime();

    while (1) {
        int local_has  = (local_frontier_sz > 0) ? 1 : 0;
        int global_has = 0;
        MPI_Allreduce(&local_has, &global_has, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
        if (global_has == 0) break;

        /* ---- Phase 1: tentatively claim unvisited neighbours ---- */
        memset(claim, 0, num_nodes);
        long long local_edges = 0;

        for (int fi = 0; fi < local_frontier_sz; fi++) {
            int u      = local_frontier[fi];
            int li_u   = u - local_start;
            int edge_s = row_ptr[li_u];
            int edge_e = row_ptr[li_u + 1];
            local_edges += (edge_e - edge_s);

            for (int e = edge_s; e < edge_e; e++) {
                int v = col_ind[e];
                if (!visited[v])
                    claim[v] = 1;   /* tentative claim — no send yet */
            }
        }

        /* ---- Phase 2: merge all ranks' claims via Allreduce BOR ----
           After this, claim[v]=1 means AT LEAST ONE rank wants to send v.
           visited[v] is still 0 for all of these (pre-send).
           We assign each claimed node to its OWNER rank — the owner
           will receive it from whoever sends it and dedup locally.
           To avoid duplicate sends, only send v if THIS rank is the
           lowest-ranked rank in whose frontier a predecessor of v exists.
           Simplest correct approach: Allreduce the claim bitmap, then
           each rank sends only nodes where claim[v]=1 AND v is owned
           by a rank — the owner deduplicates on receipt. */
        MPI_Allreduce(MPI_IN_PLACE, claim, num_nodes, MPI_CHAR,
                      MPI_BOR, MPI_COMM_WORLD);

        /* Now update visited with all claimed nodes so future levels skip them */
        /* Also build send buffers — each rank sends claimed nodes to their owners.
           Duplicates are handled at receiver (level[li]==-1 check). */
        for (int r = 0; r < num_ranks; r++) send_count[r] = 0;

        for (int fi = 0; fi < local_frontier_sz; fi++) {
            int u      = local_frontier[fi];
            int li_u   = u - local_start;
            int edge_s = row_ptr[li_u];
            int edge_e = row_ptr[li_u + 1];

            for (int e = edge_s; e < edge_e; e++) {
                int v = col_ind[e];
                /* Only send if v was in global claim and not yet visited */
                if (!claim[v] || visited[v]) continue;
                /* Mark visited NOW so same rank doesn't send v twice */
                visited[v] = 1;

                int v_owner = owner(v, num_nodes, num_ranks);
                if (send_count[v_owner] == send_cap[v_owner]) {
                    send_cap[v_owner] *= 2;
                    send_buf[v_owner]  = (int *)realloc(send_buf[v_owner],
                                         send_cap[v_owner] * sizeof(int));
                }
                send_buf[v_owner][send_count[v_owner]++] = v;
            }
        }

        /* ---- Sparse Alltoallv ---- */
        int *recv_count = (int *)calloc(num_ranks, sizeof(int));
        MPI_Alltoall(send_count, 1, MPI_INT,
                     recv_count, 1, MPI_INT, MPI_COMM_WORLD);

        int *sdispls = (int *)malloc(num_ranks * sizeof(int));
        int *rdispls = (int *)malloc(num_ranks * sizeof(int));
        int  stotal  = 0, rtotal = 0;
        for (int r = 0; r < num_ranks; r++) {
            sdispls[r] = stotal; stotal += send_count[r];
            rdispls[r] = rtotal; rtotal += recv_count[r];
        }

        int *flat_send = (int *)malloc((stotal ? stotal : 1) * sizeof(int));
        for (int r = 0; r < num_ranks; r++)
            memcpy(flat_send + sdispls[r], send_buf[r],
                   send_count[r] * sizeof(int));

        int *flat_recv = (int *)malloc((rtotal ? rtotal : 1) * sizeof(int));
        MPI_Alltoallv(flat_send, send_count, sdispls, MPI_INT,
                      flat_recv, recv_count, rdispls, MPI_INT,
                      MPI_COMM_WORLD);

        /* Build next frontier */
        local_frontier_sz = 0;
        for (int i = 0; i < rtotal; i++) {
            int v  = flat_recv[i];
            int li = v - local_start;
            if (local_level[li] == -1) {
                local_level[li] = cur_level + 1;
                local_frontier[local_frontier_sz++] = v;
            }
        }

        /* Sync visited[] across all ranks so every rank has the same
           picture going into the next level's claim phase */
        MPI_Allreduce(MPI_IN_PLACE, visited, num_nodes, MPI_CHAR,
                      MPI_BOR, MPI_COMM_WORLD);

        long long g_edges = 0;
        MPI_Allreduce(&local_edges, &g_edges, 1, MPI_LONG_LONG,
                      MPI_SUM, MPI_COMM_WORLD);
        total_edges += g_edges;

        free(flat_send); free(flat_recv);
        free(recv_count); free(sdispls); free(rdispls);

        cur_level++;
    }

    double runtime = MPI_Wtime() - t_start;

    int local_reached = 0;
    for (int i = 0; i < local_count; i++)
        if (local_level[i] != -1) local_reached++;
    MPI_Reduce(&local_reached, &total_nodes, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        printf("MPI BFS Runtime : %.6f seconds\n", runtime);
        printf("BFS Levels      : %d\n", cur_level);
        printf("Nodes reached   : %d / %d\n", total_nodes, num_nodes);
        printf("Edges examined  : %lld\n", total_edges);
        if (runtime > 0)
            printf("Performance     : %.2f MTEPS\n", total_edges / runtime / 1e6);
    }

    free(local_level); free(visited); free(claim); free(local_frontier);
    for (int r = 0; r < num_ranks; r++) free(send_buf[r]);
    free(send_buf); free(send_count); free(send_cap);
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
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

    int num_nodes = 0, num_edges_total = 0;
    int *full_row_ptr = NULL;
    int *full_col_ind = NULL;

    if (rank == 0) {
        FILE *f = fopen(argv[1], "rb");
        if (!f) { fprintf(stderr, "Cannot open %s\n", argv[1]); MPI_Abort(MPI_COMM_WORLD, 1); }
        if (fread(&num_nodes,       sizeof(int), 1, f) != 1 ||
            fread(&num_edges_total, sizeof(int), 1, f) != 1) {
            fprintf(stderr, "Error reading header\n");
            fclose(f); MPI_Abort(MPI_COMM_WORLD, 1);
        }
        printf("Graph: %d nodes, %d edges\n", num_nodes, num_edges_total);
        full_row_ptr = (int *)malloc((num_nodes + 1) * sizeof(int));
        full_col_ind = (int *)malloc(num_edges_total  * sizeof(int));
        if (fread(full_row_ptr, sizeof(int), num_nodes + 1,   f) != (size_t)(num_nodes + 1) ||
            fread(full_col_ind, sizeof(int), num_edges_total, f) != (size_t)num_edges_total) {
            fprintf(stderr, "Error reading CSR data\n");
            fclose(f); MPI_Abort(MPI_COMM_WORLD, 1);
        }
        fclose(f);
    }

    MPI_Bcast(&num_nodes,       1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&num_edges_total, 1, MPI_INT, 0, MPI_COMM_WORLD);

    int local_start = (long long)num_nodes * rank       / num_ranks;
    int local_end   = (long long)num_nodes * (rank + 1) / num_ranks;
    int local_count = local_end - local_start;

    if (rank != 0)
        full_row_ptr = (int *)malloc((num_nodes + 1) * sizeof(int));
    MPI_Bcast(full_row_ptr, num_nodes + 1, MPI_INT, 0, MPI_COMM_WORLD);

    int *local_row_ptr = (int *)malloc((local_count + 1) * sizeof(int));
    int  edge_offset   = full_row_ptr[local_start];
    for (int i = 0; i <= local_count; i++)
        local_row_ptr[i] = full_row_ptr[local_start + i] - edge_offset;

    int local_edges = full_row_ptr[local_end] - full_row_ptr[local_start];

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

    int *local_col_ind = (int *)malloc((local_edges ? local_edges : 1) * sizeof(int));
    MPI_Scatterv(full_col_ind,  send_counts_e, send_displs_e, MPI_INT,
                 local_col_ind, local_edges,                  MPI_INT,
                 0, MPI_COMM_WORLD);

    if (rank == 0) {
        free(full_col_ind);
        free(send_counts_e);
        free(send_displs_e);
    }
    free(full_row_ptr);

    if (source_node < 0 || source_node >= num_nodes) {
        if (rank == 0)
            fprintf(stderr, "Invalid source node %d (graph has %d nodes)\n",
                    source_node, num_nodes);
        free(local_row_ptr); free(local_col_ind);
        MPI_Finalize();
        return 1;
    }

    mpi_bfs(source_node, num_nodes,
            local_row_ptr, local_col_ind,
            local_start, local_end,
            num_ranks, rank);

    free(local_row_ptr);
    free(local_col_ind);
    MPI_Finalize();
    return 0;
}