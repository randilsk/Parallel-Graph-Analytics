# Compiler
NVCC = nvcc

# Compiler Flags
NVCCFLAGS = -O3 -std=c++14 -arch=sm_50

# Target Executable
TARGET = cuda_analysis

# Source Files
SRC = cuda_analysis.cu

# Build Rule
all: $(TARGET)

$(TARGET): $(SRC)
	$(NVCC) $(NVCCFLAGS) -o $(TARGET) $(SRC)

# Clean Rule
clean:
	rm -f $(TARGET)

.PHONY: all clean
