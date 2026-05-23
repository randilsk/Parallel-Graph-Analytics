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
| **OpenMP**            | Shared-memory parallelization      | Thread-local frontiers, lock-free atomic minimum hook, dynamic scheduling     |
| **MPI**               | Distributed-memory parallelization | Vertex partitioning, collective Allreduce swaps, binary hierarchical merge    |
| **Hybrid MPI+OpenMP** | Combined multi-level parallelism   | Inter-node (MPI) + Intra-node (OpenMP) optimization                          |
| **CUDA**              | GPU acceleration                   | Frontier-based traversal, coalesced memory access, lock-free atomic hooking  |

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
- **OpenMP**: Shared-memory CPU parallelization
- **MPI**: Distributed-memory communication
- **CUDA**: GPU kernel programming
- **Build System**: Master Makefile (supports all backends)

### Development Environment

- Compiler: GCC/G++ with OpenMP support
- MPI Implementation: OpenMPI / MPICH
- CUDA Toolkit: NVIDIA CUDA 11.0+
- Operating System: Linux (recommended) / Windows with WSL

---

## Project Structure

```text
Parallel-Graph-Analytics/
├── src/                    # Core source files grouped by backend
│   ├── serial/
│   │   ├── serial_analysis.c   # Serial baseline implementations (BFS + CC)
│   │   └── convert_to_csr.c    # Text-to-binary CSR converter utility
│   ├── parallel/
│   │   └── parallel_graph.c    # OpenMP parallel implementations (BFS + CC)
│   ├── mpi/
│   │   └── mpi.c               # MPI distributed implementations (BFS + CC)
│   ├── hybrid/
│   │   └── hybrid_analysis.cu  # OpenMP+CUDA hybrid implementation
│   └── cuda/
│       ├── cuda_analysis.cu    # CUDA GPU implementations (BFS + CC)
│       ├── build.bat           # Windows compiler script for MSVC+NVCC
│       └── README.md           # CUDA-specific instructions
├── bin/                    # Compiled binary outputs (git-ignored, created automatically)
├── data-sets/              # Source raw graph datasets (e.g., web-Google.txt)
├── outputs/                # Pre-processed CSR binary graphs and benchmark charts
│   ├── csr_format_web-google
│   └── benchmark_chart.svg
├── Makefile                # Master Makefile
├── README.md               # Project documentation
└── plot_benchmark.py       # SVG visualizer script for comparative runtimes
```

---

## Datasets
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
| **OpenMP**            | Shared-memory parallelization      | Thread-local frontiers, lock-free atomic minimum hook, dynamic scheduling     |
| **MPI**               | Distributed-memory parallelization | Vertex partitioning, collective Allreduce swaps, binary hierarchical merge    |
| **Hybrid MPI+OpenMP** | Combined multi-level parallelism   | Inter-node (MPI) + Intra-node (OpenMP) optimization                          |
| **CUDA**              | GPU acceleration                   | Frontier-based traversal, coalesced memory access, lock-free atomic hooking  |

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
- **OpenMP**: Shared-memory CPU parallelization
- **MPI**: Distributed-memory communication
- **CUDA**: GPU kernel programming
- **Build System**: Master Makefile (supports all backends)

### Development Environment

- Compiler: GCC/G++ with OpenMP support
- MPI Implementation: OpenMPI / MPICH
- CUDA Toolkit: NVIDIA CUDA 11.0+
- Operating System: Linux (recommended) / Windows with WSL

---

## Project Structure

```text
Parallel-Graph-Analytics/
├── src/                    # Core source files grouped by backend
│   ├── serial/
│   │   ├── serial_analysis.c   # Serial baseline implementations (BFS + CC)
│   │   └── convert_to_csr.c    # Text-to-binary CSR converter utility
│   ├── parallel/
│   │   └── parallel_graph.c    # OpenMP parallel implementations (BFS + CC)
│   ├── mpi/
│   │   └── mpi.c               # MPI distributed implementations (BFS + CC)
│   ├── hybrid/
│   │   └── hybrid_analysis.cu  # OpenMP+CUDA hybrid implementation
│   └── cuda/
│       ├── cuda_analysis.cu    # CUDA GPU implementations (BFS + CC)
│       ├── build.bat           # Windows compiler script for MSVC+NVCC
│       └── README.md           # CUDA-specific instructions
├── bin/                    # Compiled binary outputs (git-ignored, created automatically)
├── data-sets/              # Source raw graph datasets (e.g., web-Google.txt)
├── outputs/                # Pre-processed CSR binary graphs and benchmark charts
│   ├── csr_format_web-google
│   └── benchmark_chart.svg
├── Makefile                # Master Makefile
├── README.md               # Project documentation
└── plot_benchmark.py       # SVG visualizer script for comparative runtimes
```

---

## Datasets

### Source

Large-scale real-world graph datasets from publicly available repositories:

