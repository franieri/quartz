# system.data Extension - DataFrame Library

A high-performance DataFrame library for Quartz, inspired by Python's pandas. Provides efficient data manipulation, analysis, and I/O operations optimized with SIMD instructions where available.

## Overview

The `system.data` extension provides:
- **DataFrame**: A 2D table with labeled columns of potentially different types
- **Series**: A 1D array with labels (column of a DataFrame)
- **High Performance**: SIMD-optimized operations (AVX2) for numerical computations
- **Null Handling**: Built-in support for missing values with efficient null bitmaps
- **Memory Efficient**: Columnar storage format optimized for analytics

## Quick Start

```quartz
import system.data;

// Create DataFrame from CSV
string csv = "name,age,salary
Alice,30,75000
Bob,25,55000";

let df = system.data.df.fromCSV(csv, ",", true);

// Print info
io.out.println(system.data.df.repr(df));

// Get statistics
io.out.println("Mean salary:", system.data.df.col.mean(df, "salary"));

// Clean up
system.data.df.destroy(df);
```

## DataFrame Functions

### Creation

| Function | Description |
|----------|-------------|
| `system.data.df.create()` | Create empty DataFrame |
| `system.data.df.fromCSV(csv, delimiter?, hasHeader?)` | Create from CSV string |
| `system.data.df.fromArrays(names[], arrays[])` | Create from column arrays |
| `system.data.df.destroy(dfId)` | Free DataFrame memory |

### Information

| Function | Description |
|----------|-------------|
| `system.data.df.numRows(dfId)` | Number of rows |
| `system.data.df.numCols(dfId)` | Number of columns |
| `system.data.df.columns(dfId)` | List of column names |
| `system.data.df.repr(dfId, maxRows?)` | String representation |
| `system.data.df.describe(dfId)` | Summary statistics |
| `system.data.df.memory(dfId)` | Memory usage in bytes |

### Column Operations

| Function | Description |
|----------|-------------|
| `system.data.df.addColumn(dfId, name, data[])` | Add new column |
| `system.data.df.removeColumn(dfId, name)` | Remove column |
| `system.data.df.renameColumn(dfId, old, new)` | Rename column |
| `system.data.df.select(dfId, names[])` | Select columns |
| `system.data.df.drop(dfId, names[])` | Drop columns |
| `system.data.df.getColumn(dfId, name)` | Get column as array |

### Row Operations

| Function | Description |
|----------|-------------|
| `system.data.df.head(dfId, n?)` | First n rows (default 5) |
| `system.data.df.tail(dfId, n?)` | Last n rows (default 5) |
| `system.data.df.slice(dfId, start, end)` | Row range |
| `system.data.df.query(dfId, expr)` | Filter with expression |

**Query Syntax:**
```quartz
// Simple comparisons
system.data.df.query(df, "age > 30");
system.data.df.query(df, "salary >= 50000");
system.data.df.query(df, "id == 1");
```

### Sorting

| Function | Description |
|----------|-------------|
| `system.data.df.sortBy(dfId, column, ascending?)` | Sort by column |

### Aggregation

| Function | Description |
|----------|-------------|
| `system.data.df.sum(dfId)` | Sum all numeric columns |
| `system.data.df.mean(dfId)` | Mean all numeric columns |
| `system.data.df.min(dfId)` | Min all numeric columns |
| `system.data.df.max(dfId)` | Max all numeric columns |
| `system.data.df.std(dfId)` | Std dev all numeric columns |

### Column-Level Stats

| Function | Description |
|----------|-------------|
| `system.data.df.col.sum(dfId, col)` | Sum of column |
| `system.data.df.col.mean(dfId, col)` | Mean of column |
| `system.data.df.col.min(dfId, col)` | Min of column |
| `system.data.df.col.max(dfId, col)` | Max of column |
| `system.data.df.col.std(dfId, col)` | Std dev of column |

### GroupBy Operations

```quartz
// Group by department and compute mean
let grouped = system.data.df.groupby.mean(df, "department");

// Other aggregations
system.data.df.groupby.sum(df, "category");
system.data.df.groupby.count(df, "category");
```

| Function | Description |
|----------|-------------|
| `system.data.df.groupby.sum(dfId, col)` | Group and sum |
| `system.data.df.groupby.mean(dfId, col)` | Group and mean |
| `system.data.df.groupby.count(dfId, col)` | Group and count |

### Join Operations

| Function | Description |
|----------|-------------|
| `system.data.df.merge(df1, df2, on, how?)` | Merge DataFrames |
| `system.data.df.concat(df1, df2, axis?)` | Concatenate DataFrames |

**Join Types (how parameter):**
- `"inner"` - Only matching rows (default)
- `"left"` - All from left, matching from right
- `"right"` - All from right, matching from left
- `"outer"` - All rows from both

**Axis (for concat):**
- `0` - Concatenate rows (default)
- `1` - Concatenate columns

