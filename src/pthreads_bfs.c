/*
 * pthreads_bfs.c - POSIX Threads Parallel BFS
 *
 * Build:
 *   gcc -O3 -Wall -Wextra -pthread -o bin/pthreads_bfs src/pthreads_bfs.c
 *
 * Run:
 *   ./bin/pthreads_bfs outputs/csr_format_web-google
 *   ./bin/pthreads_bfs outputs/csr_format_web-google 0 4
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>

typedef struct {
    int  num_nodes;
    int *row_ptr;
    int *col_ind;
    int *level;
    int *current_frontier;
    int  current_frontier_size;
    int *next_frontier;
    int  next_frontier_size;     /* atomic counter */
    long long *edges_per_thread;
    int  num_threads;
    pthread_barrier_t barrier;
    int  done;
} SharedState;

typedef struct {
    SharedState *s;
    int          tid;
} WorkerArg;

/* ------------------------------------------------------------------ */
/* Worker                                                               */
/* ------------------------------------------------------------------ */
void *bfs_worker(void *arg) {
    WorkerArg   *wa  = (WorkerArg *)arg;
    SharedState *s   = wa->s;
    int          tid = wa->tid;

    while (1) {
        /* Barrier 1: wait for main to set up this level */
        pthread_barrier_wait(&s->barrier);

        /* Check exit AFTER barrier 1 — main sets done before this barrier */
        if (s->done) {
            /* Hit barrier 2 so main thread can unblock too */
            pthread_barrier_wait(&s->barrier);
            break;
        }

        /* Compute chunk */
        int total       = s->current_frontier_size;
        int chunk_start = (long long)total *  tid      / s->num_threads;
        int chunk_end   = (long long)total * (tid + 1) / s->num_threads;

        long long local_edges = 0;

        for (int i = chunk_start; i < chunk_end; i++) {
            int u      = s->current_frontier[i];
            int edge_s = s->row_ptr[u];
            int edge_e = s->row_ptr[u + 1];
            local_edges += (edge_e - edge_s);

            for (int e = edge_s; e < edge_e; e++) {
                int v = s->col_ind[e];
                if (s->level[v] == -1) {
                    if (__sync_bool_compare_and_swap(
                            &s->level[v], -1, s->level[u] + 1)) {
                        int idx = __sync_fetch_and_add(
                                      &s->next_frontier_size, 1);
                        s->next_frontier[idx] = v;
                    }
                }
            }
        }

        s->edges_per_thread[tid] = local_edges;

        /* Barrier 2: signal main that this level is done */
        pthread_barrier_wait(&s->barrier);
    }

    return NULL;
}