- [Stanford Network Analysis Project (SNAP)](http://snap.stanford.edu/data/)
- [SuiteSparse Matrix Collection](https://sparse.tamu.edu/)
- [Network Repository](http://networkrepository.com/)

---

## Building the Project

### Prerequisites

To compile all backends successfully, ensure your environment has:
- GCC compiler with OpenMP support (`-fopenmp`)
- MPI toolkit library (such as OpenMPI or MPICH) with `mpicc` wrapper
- NVIDIA CUDA Toolkit with `nvcc` compiler

### Master Compilation Command

You can build all executables at once or compile each backend individually using the master Makefile:

```bash
# Compile all 5 backend systems + Converter
make all

# Clean up all binaries and build artifacts
make clean
```

---

## Running the Code

### ⚡ Automated One-Command Execution (Recommended)
You can compile and execute any of the backends using the automated `make run_*` shortcuts. If a binary is missing or modified, it will compile it automatically before executing:

```bash
# 0. Convert SNAP dataset to binary CSR format
make run_convert

# 1. Execute CPU Serial baseline
make run_serial

# 2. Execute OpenMP CPU Parallel (Default: THREADS=8)
make run_openmp

# 3. Execute MPI Distributed Parallel (Default: RANKS=4, SOURCE=0)
make run_mpi

# 4. Execute GPU CUDA Parallel (Default: SOURCE=0)
make run_cuda

# 5. Execute Cooperative Hybrid OpenMP+CUDA (Default: THREADS=8, SOURCE=0)
make run_hybrid

# 6. Re-generate comparative visual benchmark charts
make run_plot
```

> [!TIP]
> **Custom Parameters**: You can override the default dataset, thread pools, or MPI ranks directly on the command line:
> - Change OpenMP thread limits: `make run_openmp THREADS=16`
> - Change MPI processes and start node: `make run_mpi RANKS=8 SOURCE=100`
> - Run on custom datasets: `make run_serial DATASET=outputs/custom_graph`

---

### 🏃 Manual Execution Guidelines
If you prefer to run the compiled binaries manually inside the `bin/` folder, use these exact parameter formats:

#### 0. Dataset Pre-processor
```bash
./bin/convert_to_csr <input_text_graph> <output_binary_csr>
```

#### 1. CPU Serial Baseline
```bash
./bin/serial_analysis <dataset.csr>
```

#### 2. OpenMP CPU Parallel
```bash
./bin/parallel_graph <dataset.csr> [num_threads]
```

#### 3. MPI Distributed Parallel
```bash
mpirun -np <num_ranks> ./bin/mpi_analysis <dataset.csr> [source_node]
```

#### 4. GPU CUDA Parallel
```bash
./bin/cuda_analysis <dataset.csr> [source_node]
```

#### 5. Hybrid CPU+GPU Parallel
```bash
./bin/hybrid_analysis <dataset.csr> [source_node] [num_threads]
```

---

## Evaluation Metrics

### 1. Correctness & Accuracy

- **Validation**: Parallel outputs are automatically gathered and validated key-by-key against the CPU serial baseline inside the binary.
- **Status**: SUCCESS confirmation is printed only if all levels and component counts match exactly.

### 2. Performance Metrics

- **Runtime**: Measured in exact wall-clock seconds.
- **Speedup**: $S_p = \frac{T_{\text{serial}}}{T_{\text{parallel}}}$ (runtime improvement factor).
- **Throughput (TEPS)**: Traversed Edges Per Second ($TEPS = \frac{\text{Edges traversed}}{\text{Execution time}}$). Higher is better.

---

## Expected Deliverables

### 1. Implementation

- [x] Serial baseline implementations (BFS + CC)
- [x] Shared-memory parallel code (OpenMP)
- [x] Distributed-memory parallel code (MPI)
- [x] Cooperative Hybrid parallel code (OpenMP+CUDA)
- [x] GPU-accelerated code (CUDA)

### 2. Documentation

- [x] Unified Project README
- [x] Zero-warning build system (Makefile)
- [x] Programmatic SVG charting script

---

## References

1. Beamer, S., Asanović, K., & Patterson, D. (2012). "Direction-optimizing breadth-first search." _SC'12_.
2. Bader, D. A., & Madduri, K. (2006). "Designing multithreaded algorithms for breadth-first search." _ICPP 2006_.
3. Merrill, D., Garland, M., & Grimshaw, A. (2012). "Scalable GPU graph traversal." _PPoPP 2012_.
4. Slota, G. M., Madduri, K., & Rajamanickam, S. (2014). "BFS and coloring-based parallel algorithms for connected components." _IPDPS 2014_.

---

## License

This project is developed as part of the EE7218 High Performance Computing course.
🏆 **Last Updated**: May 2026

---

## Contact

For questions or issues, please contact:

- Randil K.A.G.S. - [Email]
- Wijerathne G.P.W.P. - [Email]

---

**Last Updated**: February 25, 2026
