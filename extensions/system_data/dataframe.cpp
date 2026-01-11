#include "dataframe.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <set>

namespace qz_data {

// ============================================================================
// DataFrame Implementation
// ============================================================================

DataFrame::DataFrame(const std::unordered_map<std::string, Series>& columns) {
    for (const auto& [name, series] : columns) {
        addColumn(name, series);
    }
}

DataFrame DataFrame::fromColumns(const std::vector<std::pair<std::string, Series>>& columns) {
    DataFrame df;
    for (const auto& [name, series] : columns) {
        df.addColumn(name, series);
    }
    return df;
}

DataFrame DataFrame::fromRows(const std::vector<std::string>& columnNames,
                               const std::vector<std::vector<double>>& rows) {
    DataFrame df;
    size_t nrows = rows.size();
    size_t ncols = columnNames.size();
    
    // Initialize columns
    std::vector<std::vector<double>> columnData(ncols);
    for (auto& col : columnData) {
        col.reserve(nrows);
    }
    
    // Transpose row-oriented data to column-oriented
    for (const auto& row : rows) {
        for (size_t c = 0; c < ncols && c < row.size(); ++c) {
            columnData[c].push_back(row[c]);
        }
    }
    
    // Add columns
    for (size_t c = 0; c < ncols; ++c) {
        df.addColumn(columnNames[c], Series(columnNames[c], columnData[c]));
    }
    
    return df;
}

DataFrame DataFrame::fromCSV(const std::string& content, char delimiter, bool hasHeader) {
    DataFrame df;
    std::istringstream stream(content);
    std::string line;
    std::vector<std::string> columnNames;
    std::vector<std::vector<std::string>> columnData;
    
    bool firstLine = true;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        
        std::vector<std::string> fields;
        std::string field;
        bool inQuotes = false;
        
        for (size_t i = 0; i < line.size(); ++i) {
            char c = line[i];
            if (c == '"') {
                inQuotes = !inQuotes;
            } else if (c == delimiter && !inQuotes) {
                fields.push_back(field);
                field.clear();
            } else {
                field += c;
            }
        }
        fields.push_back(field);
        
        if (firstLine && hasHeader) {
            columnNames = fields;
            columnData.resize(fields.size());
            firstLine = false;
            continue;
        }
        
        if (firstLine) {
            columnNames.resize(fields.size());
            for (size_t i = 0; i < fields.size(); ++i) {
                columnNames[i] = "col" + std::to_string(i);
            }
            columnData.resize(fields.size());
            firstLine = false;
        }
        
        for (size_t i = 0; i < fields.size() && i < columnData.size(); ++i) {
            columnData[i].push_back(fields[i]);
        }
    }
    
    // Try to convert columns to appropriate types
    for (size_t c = 0; c < columnNames.size(); ++c) {
        const auto& col = columnData[c];
        
        // Try to parse as doubles
        bool allNumeric = true;
        std::vector<double> numericData;
        numericData.reserve(col.size());
        
        for (const auto& val : col) {
            try {
                if (val.empty()) {
                    numericData.push_back(std::nan(""));
                } else {
                    numericData.push_back(std::stod(val));
                }
            } catch (...) {
                allNumeric = false;
                break;
            }
        }
        
        if (allNumeric) {
            df.addColumn(columnNames[c], Series(columnNames[c], numericData));
        } else {
            df.addColumn(columnNames[c], Series(columnNames[c], col));
        }
    }
    
    return df;
}

// ============================================================================
// Basic Info
// ============================================================================

std::vector<std::string> DataFrame::columnNames() const {
    return column_order_;
}

std::vector<DType> DataFrame::dtypes() const {
    std::vector<DType> types;
    types.reserve(column_order_.size());
    for (const auto& name : column_order_) {
        types.push_back(columns_.at(name).dtype());
    }
    return types;
}

// ============================================================================
// Column Access
// ============================================================================

Series& DataFrame::operator[](const std::string& name) {
    auto it = columns_.find(name);
    if (it == columns_.end()) {
        throw std::runtime_error("Column '" + name + "' not found");
    }
    return it->second;
}

const Series& DataFrame::operator[](const std::string& name) const {
    auto it = columns_.find(name);
    if (it == columns_.end()) {
        throw std::runtime_error("Column '" + name + "' not found");
    }
    return it->second;
}

bool DataFrame::hasColumn(const std::string& name) const {
    return columns_.find(name) != columns_.end();
}

// ============================================================================
// Column Management
// ============================================================================

