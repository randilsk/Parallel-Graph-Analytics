# Parallel Graph Analytics Using Multi-Level Parallelization

**EE7218 - High Performance Computing**  
**Course Project - 2026**

## Team Members

- **Randil K.A.G.S.** - EG/2021/4745
- **Wijerathne G.P.W.P.** - EG/2021/4871

---

## Table of Contents

- [Motivation & Objectives](#motivation--objectives)
- [Graph Operations](#graph-operations)
- [Parallel Strategies](#parallel-strategies)
- [Technical Implementation](#technical-implementation)
- [Project Structure](#project-structure)
- [Datasets](#datasets)
- [Building the Project](#building-the-project)
- [Running the Code](#running-the-code)
- [Evaluation Metrics](#evaluation-metrics)
- [Expected Deliverables](#expected-deliverables)
- [References](#references)

---

## Motivation & Objectives

Large-scale infrastructure, communication, and transportation systems can be modeled as sparse graphs. Efficient graph traversal and connectivity analysis are essential for:

- Routing optimization
- Reliability assessment
- System resilience studies

### Project Aim

Design and evaluate high-performance implementations of fundamental graph analytics operations using multiple parallel computing models, comparing performance across:

- **Shared-memory** architectures (OpenMP)
- **Distributed-memory** systems (MPI)
- **Hybrid** approaches (MPI+OpenMP)
- **GPU-based** platforms (CUDA)

---

## Graph Operations

### 1. Breadth-First Search (BFS)

Performs level-wise traversal from a source vertex, generating:

- Traversal levels for each vertex
- Parent relationships for path reconstruction
- Reachability information

**Applications**: Shortest path finding, network diameter calculation, social network analysis

### 2. Connected Components (CC)

Identifies mutually reachable vertex groups by assigning component labels to vertices.

**Applications**: Network connectivity analysis, community detection, infrastructure resilience

---

## Parallel Strategies

| Strategy              | Description                        | Key Features                                                                 |
| --------------------- | ---------------------------------- | ---------------------------------------------------------------------------- |
| **Serial**            | Baseline implementation            | Reference for correctness and performance comparison                         |
| **OpenMP**            | Shared-memory parallelization      | Thread-local buffers, atomic visitation control                              |
| **MPI**               | Distributed-memory parallelization | Vertex partitioning, message-passing boundary updates                        |
| **Hybrid MPI+OpenMP** | Combined multi-level parallelism   | Inter-node (MPI) + Intra-node (OpenMP) optimization                          |
| **CUDA**              | GPU acceleration                   | Frontier-based traversal, coalesced memory access, shared memory utilization |

---

## Technical Implementation

### Graph Representation

- **Format**: Compressed Sparse Row (CSR)
  - `row_ptr[]`: Index array for vertex adjacency list offsets
  - `col_ind[]`: Column indices of neighbors
- **Advantages**:
  - Efficient neighbor access
  - Cache locality optimization
  - Suitable for both CPU and GPU

### Programming Languages & Tools

- **C/C++**: Core implementation language
- **OpenMP**: Shared-memory parallelization
- **MPI**: Distributed-memory communication
- **CUDA**: GPU kernel programming
- **Build System**: Makefile / CMake

### Development Environment

- Compiler: GCC/G++ with OpenMP support
- MPI Implementation: OpenMPI / MPICH
- CUDA Toolkit: NVIDIA CUDA 11.0+
- Operating System: Linux (recommended) / Windows with WSL

---

## Project Structure

```
Parallel-Graph-Analytics/
├── README.md
├── datasets/               # Graph datasets in CSR format
├── src/
│   ├── serial/            # Serial baseline implementations
│   ├── openmp/            # OpenMP parallel implementations
│   ├── mpi/               # MPI distributed implementations
│   ├── hybrid/            # MPI+OpenMP hybrid implementations
│   └── cuda/              # CUDA GPU implementations
├── include/               # Header files
├── scripts/               # Build and run scripts
├── results/               # Performance results and logs
├── analysis/              # Analysis scripts and visualizations
└── docs/                  # Documentation and reports
```

---

## Datasets

### Source

Large-scale real-world graph datasets from publicly available repositories:

- [Stanford Network Analysis Project (SNAP)](http://snap.stanford.edu/data/)
- [SuiteSparse Matrix Collection](https://sparse.tamu.edu/)
- [Network Repository](http://networkrepository.com/)

### Dataset Characteristics

- Varying sizes (vertices and edges)
- Different structural properties (density, diameter, clustering coefficient)
- Real-world and synthetic graphs

### Preprocessing

- Convert to CSR format
- Generate metadata (number of vertices, edges, average degree)
- Validation of graph structure

---

## Building the Project

### Prerequisites

```bash
# Install GCC/G++ with OpenMP support
# Install MPI (OpenMPI or MPICH)
# Install CUDA Toolkit (for GPU implementations)
```

### Compilation

```bash
# Serial version
make serial

# OpenMP version
make openmp

# MPI version
make mpi

# Hybrid MPI+OpenMP version
make hybrid

# CUDA version
make cuda

# Build all versions
make all
```

---

## Running the Code

### Serial Execution

```bash
./bin/serial_bfs <graph_file> <source_vertex>
./bin/serial_cc <graph_file>
```

### OpenMP Execution

```bash
export OMP_NUM_THREADS=16
./bin/openmp_bfs <graph_file> <source_vertex>
./bin/openmp_cc <graph_file>
```

### MPI Execution

```bash
mpirun -np 4 ./bin/mpi_bfs <graph_file> <source_vertex>
mpirun -np 4 ./bin/mpi_cc <graph_file>
```

### Hybrid MPI+OpenMP Execution

```bash
export OMP_NUM_THREADS=8
mpirun -np 4 ./bin/hybrid_bfs <graph_file> <source_vertex>
mpirun -np 4 ./bin/hybrid_cc <graph_file>
```

### CUDA Execution

```bash
./bin/cuda_bfs <graph_file> <source_vertex>
./bin/cuda_cc <graph_file>
```

---

## Evaluation Metrics

### 1. Correctness & Accuracy

- **Validation**: Compare parallel outputs against serial baseline
- **Metrics**: Exact comparison or Root Mean Square Error (RMSE)

### 2. Performance Metrics

- **Runtime**: Wall-clock time (seconds)
- **Speedup**: $S_p = \frac{T_1}{T_p}$ (serial time / parallel time)
- **Parallel Efficiency**: $E_p = \frac{S_p}{p}$ (speedup / number of processors)

### 3. Throughput

- **TEPS**: Traversed Edges Per Second
- Formula: $TEPS = \frac{\text{Number of edges traversed}}{\text{Execution time}}$

### 4. Scalability Analysis

- **Strong Scaling**: Fixed problem size, varying number of processors
- **Weak Scaling**: Problem size scales proportionally with processors
- **Communication Overhead**: Time spent in inter-process communication

---

## Expected Deliverables

### 1. Implementation

- [x] Serial implementation (C/C++)
- [ ] Shared-memory parallel code (OpenMP)
- [ ] Distributed-memory parallel code (MPI)
- [ ] Hybrid parallel code (MPI+OpenMP)
- [ ] GPU-accelerated code (CUDA)

### 2. Documentation

- [x] Project README
- [ ] Code documentation and comments
- [ ] Build and execution instructions

### 3. Analysis Report

- [ ] Parallelization strategy diagrams
- [ ] Accuracy validation results
- [ ] Timing comparisons with parameter variation
- [ ] Speedup and efficiency graphs
- [ ] Strong and weak scaling analysis
- [ ] Communication overhead analysis
- [ ] Performance comparison across all implementations

### 4. Presentation

- [ ] Project presentation slides
- [ ] Demo of implementations

---

## References

1. Beamer, S., Asanović, K., & Patterson, D. (2012). "Direction-optimizing breadth-first search." _SC'12: Proceedings of the International Conference on High Performance Computing, Networking, Storage and Analysis_.

2. Bader, D. A., & Madduri, K. (2006). "Designing multithreaded algorithms for breadth-first search and st-connectivity on the Cray MTA-2." _ICPP 2006_.

3. Merrill, D., Garland, M., & Grimshaw, A. (2012). "Scalable GPU graph traversal." _ACM SIGPLAN Notices, 47(8)_.

4. Slota, G. M., Madduri, K., & Rajamanickam, S. (2014). "BFS and coloring-based parallel algorithms for strongly connected components and related problems." _IPDPS 2014_.

---

## License

This project is developed as part of the EE7218 High Performance Computing course.

---

## Contact

For questions or issues, please contact:

- Randil K.A.G.S. - [Email]
- Wijerathne G.P.W.P. - [Email]

---

**Last Updated**: February 25, 2026
