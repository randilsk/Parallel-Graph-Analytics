/*
 * mpi.c - MPI Parallel Graph Analytics Suite (BFS & CC)
 *
 * Fully integrated to support both Breadth-First Search (BFS) and
 * Connected Components (CC) using MPI distributed memory paradigms.
 * Includes serial baselines, correctness validation, and speedup analytics.
 *
 * Build:
 *   mpicc -O3 -Wall -o bin/mpi_analysis src/mpi/mpi.c
 * Run:
 *   mpirun -np 4 ./bin/mpi_analysis outputs/csr_format_web-google [source_node]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>

// Helper to determine which rank owns a node
static inline int owner(int node, int num_nodes, int num_ranks) {
    return (long long)node * num_ranks / num_nodes;
}

// -------------------------------------------------------------------------
// Disjoint-Set Union (DSU) Helpers for MPI CC
// -------------------------------------------------------------------------
static inline int dsu_find(int *parent, int i) {
    int root = i;
    while (parent[root] != root) {
        root = parent[root];
    }
    // Path compression
    int curr = i;
    while (parent[curr] != root) {
        int next = parent[curr];
        parent[curr] = root;
        curr = next;
    }
    return root;
}

static inline int dsu_union(int *parent, int i, int j) {
    int root_i = dsu_find(parent, i);
    int root_j = dsu_find(parent, j);
    if (root_i != root_j) {
        parent[root_i] = root_j;
        return 1; // Merged
    }
    return 0; // Already in same component
}

// -------------------------------------------------------------------------
// CPU Serial BFS Baseline
// -------------------------------------------------------------------------
int* run_serial_bfs(int source, int num_nodes, const int *row_ptr, const int *col_ind, 
                    double *cpu_time, long long *cpu_edges, int *cpu_nodes_reached) {
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

    *cpu_edges = 0;

    double start_time = MPI_Wtime();
    while (front < rear) {
        int current_node = queue[front++];
        int start_edge = row_ptr[current_node];
        int end_edge = row_ptr[current_node + 1];

        for (int i = start_edge; i < end_edge; i++) {
            int neighbor = col_ind[i];
            (*cpu_edges)++;

            if (level[neighbor] == -1) {
                level[neighbor] = level[current_node] + 1;
                parent[neighbor] = current_node;
                queue[rear++] = neighbor;
            }
        }
    }
    double end_time = MPI_Wtime();
    *cpu_time = end_time - start_time;
    *cpu_nodes_reached = rear;

    free(parent);
    free(queue);
    return level;
}

// -------------------------------------------------------------------------
// CPU Serial Connected Components Baseline
// -------------------------------------------------------------------------
int run_serial_cc(int num_nodes, const int *row_ptr, const int *col_ind, double *cpu_time) {
    int *components = (int *)malloc(num_nodes * sizeof(int));
    for (int i = 0; i < num_nodes; i++) {
        components[i] = -1;
    }

    int *queue = (int *)malloc(num_nodes * sizeof(int));
    int num_components = 0;

    double start_time = MPI_Wtime();
    for (int i = 0; i < num_nodes; i++) {
        if (components[i] == -1) {
            int current_component = num_components++;
            int front = 0, rear = 0;

            components[i] = current_component;
            queue[rear++] = i;

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
    double end_time = MPI_Wtime();
    *cpu_time = end_time - start_time;

    free(components);
    free(queue);
    return num_components;
}

// -------------------------------------------------------------------------
// MPI Parallel BFS
// -------------------------------------------------------------------------
int* mpi_bfs(int source,
             int num_nodes,
             const int *row_ptr, const int *col_ind,
             int local_start, int local_end,
             int num_ranks, int rank,
             double *mpi_time, int *mpi_levels, long long *mpi_edges)
{
    int local_count = local_end - local_start;

    int *local_level = (int *)malloc(local_count * sizeof(int));
    for (int i = 0; i < local_count; i++) local_level[i] = -1;

    char *visited = (char *)calloc(num_nodes, 1);
    char *claim = (char *)calloc(num_nodes, 1);

    int **send_buf   = (int **)malloc(num_ranks * sizeof(int *));
    int  *send_count = (int  *)calloc(num_ranks, sizeof(int));
    int  *send_cap   = (int  *)malloc(num_ranks * sizeof(int));
    for (int r = 0; r < num_ranks; r++) {
        send_cap[r] = 64;
        send_buf[r] = (int *)malloc(send_cap[r] * sizeof(int));
    }

    int *local_frontier    = (int *)malloc(local_count * sizeof(int));
    int  local_frontier_sz = 0;

    if (owner(source, num_nodes, num_ranks) == rank) {
        local_level[source - local_start] = 0;
        local_frontier[local_frontier_sz++] = source;
    }
    visited[source] = 1;
    
    MPI_Bcast(visited, num_nodes, MPI_CHAR, 
              owner(source, num_nodes, num_ranks), MPI_COMM_WORLD);

    long long total_edges = 0;
    int       cur_level   = 0;

    double t_start = MPI_Wtime();

    while (1) {
        int local_has  = (local_frontier_sz > 0) ? 1 : 0;
        int global_has = 0;
        MPI_Allreduce(&local_has, &global_has, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
        if (global_has == 0) break;

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
                    claim[v] = 1;
            }
        }

        MPI_Allreduce(MPI_IN_PLACE, claim, num_nodes, MPI_CHAR,
                      MPI_BOR, MPI_COMM_WORLD);

        for (int r = 0; r < num_ranks; r++) send_count[r] = 0;

        for (int fi = 0; fi < local_frontier_sz; fi++) {
            int u      = local_frontier[fi];
            int li_u   = u - local_start;
            int edge_s = row_ptr[li_u];
            int edge_e = row_ptr[li_u + 1];

            for (int e = edge_s; e < edge_e; e++) {
                int v = col_ind[e];
                if (!claim[v] || visited[v]) continue;
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

        local_frontier_sz = 0;
        for (int i = 0; i < rtotal; i++) {
            int v  = flat_recv[i];
            int li = v - local_start;
            if (local_level[li] == -1) {
                local_level[li] = cur_level + 1;
                local_frontier[local_frontier_sz++] = v;
            }
        }

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

    *mpi_time = MPI_Wtime() - t_start;
    *mpi_levels = cur_level;
    *mpi_edges = total_edges;

    free(visited); free(claim); free(local_frontier);
    for (int r = 0; r < num_ranks; r++) free(send_buf[r]);
    free(send_buf); free(send_count); free(send_cap);

    return local_level;
}

// -------------------------------------------------------------------------
// MPI Connected Components via Hierarchical Merge Reduction Tree
// -------------------------------------------------------------------------
int mpi_cc(int num_nodes, const int *row_ptr, const int *col_ind,
           int local_start, int local_end,
           int num_ranks, int rank, double *mpi_time, int *mpi_iterations) {
    
    double start_time = MPI_Wtime();

    int *parent = (int *)malloc(num_nodes * sizeof(int));
    for (int i = 0; i < num_nodes; i++) parent[i] = i;

    // Stores union modifications to send to parent in reduction tree
    int *merge_edges = (int *)malloc(2 * num_nodes * sizeof(int));
    int num_merges = 0;

    int local_count = local_end - local_start;

    // 1. Local Spanning Forest (Build local connected subsets)
    for (int u = 0; u < local_count; u++) {
        int global_u = local_start + u;
        int start_edge = row_ptr[u];
        int end_edge = row_ptr[u + 1];
        for (int e = start_edge; e < end_edge; e++) {
            int v = col_ind[e];
            if (dsu_union(parent, global_u, v)) {
                merge_edges[2 * num_merges] = global_u;
                merge_edges[2 * num_merges + 1] = v;
                num_merges++;
            }
        }
    }

    // 2. Binary Tree Hierarchical Reduction Merge
    int step = 1;
    int iterations = 0;
    while (step < num_ranks) {
        iterations++;
        if (rank % (2 * step) == 0) {
            int src = rank + step;
            if (src < num_ranks) {
                int recv_merges = 0;
                MPI_Recv(&recv_merges, 1, MPI_INT, src, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                if (recv_merges > 0) {
                    int *recv_buf = (int *)malloc(2 * recv_merges * sizeof(int));
                    MPI_Recv(recv_buf, 2 * recv_merges, MPI_INT, src, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    
                    for (int k = 0; k < recv_merges; k++) {
                        int u = recv_buf[2 * k];
                        int v = recv_buf[2 * k + 1];
                        if (dsu_union(parent, u, v)) {
                            merge_edges[2 * num_merges] = u;
                            merge_edges[2 * num_merges + 1] = v;
                            num_merges++;
                        }
                    }
                    free(recv_buf);
                }
            }
        } else {
            // Send our DSU merges to our partner rank and exit the reduction
            int dest = rank - step;
            MPI_Send(&num_merges, 1, MPI_INT, dest, 0, MPI_COMM_WORLD);
            if (num_merges > 0) {
                MPI_Send(merge_edges, 2 * num_merges, MPI_INT, dest, 1, MPI_COMM_WORLD);
            }
            break;
        }
        step *= 2;
    }

    double end_time = MPI_Wtime();
    *mpi_time = end_time - start_time;
    *mpi_iterations = iterations;

    int global_components = 0;
    if (rank == 0) {
        for (int i = 0; i < num_nodes; i++) {
            if (parent[i] == i) {
                global_components++;
            }
        }
    }

    free(parent);
    free(merge_edges);

    return global_components;
}

// -------------------------------------------------------------------------
// Main Function
// -------------------------------------------------------------------------
int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);

    int rank, num_ranks;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &num_ranks);

    if (argc < 2) {
        if (rank == 0) {
            fprintf(stderr, "Usage: %s <graph.csr> [source_node]\n", argv[0]);
        }
        MPI_Finalize();
        return 1;
    }

    int source_node = (argc >= 3) ? atoi(argv[2]) : 0;

    int num_nodes = 0, num_edges_total = 0;
    int *full_row_ptr = NULL;
    int *full_col_ind = NULL;
    
    // BFS Baseline Variables
    int *cpu_level = NULL;
    double cpu_time = 0.0;
    long long cpu_edges = 0;
    int cpu_nodes_reached = 0;

    // CC Baseline Variables
    double cpu_cc_time = 0.0;
    int cpu_components = 0;

    // A. CPU Serial Baselines (Executed solely on Rank 0)
    if (rank == 0) {
        FILE *f = fopen(argv[1], "rb");
        if (!f) {
            fprintf(stderr, "Error: Cannot open CSR binary file %s\n", argv[1]);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        if (fread(&num_nodes,       sizeof(int), 1, f) != 1 ||
            fread(&num_edges_total, sizeof(int), 1, f) != 1) {
            fprintf(stderr, "Error reading header from %s\n", argv[1]);
            fclose(f); MPI_Abort(MPI_COMM_WORLD, 1);
        }

        full_row_ptr = (int *)malloc((num_nodes + 1) * sizeof(int));
        full_col_ind = (int *)malloc(num_edges_total  * sizeof(int));
        if (fread(full_row_ptr, sizeof(int), num_nodes + 1,   f) != (size_t)(num_nodes + 1) ||
            fread(full_col_ind, sizeof(int), num_edges_total, f) != (size_t)num_edges_total) {
            fprintf(stderr, "Error reading CSR structure data\n");
            fclose(f); MPI_Abort(MPI_COMM_WORLD, 1);
        }
        fclose(f);

        printf("[CPU] Running Serial BFS baseline... ");
        fflush(stdout);
        cpu_level = run_serial_bfs(source_node, num_nodes, full_row_ptr, full_col_ind,
                                   &cpu_time, &cpu_edges, &cpu_nodes_reached);
        printf("Done.\n");

        printf("[CPU] Running Serial Connected Components... ");
        fflush(stdout);
        cpu_components = run_serial_cc(num_nodes, full_row_ptr, full_col_ind, &cpu_cc_time);
        printf("Done.\n");
    }

    // Broadcast graph parameters to other workers
    MPI_Bcast(&num_nodes,       1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&num_edges_total, 1, MPI_INT, 0, MPI_COMM_WORLD);

    // Compute partitions
    int local_start = (long long)num_nodes * rank       / num_ranks;
    int local_end   = (long long)num_nodes * (rank + 1) / num_ranks;
    int local_count = local_end - local_start;

    if (rank != 0) {
        full_row_ptr = (int *)malloc((num_nodes + 1) * sizeof(int));
    }
    MPI_Bcast(full_row_ptr, num_nodes + 1, MPI_INT, 0, MPI_COMM_WORLD);

    int *local_row_ptr = (int *)malloc((local_count + 1) * sizeof(int));
    int  edge_offset   = full_row_ptr[local_start];
    for (int i = 0; i <= local_count; i++) {
        local_row_ptr[i] = full_row_ptr[local_start + i] - edge_offset;
    }

    int local_edges = full_row_ptr[local_end] - full_row_ptr[local_start];

    // Scatter the edge targets
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

    // Free loaded global arrays on Rank 0 to reclaim memory
    if (rank == 0) {
        free(full_col_ind);
        free(send_counts_e);
        free(send_displs_e);
    }
    free(full_row_ptr);

    if (source_node < 0 || source_node >= num_nodes) {
        if (rank == 0) {
            fprintf(stderr, "Invalid source node %d (graph has %d nodes)\n",
                    source_node, num_nodes);
        }
        free(local_row_ptr); free(local_col_ind);
        if (rank == 0) free(cpu_level);
        MPI_Finalize();
        return 1;
    }

    // B. MPI Distributed Parallel BFS
    double mpi_time = 0.0;
    int mpi_levels = 0;
    long long mpi_edges = 0;

    if (rank == 0) {
        printf("[MPI] Running MPI Parallel BFS... ");
        fflush(stdout);
    }

    int *local_level = mpi_bfs(source_node, num_nodes,
                               local_row_ptr, local_col_ind,
                               local_start, local_end,
                               num_ranks, rank,
                               &mpi_time, &mpi_levels, &mpi_edges);

    if (rank == 0) {
        printf("Done.\n");
    }

    // C. MPI Distributed Connected Components
    if (rank == 0) {
        printf("[MPI] Running MPI Parallel CC... ");
        fflush(stdout);
    }

    double mpi_cc_time = 0.0;
    int mpi_cc_iterations = 0;
    int mpi_components = mpi_cc(num_nodes, local_row_ptr, local_col_ind,
                                 local_start, local_end,
                                 num_ranks, rank,
                                 &mpi_cc_time, &mpi_cc_iterations);

    if (rank == 0) {
        printf("Done.\n");
    }

    // D. BFS Levels Gathering & Correctness Verification on Rank 0
    int *recvcounts = NULL;
    int *displs = NULL;
    int *global_mpi_level = NULL;

    if (rank == 0) {
        recvcounts = (int *)malloc(num_ranks * sizeof(int));
        displs = (int *)malloc(num_ranks * sizeof(int));
        for (int r = 0; r < num_ranks; r++) {
            int rs = (long long)num_nodes * r       / num_ranks;
            int re = (long long)num_nodes * (r + 1) / num_ranks;
            recvcounts[r] = re - rs;
            displs[r] = rs;
        }
        global_mpi_level = (int *)malloc(num_nodes * sizeof(int));
    }

    MPI_Gatherv(local_level, local_count, MPI_INT,
                global_mpi_level, recvcounts, displs, MPI_INT,
                0, MPI_COMM_WORLD);

    if (rank == 0) {
        // Correctness check for BFS
        int bfs_correct = 1;
        int mpi_nodes_reached = 0;
        for (int i = 0; i < num_nodes; i++) {
            if (global_mpi_level[i] != -1) {
                mpi_nodes_reached++;
            }
            if (cpu_level[i] != global_mpi_level[i]) {
                bfs_correct = 0;
                if (i < 10) {
                    printf("  Mismatch at vertex %d: CPU Level = %d, MPI Level = %d\n", 
                           i, cpu_level[i], global_mpi_level[i]);
                }
            }
        }

        double cpu_mteps = (cpu_time > 0) ? (cpu_edges / cpu_time) / 1e6 : 0.0;
        double mpi_mteps = (mpi_time > 0) ? (mpi_edges / mpi_time) / 1e6 : 0.0;

        printf("\n=========================================================================\n");
        printf("   MPI Distributed-Memory Parallel Graph Analytics Suite\n");
        printf("=========================================================================\n");
        printf("Graph Dataset  : %s\n", argv[1]);
        printf("Number of Nodes: %d\n", num_nodes);
        printf("Number of Edges: %d\n", num_edges_total);
        printf("Ranks / Workers: %d\n", num_ranks);
        printf("Source Vertex  : %d\n\n", source_node);

        printf("-------------------------------------------------------------------------\n");
        printf(" 1. Breadth-First Search (BFS) Benchmark\n");
        printf("-------------------------------------------------------------------------\n");
        printf("  %-20s %-20s %-20s\n", "Metric", "CPU (Serial)", "MPI (Parallel)");
        printf("  %-20s %-20.6f %-20.6f\n", "Runtime (s)", cpu_time, mpi_time);
        printf("  %-20s %-20d %-20d\n", "Nodes Reached", cpu_nodes_reached, mpi_nodes_reached);
        printf("  %-20s %-20lld %-20lld\n", "Edges Examined", cpu_edges, mpi_edges);
        printf("  %-20s %-20.2f %-20.2f\n", "Throughput (MTEPS)", cpu_mteps, mpi_mteps);
        printf("  Validation:          %s\n", bfs_correct ? "SUCCESS (Outputs match exactly!)" : "FAILURE (Outputs differ!)");
        if (mpi_time > 0) {
            printf("  MPI Speedup:         \033[1;32m%.2fx\033[0m\n", cpu_time / mpi_time);
        }

        printf("\n-------------------------------------------------------------------------\n");
        printf(" 2. Connected Components (CC) Benchmark\n");
        printf("-------------------------------------------------------------------------\n");
        printf("  %-20s %-20s %-20s\n", "Metric", "CPU (Serial)", "MPI (Parallel)");
        printf("  %-20s %-20.6f %-20.6f\n", "Runtime (s)", cpu_cc_time, mpi_cc_time);
        printf("  %-20s %-20d %-20d\n", "Components Found", cpu_components, mpi_components);
        printf("  %-20s %-20s %-20d\n", "Iterations", "N/A", mpi_cc_iterations);
        printf("  Validation:          %s\n", (cpu_components == mpi_components) ? "SUCCESS (Component counts match exactly!)" : "FAILURE (Component counts differ!)");
        if (mpi_cc_time > 0) {
            printf("  MPI Speedup:         \033[1;32m%.2fx\033[0m\n", cpu_cc_time / mpi_cc_time);
        }
        printf("=========================================================================\n");

        free(recvcounts);
        free(displs);
        free(global_mpi_level);
        free(cpu_level);
    }

    free(local_row_ptr);
    free(local_col_ind);
    free(local_level);

    MPI_Finalize();
    return 0;
}