void DataFrame::addColumn(const std::string& name, const Series& series) {
    if (columns_.empty()) {
        nrows_ = series.size();
    } else if (series.size() != nrows_) {
        throw std::runtime_error("Series size mismatch: expected " + 
                                std::to_string(nrows_) + ", got " + 
                                std::to_string(series.size()));
    }
    
    if (columns_.find(name) == columns_.end()) {
        column_order_.push_back(name);
    }
    columns_[name] = series;
    columns_[name].setName(name);
}

void DataFrame::addColumn(const std::string& name, Series&& series) {
    if (columns_.empty()) {
        nrows_ = series.size();
    } else if (series.size() != nrows_) {
        throw std::runtime_error("Series size mismatch");
    }
    
    if (columns_.find(name) == columns_.end()) {
        column_order_.push_back(name);
    }
    series.setName(name);
    columns_[name] = std::move(series);
}

void DataFrame::removeColumn(const std::string& name) {
    columns_.erase(name);
    column_order_.erase(
        std::remove(column_order_.begin(), column_order_.end(), name),
        column_order_.end()
    );
    updateRowCount();
}

void DataFrame::renameColumn(const std::string& oldName, const std::string& newName) {
    auto it = columns_.find(oldName);
    if (it == columns_.end()) return;
    
    Series s = std::move(it->second);
    s.setName(newName);
    columns_.erase(it);
    columns_[newName] = std::move(s);
    
    for (auto& name : column_order_) {
        if (name == oldName) {
            name = newName;
            break;
        }
    }
}

DataFrame DataFrame::select(const std::vector<std::string>& names) const {
    DataFrame df;
    for (const auto& name : names) {
        if (hasColumn(name)) {
            df.addColumn(name, columns_.at(name));
        }
    }
    return df;
}

DataFrame DataFrame::drop(const std::vector<std::string>& names) const {
    std::set<std::string> dropSet(names.begin(), names.end());
    DataFrame df;
    for (const auto& name : column_order_) {
        if (dropSet.find(name) == dropSet.end()) {
            df.addColumn(name, columns_.at(name));
        }
    }
    return df;
}

// ============================================================================
// Row Access
// ============================================================================

std::unordered_map<std::string, double> DataFrame::rowAsMap(size_t idx) const {
    std::unordered_map<std::string, double> row;
    for (const auto& name : column_order_) {
        row[name] = columns_.at(name).getDouble(idx);
    }
    return row;
}

// ============================================================================
// Filtering and Selection
// ============================================================================

DataFrame DataFrame::head(size_t n) const {
    return slice(0, std::min(n, nrows_));
}

DataFrame DataFrame::tail(size_t n) const {
    if (n >= nrows_) return copy();
    return slice(nrows_ - n, nrows_);
}

DataFrame DataFrame::slice(size_t start, size_t end) const {
    std::vector<size_t> indices;
    indices.reserve(end - start);
    for (size_t i = start; i < end && i < nrows_; ++i) {
        indices.push_back(i);
    }
    return take(indices);
}

DataFrame DataFrame::take(const std::vector<size_t>& indices) const {
    DataFrame df;
    for (const auto& name : column_order_) {
        df.addColumn(name, columns_.at(name).take(indices));
    }
    return df;
}

DataFrame DataFrame::where(const Series& condition) const {
    std::vector<size_t> indices;
    for (size_t i = 0; i < std::min(condition.size(), nrows_); ++i) {
        if (condition.getBool(i)) {
            indices.push_back(i);
        }
    }
    return take(indices);
}

DataFrame DataFrame::query(const std::string& expr) const {
    // Simple query parser: "column > value" or "column == value" etc.
    // This is a simplified implementation
    size_t opPos = std::string::npos;
    std::string op;
    
    // Find operator
    static const std::vector<std::string> ops = {">=", "<=", "!=", "==", ">", "<"};
    for (const auto& candidate : ops) {
        opPos = expr.find(candidate);
        if (opPos != std::string::npos) {
            op = candidate;
            break;
        }
    }
    
    if (opPos == std::string::npos) {
        return copy();  // No valid expression
    }
    
    std::string colName = expr.substr(0, opPos);
    std::string valueStr = expr.substr(opPos + op.length());
    
    // Trim whitespace
    auto trim = [](std::string& s) {
        s.erase(0, s.find_first_not_of(" \t"));
        s.erase(s.find_last_not_of(" \t") + 1);
    };
    trim(colName);
    trim(valueStr);
    
    if (!hasColumn(colName)) {
        return copy();
    }
    
    const Series& col = columns_.at(colName);
    double value = std::stod(valueStr);
    
    Series condition("cond");
    if (op == "==") condition = col.eq(value);
    else if (op == "!=") condition = col.ne(value);
    else if (op == "<") condition = col.lt(value);
    else if (op == "<=") condition = col.le(value);
    else if (op == ">") condition = col.gt(value);
    else if (op == ">=") condition = col.ge(value);
    else return copy();
    
    return where(condition);
}

