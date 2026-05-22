# =============================================================================
# Parallel Graph Analytics — Makefile
# =============================================================================
# Usage:
#   make              → build everything
#   make serial       → build serial analysis only
#   make openmp       → build OpenMP BFS only
#   make mpi          → build MPI BFS only
#   make convert      → build CSR converter only
#   make run-convert  → convert web-Google.txt → outputs/csr_format_web-google
#   make run-serial   → run serial BFS + CC
#   make run-openmp   → run OpenMP BFS (set THREADS= to change thread count)
#   make run-mpi      → run MPI BFS    (set NP= to change process count)
#   make run-all      → convert dataset then run all three implementations
#   make clean        → remove all compiled binaries
# =============================================================================

# --- Compilers & Flags -------------------------------------------------------
CC      = gcc
MPICC   = mpicc
CFLAGS  = -O3 -Wall -Wextra -Wno-unused-result

# --- Directories -------------------------------------------------------------
SRC     = src
DATA    = data
OUT     = outputs

# --- Binaries (written to project root) --------------------------------------
# New
BIN         = bin
SERIAL      = $(BIN)/serial_analysis
OPENMP_BFS  = $(BIN)/openmp_bfs
MPI_BFS     = $(BIN)/mpi_bfs
CONVERTER   = $(BIN)/convert_to_csr

# --- Dataset -----------------------------------------------------------------
DATASET     = $(OUT)/csr_format_web-google
SOURCE_NODE = 0

# --- Runtime defaults (override on command line) -----------------------------
THREADS = 4
NP      = 4

# =============================================================================
# Default target: build everything
# =============================================================================
.PHONY: all
all: convert serial openmp mpi
	@echo ""
	@echo "Build complete. Binaries: $(SERIAL)  $(OPENMP_BFS)  $(MPI_BFS)  $(CONVERTER)"
	@echo "Next: run 'make run-convert' to generate the CSR dataset."

# =============================================================================
# Build targets
# =============================================================================
.PHONY: convert
convert: $(CONVERTER)

$(CONVERTER): $(DATA)/convert_to_csr.c
	$(CC) $(CFLAGS) -o $@ $<

# ---
.PHONY: serial
serial: $(SERIAL)

$(SERIAL): $(SRC)/serial_analysis.c
	$(CC) $(CFLAGS) -o $@ $<

# ---
.PHONY: openmp
openmp: $(OPENMP_BFS)

$(OPENMP_BFS): $(SRC)/openmp_bfs.c
	$(CC) $(CFLAGS) -fopenmp -o $@ $<

# ---
.PHONY: mpi
mpi: $(MPI_BFS)

$(MPI_BFS): $(SRC)/mpi_bfs.c
	$(MPICC) $(CFLAGS) -o $@ $<

# =============================================================================
# Run targets
# =============================================================================
.PHONY: run-convert
run-convert: $(CONVERTER)
	@echo "--- Converting dataset ---"
	./$(CONVERTER) $(DATA)/web-Google.txt $(DATASET)
	@echo "CSR binary written to $(DATASET)"

.PHONY: run-serial
run-serial: $(SERIAL) $(DATASET)
	@echo "--- Serial BFS + CC (source node: $(SOURCE_NODE)) ---"
	./$(SERIAL) $(DATASET) $(SOURCE_NODE)

.PHONY: run-openmp
run-openmp: $(OPENMP_BFS) $(DATASET)
	@echo "--- OpenMP BFS  threads=$(THREADS)  source=$(SOURCE_NODE) ---"
	OMP_NUM_THREADS=$(THREADS) ./$(OPENMP_BFS) $(DATASET) $(SOURCE_NODE)

.PHONY: run-mpi
run-mpi: $(MPI_BFS) $(DATASET)
	@echo "--- MPI BFS  processes=$(NP)  source=$(SOURCE_NODE) ---"
	mpirun -np $(NP) ./$(MPI_BFS) $(DATASET) $(SOURCE_NODE)

.PHONY: run-all
run-all: run-convert run-serial run-openmp run-mpi
	@echo ""
	@echo "All implementations finished."

# =============================================================================
# Utility
# =============================================================================
.PHONY: clean
clean:
	@echo "Removing binaries..."
	rm -f $(SERIAL) $(OPENMP_BFS) $(MPI_BFS) $(CONVERTER)
	@echo "Done."

# Check that required tools are available
.PHONY: check-deps
check-deps:
	@command -v gcc    >/dev/null 2>&1 && echo "gcc:     OK" || echo "gcc:     NOT FOUND"
	@command -v mpicc  >/dev/null 2>&1 && echo "mpicc:   OK" || echo "mpicc:   NOT FOUND"
	@command -v mpirun >/dev/null 2>&1 && echo "mpirun:  OK" || echo "mpirun:  NOT FOUND"