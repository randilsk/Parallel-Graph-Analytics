# =========================================================================
# Master Makefile for Parallel Graph Analytics
# Supports Serial, OpenMP, MPI, and CUDA GPU targets
# =========================================================================

# Compilers
CC = gcc
MPICC = mpicc
NVCC = /usr/local/cuda/bin/nvcc

# Compiler Flags
CFLAGS = -O3 -Wall
OMP_FLAGS = -fopenmp
NVCCFLAGS = -O3 -std=c++14 -arch=sm_50

# Output Directories
BIN_DIR = bin

# Executables
SERIAL_EXE = $(BIN_DIR)/serial_analysis
OPENMP_EXE = $(BIN_DIR)/parallel_graph
CONVERT_EXE = $(BIN_DIR)/convert_to_csr
MPI_EXE    = $(BIN_DIR)/mpi_analysis
CUDA_EXE   = $(BIN_DIR)/cuda_analysis
HYBRID_EXE = $(BIN_DIR)/hybrid_analysis

# Source Paths
SRC_SERIAL = src/serial/serial_analysis.c
SRC_CONVERT = src/serial/convert_to_csr.c
SRC_PARALLEL = src/parallel/parallel_graph.c
SRC_MPI      = src/mpi/mpi.c
SRC_CUDA     = src/cuda/cuda_analysis.cu
SRC_HYBRID   = src/hybrid/hybrid_analysis.cu

# Phony Targets
.PHONY: all serial openmp convert mpi cuda hybrid clean help check_dirs

all: check_dirs serial openmp convert mpi cuda hybrid

# Ensure bin directory exists
check_dirs:
	mkdir -p $(BIN_DIR)

# 1. Compile Serial Baseline
serial: check_dirs $(SRC_SERIAL)
	$(CC) $(CFLAGS) -o $(SERIAL_EXE) $(SRC_SERIAL)

# 2. Compile OpenMP Parallel Version
openmp: check_dirs $(SRC_PARALLEL)
	$(CC) $(CFLAGS) $(OMP_FLAGS) -o $(OPENMP_EXE) $(SRC_PARALLEL)

# 3. Compile CSR Converter
convert: check_dirs $(SRC_CONVERT)
	$(CC) $(CFLAGS) -o $(CONVERT_EXE) $(SRC_CONVERT)

# 4. Compile MPI Parallel Version
mpi: check_dirs $(SRC_MPI)
	$(MPICC) $(CFLAGS) -o $(MPI_EXE) $(SRC_MPI)

# 5. Compile CUDA GPU Implementation
cuda: check_dirs $(SRC_CUDA)
	$(NVCC) $(NVCCFLAGS) -o $(CUDA_EXE) $(SRC_CUDA)

# 6. Compile OpenMP + CUDA Hybrid Implementation
hybrid: check_dirs $(SRC_HYBRID)
	$(NVCC) $(NVCCFLAGS) -o $(HYBRID_EXE) $(SRC_HYBRID)

# Clean up all built files
clean:
	rm -rf $(BIN_DIR)
	rm -f *.exe *.exp *.lib

# Help description
help:
	@echo "Available make targets:"
	@echo "  make all       - Build Serial, OpenMP, Converter, MPI, CUDA, and Hybrid targets"
	@echo "  make serial    - Build the CPU serial baseline"
	@echo "  make openmp    - Build the OpenMP shared-memory parallel version"
	@echo "  make convert   - Build the CSR text-to-binary dataset converter"
	@echo "  make mpi       - Build the MPI distributed-memory parallel version"
	@echo "  make cuda      - Build the high-performance GPU CUDA version"
	@echo "  make hybrid    - Build the cooperative CPU-GPU OpenMP+CUDA hybrid version"
	@echo "  make clean     - Remove all compiled executables and temporary objects"