// ============================================================================
// Sorting
// ============================================================================

DataFrame DataFrame::sortBy(const std::string& column, bool ascending) const {
    if (!hasColumn(column)) return copy();
    
    std::vector<size_t> indices = columns_.at(column).argsort(ascending);
    return take(indices);
}

DataFrame DataFrame::sortBy(const std::vector<std::string>& columns,
                             const std::vector<bool>& ascending) const {
    if (columns.empty()) return copy();
    
    std::vector<size_t> indices(nrows_);
    std::iota(indices.begin(), indices.end(), 0);
    
    auto cmp = [&](size_t a, size_t b) {
        for (size_t i = 0; i < columns.size(); ++i) {
            const std::string& colName = columns[i];
            if (!hasColumn(colName)) continue;
            
            const Series& col = columns_.at(colName);
            bool asc = i < ascending.size() ? ascending[i] : true;
            
            double va = col.getDouble(a);
            double vb = col.getDouble(b);
            
            if (std::isnan(va) && std::isnan(vb)) continue;
            if (std::isnan(va)) return false;
            if (std::isnan(vb)) return true;
            
            if (va != vb) {
                return asc ? (va < vb) : (va > vb);
            }
        }
        return false;
    };
    
    std::stable_sort(indices.begin(), indices.end(), cmp);
    return take(indices);
}

// ============================================================================
// Aggregation
// ============================================================================

std::unordered_map<std::string, double> DataFrame::sum() const {
    std::unordered_map<std::string, double> result;
    for (const auto& name : column_order_) {
        result[name] = columns_.at(name).sum();
    }
    return result;
}

std::unordered_map<std::string, double> DataFrame::mean() const {
    std::unordered_map<std::string, double> result;
    for (const auto& name : column_order_) {
        result[name] = columns_.at(name).mean();
    }
    return result;
}

std::unordered_map<std::string, double> DataFrame::min() const {
    std::unordered_map<std::string, double> result;
    for (const auto& name : column_order_) {
        result[name] = columns_.at(name).min();
    }
    return result;
}

std::unordered_map<std::string, double> DataFrame::max() const {
    std::unordered_map<std::string, double> result;
    for (const auto& name : column_order_) {
        result[name] = columns_.at(name).max();
    }
    return result;
}

std::unordered_map<std::string, double> DataFrame::std() const {
    std::unordered_map<std::string, double> result;
    for (const auto& name : column_order_) {
        result[name] = columns_.at(name).std();
    }
    return result;
}

std::unordered_map<std::string, double> DataFrame::var() const {
    std::unordered_map<std::string, double> result;
    for (const auto& name : column_order_) {
        result[name] = columns_.at(name).var();
    }
    return result;
}

std::unordered_map<std::string, int64_t> DataFrame::count() const {
    std::unordered_map<std::string, int64_t> result;
    for (const auto& name : column_order_) {
        result[name] = columns_.at(name).count();
    }
    return result;
}

DataFrame DataFrame::describe() const {
    // Stats: count, mean, std, min, 25%, 50%, 75%, max
    std::vector<std::string> statNames = {"count", "mean", "std", "min", "25%", "50%", "75%", "max"};
    
    DataFrame desc;
    std::vector<std::string> indexVals = statNames;
    
    for (const auto& name : column_order_) {
        const Series& col = columns_.at(name);
        if (col.dtype() == DType::String) continue;  // Skip string columns
        
        std::vector<double> stats = {
            static_cast<double>(col.count()),
            col.mean(),
            col.std(),
            col.min(),
            col.quantile(0.25),
            col.quantile(0.50),
            col.quantile(0.75),
            col.max()
        };
        desc.addColumn(name, Series(name, stats));
    }
    
    return desc;
}

// ============================================================================
// GroupBy Implementation
// ============================================================================

DataFrame::GroupBy::GroupBy(const DataFrame& df, const std::vector<std::string>& keys)
    : df_(df), keys_(keys) {
    buildGroups();
}

void DataFrame::GroupBy::buildGroups() {
    for (size_t i = 0; i < df_.numRows(); ++i) {
        std::string key = makeGroupKey(i);
        groups_[key].push_back(i);
    }
}

std::string DataFrame::GroupBy::makeGroupKey(size_t rowIdx) const {
    std::ostringstream oss;
    for (size_t i = 0; i < keys_.size(); ++i) {
        if (i > 0) oss << "|";
        if (df_.hasColumn(keys_[i])) {
            oss << df_.columns_.at(keys_[i]).getString(rowIdx);
        }
    }
    return oss.str();
}

DataFrame DataFrame::GroupBy::sum() const {
    return agg({{"*", "sum"}});
}