### Missing Data

| Function | Description |
|----------|-------------|
| `system.data.df.dropna(dfId, any?)` | Drop rows with nulls |
| `system.data.df.fillna(dfId, value)` | Fill nulls with value |

### Statistical Analysis

| Function | Description |
|----------|-------------|
| `system.data.df.corr(dfId)` | Correlation matrix |
| `system.data.df.cov(dfId)` | Covariance matrix |

### Transformations

| Function | Description |
|----------|-------------|
| `system.data.df.transpose(dfId)` | Transpose DataFrame |
| `system.data.df.copy(dfId)` | Deep copy |

### I/O

| Function | Description |
|----------|-------------|
| `system.data.io.readCSV(filename, delim?, header?)` | Read CSV file |
| `system.data.io.writeCSV(dfId, filename, delim?, header?)` | Write CSV file |
| `system.data.io.writeJSON(dfId, filename, orient?)` | Write JSON file |
| `system.data.df.toCSV(dfId, delim?, header?)` | Convert to CSV string |
| `system.data.df.toJSON(dfId, orient?)` | Convert to JSON string |

## Series Functions

### Creation

| Function | Description |
|----------|-------------|
| `system.data.series.create(name, data[])` | Create from array |
| `system.data.series.range(name, start, end, step?)` | Create range |
| `system.data.series.zeros(name, count)` | Create zeros |
| `system.data.series.ones(name, count)` | Create ones |
| `system.data.series.destroy(seriesId)` | Free memory |

### Statistics

| Function | Description |
|----------|-------------|
| `system.data.series.sum(id)` | Sum |
| `system.data.series.mean(id)` | Mean |
| `system.data.series.min(id)` | Minimum |
| `system.data.series.max(id)` | Maximum |
| `system.data.series.std(id)` | Standard deviation |
| `system.data.series.median(id)` | Median |
| `system.data.series.quantile(id, q)` | Quantile (0-1) |

### Transformations

| Function | Description |
|----------|-------------|
| `system.data.series.normalize(id)` | Z-score normalization |
| `system.data.series.toArray(id)` | Convert to array |
| `system.data.series.repr(id)` | String representation |

## Performance Features

### SIMD Optimization

The library uses AVX2 SIMD instructions for:
- Vector addition, subtraction, multiplication
- Sum aggregation
- Scalar operations

Falls back to scalar operations on non-AVX systems.

### Memory Layout

- **Columnar storage**: Better cache utilization for analytics
- **Null bitmap**: Compact 1-bit per element null tracking
- **Reference counting**: Automatic memory management

### Best Practices

1. **Destroy DataFrames** when done to free memory
2. **Use query()** instead of manual filtering for better performance
3. **Prefer column operations** over row-by-row access
4. **Use appropriate data types** - numeric operations are fastest

## Examples

### Data Analysis Pipeline

```quartz
import system.data;

// Load data
let df = system.data.io.readCSV("sales.csv");

// Filter
let filtered = system.data.df.query(df, "amount > 1000");

// Aggregate by category
let summary = system.data.df.groupby.sum(filtered, "category");

// Sort by total
let sorted = system.data.df.sortBy(summary, "amount", false);

// Save result
system.data.io.writeCSV(sorted, "summary.csv");

// Cleanup
system.data.df.destroy(df);
system.data.df.destroy(filtered);
system.data.df.destroy(summary);
system.data.df.destroy(sorted);
```

### Statistical Analysis

```quartz
import system.data;

let df = system.data.io.readCSV("data.csv");

// Descriptive stats
let stats = system.data.df.describe(df);
io.out.println(system.data.df.repr(stats));

// Correlation analysis
let corr = system.data.df.corr(df);
io.out.println("Correlation matrix:");
io.out.println(system.data.df.repr(corr));

// Cleanup
system.data.df.destroy(df);
system.data.df.destroy(stats);
system.data.df.destroy(corr);
```

### Data Transformation

```quartz
import system.data;

// Create sample data
let s = system.data.series.create("values", [1.0, 2.0, 3.0, 4.0, 5.0]);

// Normalize (z-score)
let normalized = system.data.series.normalize(s);
io.out.println("Normalized:", system.data.series.repr(normalized));

// Statistics
io.out.println("Original mean:", system.data.series.mean(s));
io.out.println("Normalized mean:", system.data.series.mean(normalized));  // ~0
io.out.println("Normalized std:", system.data.series.std(normalized));    // ~1

system.data.series.destroy(s);
system.data.series.destroy(normalized);
```

## Integration with AI/ML

The DataFrame library is designed for integration with:
- **Machine Learning**: Feature engineering, data preprocessing
- **Database Queries**: Import/export data
- **JSON APIs**: Web service data handling
- **Analytics**: Statistical analysis and reporting

Future extensions will add:
- Matrix operations for ML
- Database connectors
- Visualization hooks
