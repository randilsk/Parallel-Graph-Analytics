# Compiler
NVCC = /usr/local/cuda/bin/nvcc

# Compiler Flags (Compiles CUDA with OpenMP host-compiler support)
NVCCFLAGS = -O3 -std=c++14 -arch=sm_50 -Xcompiler -fopenmp

# Target Executable
TARGET = hybrid_analysis

# Source Files
SRC = hybrid_analysis.cu

# Build Rule
all: $(TARGET)

$(TARGET): $(SRC)
	$(NVCC) $(NVCCFLAGS) -o $(TARGET) $(SRC)

# Clean Rule
clean:
	rm -f $(TARGET)

.PHONY: all clean