DataFrame DataFrame::GroupBy::mean() const {
    return agg({{"*", "mean"}});
}

DataFrame DataFrame::GroupBy::min() const {
    return agg({{"*", "min"}});
}

DataFrame DataFrame::GroupBy::max() const {
    return agg({{"*", "max"}});
}

DataFrame DataFrame::GroupBy::count() const {
    return agg({{"*", "count"}});
}

DataFrame DataFrame::GroupBy::first() const {
    return agg({{"*", "first"}});
}

DataFrame DataFrame::GroupBy::last() const {
    return agg({{"*", "last"}});
}

DataFrame DataFrame::GroupBy::agg(const std::unordered_map<std::string, std::string>& aggFuncs) const {
    DataFrame result;
    
    // Get columns to aggregate
    std::set<std::string> keySet(keys_.begin(), keys_.end());
    std::vector<std::string> aggCols;
    for (const auto& name : df_.columnNames()) {
        if (keySet.find(name) == keySet.end()) {
            aggCols.push_back(name);
        }
    }
    
    // Initialize result columns
    std::vector<std::vector<std::string>> keyValues(keys_.size());
    std::unordered_map<std::string, std::vector<double>> aggValues;
    for (const auto& col : aggCols) {
        aggValues[col] = std::vector<double>();
    }
    
    // Process each group
    for (const auto& [groupKey, indices] : groups_) {
        // Extract key values from first row
        size_t firstIdx = indices[0];
        for (size_t k = 0; k < keys_.size(); ++k) {
            keyValues[k].push_back(df_.columns_.at(keys_[k]).getString(firstIdx));
        }
        
        // Aggregate each column
        for (const auto& colName : aggCols) {
            const Series& col = df_.columns_.at(colName);
            Series subset = col.take(indices);
            
            std::string func = "sum";  // default
            if (aggFuncs.find(colName) != aggFuncs.end()) {
                func = aggFuncs.at(colName);
            } else if (aggFuncs.find("*") != aggFuncs.end()) {
                func = aggFuncs.at("*");
            }
            
            double aggVal;
            if (func == "sum") aggVal = subset.sum();
            else if (func == "mean") aggVal = subset.mean();
            else if (func == "min") aggVal = subset.min();
            else if (func == "max") aggVal = subset.max();
            else if (func == "std") aggVal = subset.std();
            else if (func == "var") aggVal = subset.var();
            else if (func == "count") aggVal = static_cast<double>(subset.count());
            else if (func == "first") aggVal = subset.getDouble(0);
            else if (func == "last") aggVal = subset.getDouble(subset.size() - 1);
            else aggVal = subset.sum();
            
            aggValues[colName].push_back(aggVal);
        }
    }
    
    // Build result DataFrame
    for (size_t k = 0; k < keys_.size(); ++k) {
        result.addColumn(keys_[k], Series::fromStrings(keys_[k], keyValues[k]));
    }
    for (const auto& colName : aggCols) {
        result.addColumn(colName, Series::fromDoubles(colName, aggValues[colName]));
    }
    
    return result;
}

DataFrame::GroupBy DataFrame::groupby(const std::string& column) const {
    return GroupBy(*this, {column});
}

DataFrame::GroupBy DataFrame::groupby(const std::vector<std::string>& columns) const {
    return GroupBy(*this, columns);
}

// ============================================================================
// Join Operations
// ============================================================================

DataFrame DataFrame::merge(const DataFrame& other, const std::string& on,
                           const std::string& how) const {
    return merge(other, on, on, how);
}

