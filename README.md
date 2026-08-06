# WS25-Optimizer

Repository for the Optimizer project (Dresden Database Research Group, WS25/26). Includes `optimizer-server`, `optimizer-unit`, and `optimizer-compute-unit`.

## Requirements

Requires an x86 Linux system.

**Ubuntu (24.04):**
```bash
sudo apt update
sudo apt install protobuf-compiler git build-essential cmake graphviz
```

**Arch Linux:**
```bash
sudo pacman -Syu protobuf git base-devel cmake graphviz
```

## Build

### Host Setup

Generate Protobuf messages and compile the C++ binaries using the provided scripts:

```bash
./regenerate_proto.sh
cd cpp
./recompile.sh
```
Binaries are located in `cpp/build/bin/`.

*Optional: Run unit tests*
```bash
ctest --test-dir build --output-on-failure
```

## Usage

### 1. Server (`optimizer-server`)

Local dummy target if no real DB connection is available. Default port matches the OptimizerUnit.

```bash
./build/bin/optimizer-server
```

Or if you want the server to listen to a specific port:

```bash
./build/bin/optimizer-server -port 23232
```

### 2. ComputeUnit (`optimizer-compute-unit`)

Dummy ComputeUnit. You can start it using the default settings:

```bash
./build/bin/optimizer-compute-unit
```

Or with a specific server IP and port:

```bash
./build/bin/optimizer-compute-unit -ip 127.0.0.1 -port 37333
```

A running ComputeUnit is only needed if you use the `PARALLEL` keyword in a Query (explained in section Keywords).
Otherwise, running the `Server` only is sufficient.


### 3. OptimizerUnit (`optimizer-unit`)

Connects to the server and dispatches queries.

#### Command-Line Arguments

| Argument | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `-ip` | `string` | `127.0.0.1` | Target server IP address. |
| `-port` | `size_t` | `23232` | Target server port. |
| `-t` | `size_t` | `1` | Worker threads for the `QueryManager` pool. |
| `-q` | `string` | | Initial query executed immediately after connection. |
| `-f` | `string` | | File path with `;` separated queries. |
| `-log` | `string` | `optimizer.log` | Log file destination. |
| `-optimize` | `bool` | `true` | Toggle chain optimization. |
| `-optimize-join-outputs`| `bool` | `true` | Toggle join output optimization. |
| `-demo` | `bool` | `false` | Save logical/physical plans as images (requires `graphviz`). |

#### Execution Modes

Modes can be combined. The unit will process the file, then the CLI query, and drop into the interactive shell:
```bash
./build/bin/optimizer-unit -f all_queries.txt -q "SELECT lo_extendprice FROM lineorder"
```
*(Note: `all_queries.txt` containing all 13 SSB queries and `all_queries_parallel.txt` are provided in the repository.)*
* **Interactive (Default):** `tuddbs>` prompt. Use `exit` or `quit`.
* **Single Query (`-q`):** Executes string on startup.
* **File Batch (`-f`):** Executes parsed file sequentially.

#### Keywords

Prefix queries with keywords. **Caution:** If combined, `PARALLEL` must precede `NAME`.

* **`PARALLEL`**: DB-side intra-query parallelism. The `QueryManager` splits the physical plan into `WorkItem` batches.
* **`NAME <string>`**: Internal identifier for debugging and physical optimizer logs.

```bash
./build/bin/optimizer-unit -q "PARALLEL NAME MyQuery SELECT lo_extendprice FROM lineorder"
```

#### Enable Demo-Mode

In order to visualize the logical and physical query tree for a query, simply enable `Demo-Mode` using:

```bash
./build/bin/optimizer-unit -q "SELECT lo_extendprice FROM lineorder" -demo 1
```

The generated trees will be saved to your working directory (`cpp/`) as PNG's and as dot files.

## Architecture: QueryManager

Asynchronous processing via thread pool:
1.  **Queueing:** Queries pushed to a thread-safe queue.
2.  **Parsing:** Extract prefixes (`PARALLEL`, `NAME`).
3.  **Translation:** SQL -> `LogicalPlan`.
4.  **Optimization:** `LogicalPlan` -> `PhysicalPlan` via `PhysicalOptimizer` (applying enabled optimizations).
5.  **Dispatching:**
   * *Sequential:* Single `WorkRequest`.
   * *Parallel:* Synchronized batches of `WorkItem`s, tracked via `WorkCompletionTracker`.

## Documentation

Detailed developer logic is located in the [Logical Optimizer docs](docs/LogicalOptimizer/LogicalOptimizer.md) and [Physical Optimizer docs](docs/PhysicalOptimizer/PhysicalOptimizer.md).
