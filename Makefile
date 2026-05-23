# =========================================================================
# Master Makefile for Parallel Graph Analytics
# Supports Serial, OpenMP, and CUDA GPU targets
# =========================================================================

# Compilers
CC = gcc
NVCC = /usr/local/cuda/bin/nvcc

# Compiler Flags
CFLAGS = -O3 -Wall
OMP_FLAGS = -fopenmp
NVCCFLAGS = -O3 -std=c++14 -arch=sm_50

# Executables
SERIAL_EXE = serial_analysis
OPENMP_EXE = parallel_graph
CONVERT_EXE = convert_to_csr
CUDA_EXE = cuda/cuda_analysis
HYBRID_EXE = hybrid_section/hybrid_analysis

# Phony Targets
.PHONY: all serial openmp convert cuda hybrid clean help

all: serial openmp convert cuda hybrid

# 1. Compile Serial Baseline
serial: serial_analysis.c
	$(CC) $(CFLAGS) -o $(SERIAL_EXE) serial_analysis.c

# 2. Compile OpenMP Parallel Version
openmp: parallel_graph.c
	$(CC) $(CFLAGS) $(OMP_FLAGS) -o $(OPENMP_EXE) parallel_graph.c

# 3. Compile CSR Converter
convert: convert_to_csr.c
	$(CC) $(CFLAGS) -o $(CONVERT_EXE) convert_to_csr.c

# 4. Compile CUDA GPU Implementation
cuda: cuda/cuda_analysis.cu
	$(NVCC) $(NVCCFLAGS) -o $(CUDA_EXE) cuda/cuda_analysis.cu

# 5. Compile OpenMP + CUDA Hybrid Implementation
hybrid: hybrid_section/hybrid_analysis.cu
	$(NVCC) $(NVCCFLAGS) -o $(HYBRID_EXE) hybrid_section/hybrid_analysis.cu

# Clean up all built files
clean:
	rm -f $(SERIAL_EXE) $(OPENMP_EXE) $(CONVERT_EXE) $(CUDA_EXE) $(HYBRID_EXE)
	rm -f cuda/*.exe cuda/*.exp cuda/*.lib hybrid_section/*.exe hybrid_section/*.exp hybrid_section/*.lib

# Help description
help:
	@echo "Available make targets:"
	@echo "  make all       - Build Serial, OpenMP, Converter, CUDA, and Hybrid targets"
	@echo "  make serial    - Build the CPU serial baseline"
	@echo "  make openmp    - Build the OpenMP shared-memory parallel version"
	@echo "  make convert   - Build the CSR text-to-binary dataset converter"
	@echo "  make cuda      - Build the high-performance GPU CUDA version"
	@echo "  make hybrid    - Build the cooperative CPU-GPU OpenMP+CUDA hybrid version"
	@echo "  make clean     - Remove all compiled executables and temporary objects"