DataFrame DataFrame::merge(const DataFrame& other,
                           const std::string& leftOn, const std::string& rightOn,
                           const std::string& how) const {
    if (!hasColumn(leftOn) || !other.hasColumn(rightOn)) {
        return copy();
    }
    
    DataFrame result;
    const Series& leftKey = columns_.at(leftOn);
    const Series& rightKey = other.columns_.at(rightOn);
    
    // Build hash index for right table
    std::unordered_multimap<std::string, size_t> rightIndex;
    for (size_t i = 0; i < rightKey.size(); ++i) {
        rightIndex.insert({rightKey.getString(i), i});
    }
    
    std::vector<std::pair<int64_t, int64_t>> matches;  // (left_idx, right_idx), -1 for no match
    
    for (size_t i = 0; i < leftKey.size(); ++i) {
        std::string key = leftKey.getString(i);
        auto range = rightIndex.equal_range(key);
        
        if (range.first == range.second) {
            // No match
            if (how == "left" || how == "outer") {
                matches.push_back({static_cast<int64_t>(i), -1});
            }
        } else {
            for (auto it = range.first; it != range.second; ++it) {
                matches.push_back({static_cast<int64_t>(i), static_cast<int64_t>(it->second)});
            }
        }
    }
    
    // Handle right/outer join - add unmatched right rows
    if (how == "right" || how == "outer") {
        std::set<size_t> matchedRight;
        for (const auto& [l, r] : matches) {
            if (r >= 0) matchedRight.insert(r);
        }
        for (size_t i = 0; i < rightKey.size(); ++i) {
            if (matchedRight.find(i) == matchedRight.end()) {
                matches.push_back({-1, static_cast<int64_t>(i)});
            }
        }
    }
    
    // Build result columns
    for (const auto& name : column_order_) {
        const Series& col = columns_.at(name);
        std::vector<double> values;
        values.reserve(matches.size());
        
        for (const auto& [l, r] : matches) {
            if (l >= 0) {
                values.push_back(col.getDouble(l));
            } else {
                values.push_back(std::nan(""));
            }
        }
        result.addColumn(name, Series(name, values));
    }
    
    // Add right columns (except join key)
    for (const auto& name : other.column_order_) {
        if (name == rightOn) continue;
        
        std::string newName = name;
        if (result.hasColumn(name)) {
            newName = name + "_right";
        }
        
        const Series& col = other.columns_.at(name);
        std::vector<double> values;
        values.reserve(matches.size());
        
        for (const auto& [l, r] : matches) {
            if (r >= 0) {
                values.push_back(col.getDouble(r));
            } else {
                values.push_back(std::nan(""));
            }
        }
        result.addColumn(newName, Series(newName, values));
    }
    
    return result;
}

DataFrame DataFrame::concat(const DataFrame& other, bool axis0) const {
    if (axis0) {
        // Concatenate rows
        DataFrame result;
        std::set<std::string> allCols;
        for (const auto& name : column_order_) allCols.insert(name);
        for (const auto& name : other.column_order_) allCols.insert(name);
        
        for (const auto& name : allCols) {
            std::vector<double> values;
            values.reserve(nrows_ + other.nrows_);
            
            // Add from this
            if (hasColumn(name)) {
                for (size_t i = 0; i < nrows_; ++i) {
                    values.push_back(columns_.at(name).getDouble(i));
                }
            } else {
                for (size_t i = 0; i < nrows_; ++i) {
                    values.push_back(std::nan(""));
                }
            }
            
            // Add from other
            if (other.hasColumn(name)) {
                for (size_t i = 0; i < other.nrows_; ++i) {
                    values.push_back(other.columns_.at(name).getDouble(i));
                }
            } else {
                for (size_t i = 0; i < other.nrows_; ++i) {
                    values.push_back(std::nan(""));
                }
            }
            
            result.addColumn(name, Series(name, values));
        }
        return result;
    } else {
        // Concatenate columns
        DataFrame result = copy();
        for (const auto& name : other.column_order_) {
            std::string newName = name;
            int suffix = 1;
            while (result.hasColumn(newName)) {
                newName = name + "_" + std::to_string(suffix++);
            }
            result.addColumn(newName, other.columns_.at(name));
        }
        return result;
    }
}

// ============================================================================
// Transformations
// ============================================================================

DataFrame DataFrame::apply(const std::string& column,
                           std::function<double(double)> func) const {
    DataFrame result = copy();
    if (!hasColumn(column)) return result;
    
    const Series& col = columns_.at(column);
    std::vector<double> values(col.size());
    for (size_t i = 0; i < col.size(); ++i) {
        values[i] = func(col.getDouble(i));
    }
    result.columns_[column] = Series(column, values);
    return result;
}

DataFrame DataFrame::applyAll(std::function<double(double)> func) const {
    DataFrame result;
    for (const auto& name : column_order_) {
        const Series& col = columns_.at(name);
        if (col.dtype() == DType::String) {
            result.addColumn(name, col);
            continue;
        }
        
        std::vector<double> values(col.size());
        for (size_t i = 0; i < col.size(); ++i) {
            values[i] = func(col.getDouble(i));
        }
        result.addColumn(name, Series(name, values));
    }
    return result;
}

DataFrame DataFrame::transpose() const {
    DataFrame result;
    
    // Column names become row indices
    std::vector<std::string> rowNames = column_order_;
    
    // Create index column
    result.addColumn("index", Series::fromStrings("index", rowNames));
    
    // Each row becomes a column
    for (size_t i = 0; i < nrows_; ++i) {
        std::vector<double> values;
        values.reserve(column_order_.size());
        for (const auto& name : column_order_) {
            values.push_back(columns_.at(name).getDouble(i));
        }
        result.addColumn("col" + std::to_string(i), 
                        Series("col" + std::to_string(i), values));
    }
    
    return result;
}

