# GPU-Accelerated Graph Analytics Using CUDA

This folder contains a high-performance GPU-accelerated implementation of fundamental graph analytics operations: **Breadth-First Search (BFS)** and **Connected Components (CC)**. 

---

## 🚀 Key Parallel Strategies & Architectures

### 1. Breadth-First Search (BFS)
Traditional vertex-centric BFS on GPUs exhibits poor warp utilization and extreme memory contention. To address this, we implemented a **Frontier-Based (Work-Efficient) Parallel BFS**:
* **Double Buffering**: We maintain a `current_frontier` and a `next_frontier` array in GPU memory. Only the active vertices in the current level are processed by threads.
* **Atomic Visitation Control**: We use `atomicCAS` (Compare-And-Swap) on the vertex `level` array to ensure that each newly discovered vertex is visited and pushed to the `next_frontier` exactly once.
* **Coalesced Memory Access**: The frontier indices are processed in contiguous blocks, optimizing global memory access patterns of the CSR representations.

### 2. Connected Components (CC)
For Connected Components, we implement the **Hook-and-Jump (Shiloach-Vishkin)** algorithm which is guaranteed to converge in $O(\log |V|)$ iterations:
* **Hooking**: Threads attempt to connect component trees of neighboring vertices by updating the parent references. We use `atomicMin` on the parents mapping to avoid race conditions.
* **Pointer Jumping**: Threads compress parent pointers dynamically (`parent[i] = parent[parent[i]]`) in a logarithmic fashion, transforming disjoint trees into flat, unified component structures rapidly.
* **Convergence Loop**: Runs fully on the GPU, with a shared boolean variable copied back to check if any modifications were made in the iteration.

---

## 🛠️ Building the Project

### On Windows
The folder includes a compiler batch file `build.bat` that automatically detects the CUDA Toolkit path and MSVC C++ compiler (`cl.exe`).

Simply run:
```cmd
.\build.bat
```
*Note: If compilation fails, open the **Developer Command Prompt for VS 2022** and run `nvcc -O3 -std=c++17 -o cuda_analysis.exe cuda_analysis.cu` manually.*

### On Linux / WSL
Run the provided `Makefile`:
```bash
make
```

---

## 🏃 Running the Code

Execute the compiled executable, providing the path to a graph dataset in **CSR binary format** (generated using `convert_to_csr.c`):

### Windows
```cmd
.\cuda_analysis.exe ..\outputs\csr_format_web-google [source_vertex]
```

### Linux / WSL
```bash
./cuda_analysis ../outputs/csr_format_web-google [source_vertex]
```
*(By default, the source vertex is set to `0` if not provided.)*

---

## 📊 Evaluation & Verification

Upon running, the program will perform:
1. **CPU Serial Execution**: Runs standard serial BFS and CC.
2. **GPU CUDA Parallel Execution**: Runs GPU frontier-based BFS and Hook-and-Jump CC.
3. **Correctness Verification**: Directly verifies 100% accurate results by matching levels/reachability and connected component counts between GPU and CPU.
4. **Performance Summary**: Displays runtime (seconds), traversed edges per second (TEPS), and the exact Speedup multiplier (e.g. `25x` to `100x` faster on GPU).
