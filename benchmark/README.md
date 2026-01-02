# Quartz VM Performance Benchmarks

This directory contains comprehensive benchmarks for evaluating Quartz language performance.

## Quick Start

```bash
# Run all benchmarks
./run_benchmarks.sh

# Run specific benchmark suite
./run_benchmarks.sh --suite micro    # Micro-benchmarks for specific operations
./run_benchmarks.sh --suite compute  # Compute-intensive tests
./run_benchmarks.sh --suite memory   # Memory allocation patterns
./run_benchmarks.sh --suite io       # I/O operations (if applicable)
```

## Benchmark Suites

### 1. Micro-Benchmarks (`micro/`)
Tests specific VM operations in isolation:
- Integer arithmetic (add, sub, mul, div)
- Floating-point arithmetic
- Boolean conditions
- Comparison operations
- Unary operations

### 2. Compute Benchmarks (`compute/`)
Compute-intensive workloads:
- Tight loops
- Recursive functions
- Nested loops
- Mathematical computations

### 3. Memory Benchmarks (`memory/`)
Memory allocation patterns:
- Array creation/manipulation
- Dictionary operations
- Object instantiation
- Lambda captures

### 4. Real-World Benchmarks (`realworld/`)
Realistic usage patterns:
- Function call chains
- Mixed operations
- String processing
- Data transformation

## Output Format

Results are written to `results/` directory with timestamps:
- `results/YYYY-MM-DD_HH-MM-SS/` - Individual run results
- `results/latest/` - Symlink to most recent results
- `results/history.csv` - Historical performance data

## Interpreting Results

| Metric | Description |
|--------|-------------|
| `time_ms` | Execution time in milliseconds |
| `ops_per_sec` | Operations per second |
| `iterations` | Number of benchmark iterations |
| `std_dev` | Standard deviation across runs |

## Adding New Benchmarks

1. Create a `.qz` file in the appropriate suite directory
2. Add metadata comment at top of file:
   ```
   // @benchmark: name="My Benchmark" iterations=100000 category="compute"
   ```
3. The benchmark runner will automatically discover and run it

## Comparing Performance

To compare two versions:
```bash
./compare_results.sh results/baseline results/latest
```

This generates a comparison report showing performance changes.