DataFrame DataFrame::pivot(const std::string& index, const std::string& columns,
                           const std::string& values) const {
    if (!hasColumn(index) || !hasColumn(columns) || !hasColumn(values)) {
        return DataFrame();
    }
    
    const Series& idxCol = columns_.at(index);
    const Series& colCol = columns_.at(columns);
    const Series& valCol = columns_.at(values);
    
    // Get unique index and column values
    std::vector<std::string> uniqueIdx;
    std::unordered_map<std::string, size_t> idxMap;
    for (size_t i = 0; i < nrows_; ++i) {
        std::string key = idxCol.getString(i);
        if (idxMap.find(key) == idxMap.end()) {
            idxMap[key] = uniqueIdx.size();
            uniqueIdx.push_back(key);
        }
    }
    
    std::vector<std::string> uniqueCol;
    std::unordered_map<std::string, size_t> colMap;
    for (size_t i = 0; i < nrows_; ++i) {
        std::string key = colCol.getString(i);
        if (colMap.find(key) == colMap.end()) {
            colMap[key] = uniqueCol.size();
            uniqueCol.push_back(key);
        }
    }
    
    // Initialize pivot table
    std::vector<std::vector<double>> pivotData(uniqueCol.size(),
        std::vector<double>(uniqueIdx.size(), std::nan("")));
    
    // Fill pivot table
    for (size_t i = 0; i < nrows_; ++i) {
        size_t r = idxMap[idxCol.getString(i)];
        size_t c = colMap[colCol.getString(i)];
        pivotData[c][r] = valCol.getDouble(i);
    }
    
    // Build result
    DataFrame result;
    result.addColumn(index, Series::fromStrings(index, uniqueIdx));
    for (size_t c = 0; c < uniqueCol.size(); ++c) {
        result.addColumn(uniqueCol[c], Series(uniqueCol[c], pivotData[c]));
    }
    
    return result;
}

DataFrame DataFrame::melt(const std::vector<std::string>& idVars,
                          const std::vector<std::string>& valueVars) const {
    DataFrame result;
    
    std::vector<std::string> idData, varData;
    std::vector<double> valData;
    
    for (size_t i = 0; i < nrows_; ++i) {
        for (const auto& varName : valueVars) {
            if (!hasColumn(varName)) continue;
            
            // Build ID string
            std::string idStr;
            for (const auto& idVar : idVars) {
                if (hasColumn(idVar)) {
                    if (!idStr.empty()) idStr += "|";
                    idStr += columns_.at(idVar).getString(i);
                }
            }
            
            idData.push_back(idStr);
            varData.push_back(varName);
            valData.push_back(columns_.at(varName).getDouble(i));
        }
    }
    
    result.addColumn("id", Series::fromStrings("id", idData));
    result.addColumn("variable", Series::fromStrings("variable", varData));
    result.addColumn("value", Series("value", valData));
    
    return result;
}

// ============================================================================
// Missing Data
// ============================================================================

DataFrame DataFrame::dropna(bool any) const {
    std::vector<size_t> validRows;
    
    for (size_t i = 0; i < nrows_; ++i) {
        bool hasNull = false;
        bool allNull = true;
        
        for (const auto& name : column_order_) {
            bool isNull = columns_.at(name).isNull(i);
            if (isNull) hasNull = true;
            else allNull = false;
        }
        
        if (any) {
            if (!hasNull) validRows.push_back(i);
        } else {
            if (!allNull) validRows.push_back(i);
        }
    }
    
    return take(validRows);
}

DataFrame DataFrame::fillna(double value) const {
    DataFrame result;
    for (const auto& name : column_order_) {
        result.addColumn(name, columns_.at(name).fillNull(value));
    }
    return result;
}

DataFrame DataFrame::fillna(const std::unordered_map<std::string, double>& values) const {
    DataFrame result;
    for (const auto& name : column_order_) {
        if (values.find(name) != values.end()) {
            result.addColumn(name, columns_.at(name).fillNull(values.at(name)));
        } else {
            result.addColumn(name, columns_.at(name));
        }
    }
    return result;
}

// ============================================================================
// Correlation and Covariance
// ============================================================================