/* ------------------------------------------------------------------ */
/* BFS driver                                                           */
/* ------------------------------------------------------------------ */
void pthreads_bfs(int source, int num_nodes, int *row_ptr, int *col_ind,
                  int num_threads)
{
    SharedState *s = (SharedState *)calloc(1, sizeof(SharedState));

    s->num_nodes        = num_nodes;
    s->row_ptr          = row_ptr;
    s->col_ind          = col_ind;
    s->num_threads      = num_threads;
    s->done             = 0;
    s->level            = (int *)malloc(num_nodes * sizeof(int));
    s->current_frontier = (int *)malloc(num_nodes * sizeof(int));
    s->next_frontier    = (int *)malloc(num_nodes * sizeof(int));
    s->edges_per_thread = (long long *)calloc(num_threads, sizeof(long long));

    for (int i = 0; i < num_nodes; i++) s->level[i] = -1;

    s->level[source]         = 0;
    s->current_frontier[0]  = source;
    s->current_frontier_size = 1;
    s->next_frontier_size    = 0;

    /* num_threads workers + 1 main = num_threads + 1 */
    pthread_barrier_init(&s->barrier, NULL, num_threads + 1);

    pthread_t *threads = (pthread_t *)malloc(num_threads * sizeof(pthread_t));
    WorkerArg *wargs   = (WorkerArg  *)malloc(num_threads * sizeof(WorkerArg));

    for (int t = 0; t < num_threads; t++) {
        wargs[t].s   = s;
        wargs[t].tid = t;
        pthread_create(&threads[t], NULL, bfs_worker, &wargs[t]);
    }

    long long total_edges   = 0;
    int       total_reached = 1;

    printf("\n--- Pthreads BFS from node %d (%d threads) ---\n",
           source, num_threads);

    struct timeval tv;
    gettimeofday(&tv, NULL);
    double start_time = tv.tv_sec + tv.tv_usec * 1e-6;

    while (s->current_frontier_size > 0) {
        s->next_frontier_size = 0;

        /* Barrier 1: release workers */
        pthread_barrier_wait(&s->barrier);

        /* --- workers expanding in parallel --- */

        /* Barrier 2: wait for workers to finish */
        pthread_barrier_wait(&s->barrier);

        for (int t = 0; t < num_threads; t++)
            total_edges += s->edges_per_thread[t];

        total_reached += s->next_frontier_size;

        int *tmp             = s->current_frontier;
        s->current_frontier  = s->next_frontier;
        s->next_frontier     = tmp;
        s->current_frontier_size = s->next_frontier_size;
    }

    gettimeofday(&tv, NULL);
    double runtime = (tv.tv_sec + tv.tv_usec * 1e-6) - start_time;

    /* Signal exit: set done=1 BEFORE barrier 1 so workers see it */
    s->done = 1;
    pthread_barrier_wait(&s->barrier);   /* barrier 1 — workers check done */
    pthread_barrier_wait(&s->barrier);   /* barrier 2 — workers hit before break */

    for (int t = 0; t < num_threads; t++)
        pthread_join(threads[t], NULL);

    printf("Pthreads BFS Runtime : %.6f seconds\n", runtime);
    printf("Nodes reached        : %d / %d\n", total_reached, num_nodes);
    printf("Edges examined       : %lld\n", total_edges);
    if (runtime > 0)
        printf("Performance          : %.2f MTEPS\n",
               total_edges / runtime / 1e6);

    pthread_barrier_destroy(&s->barrier);
    free(s->level);
    free(s->current_frontier);
    free(s->next_frontier);
    free(s->edges_per_thread);
    free(s);
    free(threads);
    free(wargs);
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 4) {
        fprintf(stderr,
            "Usage: %s <graph.csr> [source_node] [num_threads]\n", argv[0]);
        return 1;
    }

    int source_node = (argc >= 3) ? atoi(argv[2]) : 0;
    int num_threads = (argc >= 4) ? atoi(argv[3]) : 4;

    if (num_threads < 1) {
        fprintf(stderr, "num_threads must be >= 1\n");
        return 1;
    }

    FILE *file = fopen(argv[1], "rb");
    if (!file) {
        fprintf(stderr, "Error: Cannot open %s\n", argv[1]);
        return 1;
    }

    int num_nodes, num_edges;
    if (fread(&num_nodes, sizeof(int), 1, file) != 1 ||
        fread(&num_edges, sizeof(int), 1, file) != 1) {
        fprintf(stderr, "Error reading header\n");
        fclose(file); return 1;
    }

    printf("Graph: %d nodes, %d edges\n", num_nodes, num_edges);

    int *row_ptr = (int *)malloc((num_nodes + 1) * sizeof(int));
    int *col_ind = (int *)malloc(num_edges * sizeof(int));

    if (fread(row_ptr, sizeof(int), num_nodes + 1, file) != (size_t)(num_nodes + 1) ||
        fread(col_ind, sizeof(int), num_edges,      file) != (size_t)num_edges) {
        fprintf(stderr, "Error reading graph data\n");
        fclose(file); return 1;
    }
    fclose(file);

    if (source_node < 0 || source_node >= num_nodes) {
        fprintf(stderr, "Invalid source node %d (graph has %d nodes)\n",
                source_node, num_nodes);
        return 1;
    }

    pthreads_bfs(source_node, num_nodes, row_ptr, col_ind, num_threads);

    free(row_ptr);
    free(col_ind);
    return 0;
}