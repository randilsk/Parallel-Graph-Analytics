# =========================================================================
# Master Makefile for Parallel Graph Analytics Suite
# Supports compilation and automated execution of all paradigms
# =========================================================================

# Compilers
CC     = gcc
MPICC  = mpicc
NVCC   = /usr/local/cuda/bin/nvcc

# Compiler Flags
CFLAGS    = -O3 -Wall
OMP_FLAGS = -fopenmp
NVCCFLAGS = -O3 -std=c++14 -arch=sm_50

# Default parameters for quick-run execution (Can be overridden on CLI)
# Example: make run_openmp THREADS=16
DATASET ?= outputs/csr_format_web-google
SOURCE  ?= 0
THREADS ?= 8
RANKS   ?= 4

# Output Directories
BIN_DIR = bin

# Executables
SERIAL_EXE  = $(BIN_DIR)/serial_analysis
OPENMP_EXE  = $(BIN_DIR)/parallel_graph
CONVERT_EXE = $(BIN_DIR)/convert_to_csr
MPI_EXE     = $(BIN_DIR)/mpi_analysis
CUDA_EXE    = $(BIN_DIR)/cuda_analysis
HYBRID_EXE  = $(BIN_DIR)/hybrid_analysis

# Source Paths
SRC_SERIAL   = src/serial/serial_analysis.c
SRC_CONVERT  = src/serial/convert_to_csr.c
SRC_PARALLEL = src/parallel/parallel_graph.c
SRC_MPI      = src/mpi/mpi.c
SRC_CUDA     = src/cuda/cuda_analysis.cu
SRC_HYBRID   = src/hybrid/hybrid_analysis.cu

# Phony Targets
.PHONY: all serial openmp convert mpi cuda hybrid clean help check_dirs \
        run_convert run_serial run_openmp run_mpi run_cuda run_hybrid run_plot

all: check_dirs serial openmp convert mpi cuda hybrid

# Ensure bin directory exists
check_dirs:
	mkdir -p $(BIN_DIR)

# =========================================================================
# 🛠️ Compilation Targets
# =========================================================================

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
	$(NVCC) $(NVCCFLAGS) -Xcompiler -fopenmp -o $(HYBRID_EXE) $(SRC_HYBRID) -lgomp

# Clean up all built files
clean:
	rm -rf $(BIN_DIR)
	rm -f *.exe *.exp *.lib

# =========================================================================
# 🏃 Execution Targets (Compiles automatically if binaries are out of date)
# =========================================================================

# Run CSR pre-processing
run_convert: convert
	./$(CONVERT_EXE) data-sets/web-Google.txt $(DATASET)

# Run Serial Baseline
run_serial: serial
	./$(SERIAL_EXE) $(DATASET)

# Run OpenMP CPU Parallel
run_openmp: openmp
	./$(OPENMP_EXE) $(DATASET) $(THREADS)

# Run MPI Distributed Parallel
run_mpi: mpi
	mpirun -np $(RANKS) ./$(MPI_EXE) $(DATASET) $(SOURCE)

# Run CUDA GPU Parallel
run_cuda: cuda
	./$(CUDA_EXE) $(DATASET) $(SOURCE)

# Run Hybrid OpenMP + CUDA Cooperative
run_hybrid: hybrid
	./$(HYBRID_EXE) $(DATASET) $(SOURCE) $(THREADS)

# Run the SVG charting visualizer
run_plot:
	python3 plot_benchmark.py

# =========================================================================
# ❓ Menu Guide
# =========================================================================
help:
	@echo "Available build targets:"
	@echo "  make all          - Compile all executable targets"
	@echo "  make serial       - Compile CPU serial baseline"
	@echo "  make openmp       - Compile OpenMP CPU version"
	@echo "  make convert      - Compile CSR dataset converter"
	@echo "  make mpi          - Compile MPI distributed version"
	@echo "  make cuda         - Compile GPU CUDA version"
	@echo "  make hybrid       - Compile OpenMP+CUDA hybrid version"
	@echo "  make clean        - Remove all compiled executables"
	@echo ""
	@echo "Available easy run targets (automatically compiles if needed):"
	@echo "  make run_convert  - Preprocess dataset to binary CSR"
	@echo "  make run_serial   - Execute CPU serial baseline"
	@echo "  make run_openmp   - Execute OpenMP CPU version (Default THREADS=8)"
	@echo "  make run_mpi      - Execute MPI version (Default RANKS=4, SOURCE=0)"
	@echo "  make run_cuda     - Execute GPU CUDA version (Default SOURCE=0)"
	@echo "  make run_hybrid   - Execute Cooperative Hybrid version (Default THREADS=8, SOURCE=0)"
	@echo "  make run_plot     - Execute visual chart script"
	@echo ""
	@echo "Overrides syntax example: make run_openmp THREADS=16"