DataFrame DataFrame::corr() const {
    std::vector<std::string> numericCols;
    for (const auto& name : column_order_) {
        if (columns_.at(name).dtype() != DType::String) {
            numericCols.push_back(name);
        }
    }
    
    DataFrame result;
    result.addColumn("index", Series::fromStrings("index", numericCols));
    
    for (const auto& col1 : numericCols) {
        std::vector<double> corrValues;
        const Series& s1 = columns_.at(col1);
        
        for (const auto& col2 : numericCols) {
            const Series& s2 = columns_.at(col2);
            
            // Calculate Pearson correlation
            double mean1 = s1.mean();
            double mean2 = s2.mean();
            double std1 = s1.std();
            double std2 = s2.std();
            
            if (std1 == 0 || std2 == 0) {
                corrValues.push_back(std::nan(""));
                continue;
            }
            
            double sumProd = 0.0;
            int64_t count = 0;
            for (size_t i = 0; i < nrows_; ++i) {
                if (!s1.isNull(i) && !s2.isNull(i)) {
                    sumProd += (s1.getDouble(i) - mean1) * (s2.getDouble(i) - mean2);
                    count++;
                }
            }
            
            double corr = count > 1 ? sumProd / ((count - 1) * std1 * std2) : std::nan("");
            corrValues.push_back(corr);
        }
        
        result.addColumn(col1, Series(col1, corrValues));
    }
    
    return result;
}

DataFrame DataFrame::cov() const {
    std::vector<std::string> numericCols;
    for (const auto& name : column_order_) {
        if (columns_.at(name).dtype() != DType::String) {
            numericCols.push_back(name);
        }
    }
    
    DataFrame result;
    result.addColumn("index", Series::fromStrings("index", numericCols));
    
    for (const auto& col1 : numericCols) {
        std::vector<double> covValues;
        const Series& s1 = columns_.at(col1);
        
        for (const auto& col2 : numericCols) {
            const Series& s2 = columns_.at(col2);
            
            double mean1 = s1.mean();
            double mean2 = s2.mean();
            
            double sumProd = 0.0;
            int64_t count = 0;
            for (size_t i = 0; i < nrows_; ++i) {
                if (!s1.isNull(i) && !s2.isNull(i)) {
                    sumProd += (s1.getDouble(i) - mean1) * (s2.getDouble(i) - mean2);
                    count++;
                }
            }
            
            double covariance = count > 1 ? sumProd / (count - 1) : std::nan("");
            covValues.push_back(covariance);
        }
        
        result.addColumn(col1, Series(col1, covValues));
    }
    
    return result;
}

// ============================================================================
// I/O
// ============================================================================

std::string DataFrame::toCSV(char delimiter, bool includeHeader) const {
    std::ostringstream oss;
    
    if (includeHeader) {
        for (size_t i = 0; i < column_order_.size(); ++i) {
            if (i > 0) oss << delimiter;
            oss << column_order_[i];
        }
        oss << "\n";
    }
    
    for (size_t row = 0; row < nrows_; ++row) {
        for (size_t col = 0; col < column_order_.size(); ++col) {
            if (col > 0) oss << delimiter;
            const Series& s = columns_.at(column_order_[col]);
            if (!s.isNull(row)) {
                if (s.dtype() == DType::String) {
                    std::string val = s.getString(row);
                    if (val.find(delimiter) != std::string::npos || 
                        val.find('"') != std::string::npos) {
                        // Quote and escape
                        oss << '"';
                        for (char c : val) {
                            if (c == '"') oss << "\"\"";
                            else oss << c;
                        }
                        oss << '"';
                    } else {
                        oss << val;
                    }
                } else {
                    oss << s.getString(row);
                }
            }
        }
        oss << "\n";
    }
    
    return oss.str();
}

std::string DataFrame::toJSON(bool orient_records) const {
    std::ostringstream oss;
    
    if (orient_records) {
        // Array of objects
        oss << "[\n";
        for (size_t row = 0; row < nrows_; ++row) {
            if (row > 0) oss << ",\n";
            oss << "  {";
            for (size_t col = 0; col < column_order_.size(); ++col) {
                if (col > 0) oss << ", ";
                const std::string& name = column_order_[col];
                const Series& s = columns_.at(name);
                oss << "\"" << name << "\": ";
                if (s.isNull(row)) {
                    oss << "null";
                } else if (s.dtype() == DType::String) {
                    oss << "\"" << s.getString(row) << "\"";
                } else if (s.dtype() == DType::Bool) {
                    oss << (s.getBool(row) ? "true" : "false");
                } else {
                    double v = s.getDouble(row);
                    if (std::isnan(v)) oss << "null";
                    else oss << v;
                }
            }
            oss << "}";
        }
        oss << "\n]";
    } else {
        // Object of arrays
        oss << "{\n";
        for (size_t col = 0; col < column_order_.size(); ++col) {
            if (col > 0) oss << ",\n";
            const std::string& name = column_order_[col];
            const Series& s = columns_.at(name);
            oss << "  \"" << name << "\": [";
            for (size_t row = 0; row < nrows_; ++row) {
                if (row > 0) oss << ", ";
                if (s.isNull(row)) {
                    oss << "null";
                } else if (s.dtype() == DType::String) {
                    oss << "\"" << s.getString(row) << "\"";
                } else if (s.dtype() == DType::Bool) {
                    oss << (s.getBool(row) ? "true" : "false");
                } else {
                    double v = s.getDouble(row);
                    if (std::isnan(v)) oss << "null";
                    else oss << v;
                }
            }
            oss << "]";
        }
        oss << "\n}";
    }
    
    return oss.str();
}

std::string DataFrame::repr(size_t maxRows, size_t maxCols) const {
    std::ostringstream oss;
    oss << "DataFrame(" << nrows_ << " rows x " << column_order_.size() << " columns)\n";
    
    // Determine columns to show
    size_t showCols = std::min(maxCols, column_order_.size());
    size_t showRows = std::min(maxRows, nrows_);
    
    // Calculate column widths
    std::vector<size_t> widths(showCols);
    for (size_t c = 0; c < showCols; ++c) {
        widths[c] = column_order_[c].length();
        const Series& col = columns_.at(column_order_[c]);
        for (size_t r = 0; r < showRows; ++r) {
            widths[c] = std::max(widths[c], col.getString(r).length());
        }
        widths[c] = std::min(widths[c], size_t(20));  // Cap width
    }
    
    // Header
    oss << std::setw(8) << "" << " ";
    for (size_t c = 0; c < showCols; ++c) {
        oss << std::setw(widths[c]) << column_order_[c].substr(0, widths[c]) << " ";
    }
    if (showCols < column_order_.size()) oss << "...";
    oss << "\n";
    
    // Separator
    oss << std::string(8, '-') << " ";
    for (size_t c = 0; c < showCols; ++c) {
        oss << std::string(widths[c], '-') << " ";
    }
    oss << "\n";
    
    // Data rows
    for (size_t r = 0; r < showRows; ++r) {
        oss << std::setw(8) << r << " ";
        for (size_t c = 0; c < showCols; ++c) {
            const Series& col = columns_.at(column_order_[c]);
            std::string val = col.isNull(r) ? "null" : col.getString(r);
            if (val.length() > widths[c]) {
                val = val.substr(0, widths[c] - 2) + "..";
            }
            oss << std::setw(widths[c]) << val << " ";
        }
        if (showCols < column_order_.size()) oss << "...";
        oss << "\n";
    }
    
    if (showRows < nrows_) {
        oss << "... (" << (nrows_ - showRows) << " more rows)\n";
    }
    
    return oss.str();
}

// ============================================================================
// Copy
// ============================================================================

DataFrame DataFrame::copy() const {
    DataFrame df;
    df.nrows_ = nrows_;
    df.column_order_ = column_order_;
    for (const auto& [name, series] : columns_) {
        df.columns_[name] = series.copy();
    }
    return df;
}

// ============================================================================
// Memory Info
// ============================================================================

size_t DataFrame::memoryUsage() const {
    size_t total = sizeof(DataFrame);
    total += column_order_.capacity() * sizeof(std::string);
    
    for (const auto& [name, series] : columns_) {
        total += name.capacity();
        total += series.size() * sizeof(double);  // Approximate
    }
    
    return total;
}

// ============================================================================
// Private Helpers
// ============================================================================

void DataFrame::validateColumnSize(const Series& series) const {
    if (!columns_.empty() && series.size() != nrows_) {
        throw std::runtime_error("Series size mismatch");
    }
}

void DataFrame::updateRowCount() {
    if (columns_.empty()) {
        nrows_ = 0;
    } else {
        nrows_ = columns_.begin()->second.size();
    }
}

// ============================================================================
// DataFrame Registry Implementation
// ============================================================================

size_t DataFrameRegistry::store(DataFrame df) {
    size_t id = nextDfId_++;
    dataframes_[id] = std::move(df);
    return id;
}

DataFrame* DataFrameRegistry::get(size_t id) {
    auto it = dataframes_.find(id);
    return it != dataframes_.end() ? &it->second : nullptr;
}

const DataFrame* DataFrameRegistry::get(size_t id) const {
    auto it = dataframes_.find(id);
    return it != dataframes_.end() ? &it->second : nullptr;
}

void DataFrameRegistry::remove(size_t id) {
    dataframes_.erase(id);
}

void DataFrameRegistry::clear() {
    dataframes_.clear();
}

size_t DataFrameRegistry::storeSeries(Series series) {
    size_t id = nextSeriesId_++;
    series_[id] = std::move(series);
    return id;
}

Series* DataFrameRegistry::getSeries(size_t id) {
    auto it = series_.find(id);
    return it != series_.end() ? &it->second : nullptr;
}

const Series* DataFrameRegistry::getSeries(size_t id) const {
    auto it = series_.find(id);
    return it != series_.end() ? &it->second : nullptr;
}

void DataFrameRegistry::removeSeries(size_t id) {
    series_.erase(id);
}

}  // namespace qz_data
