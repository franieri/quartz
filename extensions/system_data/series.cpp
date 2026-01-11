#include "dataframe.h"
#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm>
#include <unordered_set>

namespace qz_data {

// ============================================================================
// Series Implementation
// ============================================================================

Series::Series(const std::string& name, const std::vector<double>& data)
    : dtype_(DType::Float64), name_(name), size_(data.size()), float64_data_(data) {
    initNullBitmap(size_);
    // Mark all as valid
    for (size_t i = 0; i < size_; ++i) {
        setNullBit(i, !std::isnan(data[i]));
    }
}

Series::Series(const std::string& name, const std::vector<int64_t>& data)
    : dtype_(DType::Int64), name_(name), size_(data.size()), int64_data_(data) {
    initNullBitmap(size_);
    for (size_t i = 0; i < size_; ++i) {
        setNullBit(i, true);  // All valid for int
    }
}

Series::Series(const std::string& name, const std::vector<std::string>& data)
    : dtype_(DType::String), name_(name), size_(data.size()), string_data_(data) {
    initNullBitmap(size_);
    for (size_t i = 0; i < size_; ++i) {
        setNullBit(i, true);  // All valid
    }
}

Series::Series(const std::string& name, const std::vector<bool>& data)
    : dtype_(DType::Bool), name_(name), size_(data.size()) {
    bool_data_.resize((size_ + 7) / 8, 0);
    for (size_t i = 0; i < size_; ++i) {
        if (data[i]) {
            bool_data_[i / 8] |= (1u << (i % 8));
        }
    }
    initNullBitmap(size_);
    for (size_t i = 0; i < size_; ++i) {
        setNullBit(i, true);
    }
}

Series Series::fromDoubles(const std::string& name, const std::vector<double>& data) {
    return Series(name, data);
}

Series Series::fromInts(const std::string& name, const std::vector<int64_t>& data) {
    return Series(name, data);
}

Series Series::fromStrings(const std::string& name, const std::vector<std::string>& data) {
    return Series(name, data);
}

Series Series::fromBools(const std::string& name, const std::vector<bool>& data) {
    return Series(name, data);
}

Series Series::range(const std::string& name, int64_t start, int64_t end, int64_t step) {
    std::vector<int64_t> data;
    data.reserve((end - start) / step + 1);
    for (int64_t i = start; i < end; i += step) {
        data.push_back(i);
    }
    return Series(name, data);
}

Series Series::zeros(const std::string& name, size_t count) {
    return Series(name, std::vector<double>(count, 0.0));
}

Series Series::ones(const std::string& name, size_t count) {
    return Series(name, std::vector<double>(count, 1.0));
}

Series Series::constant(const std::string& name, double value, size_t count) {
    return Series(name, std::vector<double>(count, value));
}

// ============================================================================
// Null Bitmap Management
// ============================================================================

void Series::initNullBitmap(size_t size) {
    null_bitmap_.resize((size + 63) / 64, ~0ULL);  // All bits set = all valid
}

void Series::setNullBit(size_t idx, bool isValid) {
    if (idx >= size_) return;
    size_t word = idx / 64;
    size_t bit = idx % 64;
    if (isValid) {
        null_bitmap_[word] |= (1ULL << bit);
    } else {
        null_bitmap_[word] &= ~(1ULL << bit);
    }
}

bool Series::getNullBit(size_t idx) const {
    if (idx >= size_) return false;
    size_t word = idx / 64;
    size_t bit = idx % 64;
    return (null_bitmap_[word] & (1ULL << bit)) != 0;
}

bool Series::isNull(size_t idx) const {
    return !getNullBit(idx);
}

// ============================================================================
// Element Access
// ============================================================================

double Series::getDouble(size_t idx) const {
    if (idx >= size_ || isNull(idx)) return std::nan("");
    switch (dtype_) {
        case DType::Float64:
        case DType::Float32:
            return float64_data_[idx];
        case DType::Int64:
        case DType::Int32:
            return static_cast<double>(int64_data_[idx]);
        case DType::Bool:
            return (bool_data_[idx / 8] & (1u << (idx % 8))) ? 1.0 : 0.0;
        default:
            return std::nan("");
    }
}

int64_t Series::getInt(size_t idx) const {
    if (idx >= size_ || isNull(idx)) return 0;
    switch (dtype_) {
        case DType::Int64:
        case DType::Int32:
            return int64_data_[idx];
        case DType::Float64:
        case DType::Float32:
            return static_cast<int64_t>(float64_data_[idx]);
        case DType::Bool:
            return (bool_data_[idx / 8] & (1u << (idx % 8))) ? 1 : 0;
        default:
            return 0;
    }
}

std::string Series::getString(size_t idx) const {
    if (idx >= size_ || isNull(idx)) return "";
    if (dtype_ == DType::String) {
        return string_data_[idx];
    }
    // Convert numeric to string
    std::ostringstream oss;
    switch (dtype_) {
        case DType::Float64:
        case DType::Float32:
            oss << float64_data_[idx];
            break;
        case DType::Int64:
        case DType::Int32:
            oss << int64_data_[idx];
            break;
        case DType::Bool:
            oss << ((bool_data_[idx / 8] & (1u << (idx % 8))) ? "true" : "false");
            break;
        default:
            break;
    }
    return oss.str();
}

bool Series::getBool(size_t idx) const {
    if (idx >= size_ || isNull(idx)) return false;
    if (dtype_ == DType::Bool) {
        return (bool_data_[idx / 8] & (1u << (idx % 8))) != 0;
    }
    return getDouble(idx) != 0.0;
}

// ============================================================================
// Mutators
// ============================================================================

void Series::setDouble(size_t idx, double val) {
    if (idx >= size_) return;
    if (dtype_ == DType::Float64 || dtype_ == DType::Float32) {
        float64_data_[idx] = val;
        setNullBit(idx, !std::isnan(val));
    }
}

void Series::setInt(size_t idx, int64_t val) {
    if (idx >= size_) return;
    if (dtype_ == DType::Int64 || dtype_ == DType::Int32) {
        int64_data_[idx] = val;
        setNullBit(idx, true);
    }
}

void Series::setString(size_t idx, const std::string& val) {
    if (idx >= size_) return;
    if (dtype_ == DType::String) {
        string_data_[idx] = val;
        setNullBit(idx, true);
    }
}

void Series::setBool(size_t idx, bool val) {
    if (idx >= size_) return;
    if (dtype_ == DType::Bool) {
        if (val) {
            bool_data_[idx / 8] |= (1u << (idx % 8));
        } else {
            bool_data_[idx / 8] &= ~(1u << (idx % 8));
        }
        setNullBit(idx, true);
    }
}

void Series::setNull(size_t idx) {
    setNullBit(idx, false);
}

void Series::pushDouble(double val) {
    if (dtype_ == DType::Null) dtype_ = DType::Float64;
    if (dtype_ == DType::Float64 || dtype_ == DType::Float32) {
        float64_data_.push_back(val);
        size_++;
        if (size_ > null_bitmap_.size() * 64) {
            null_bitmap_.push_back(~0ULL);
        }
        setNullBit(size_ - 1, !std::isnan(val));
    }
}

void Series::pushInt(int64_t val) {
    if (dtype_ == DType::Null) dtype_ = DType::Int64;
    if (dtype_ == DType::Int64 || dtype_ == DType::Int32) {
        int64_data_.push_back(val);
        size_++;
        if (size_ > null_bitmap_.size() * 64) {
            null_bitmap_.push_back(~0ULL);
        }
        setNullBit(size_ - 1, true);
    }
}

void Series::pushString(const std::string& val) {
    if (dtype_ == DType::Null) dtype_ = DType::String;
    if (dtype_ == DType::String) {
        string_data_.push_back(val);
        size_++;
        if (size_ > null_bitmap_.size() * 64) {
            null_bitmap_.push_back(~0ULL);
        }
        setNullBit(size_ - 1, true);
    }
}

void Series::pushBool(bool val) {
    if (dtype_ == DType::Null) dtype_ = DType::Bool;
    if (dtype_ == DType::Bool) {
        size_++;
        if ((size_ + 7) / 8 > bool_data_.size()) {
            bool_data_.push_back(0);
        }
        if (val) {
            bool_data_[(size_ - 1) / 8] |= (1u << ((size_ - 1) % 8));
        }
        if (size_ > null_bitmap_.size() * 64) {
            null_bitmap_.push_back(~0ULL);
        }
        setNullBit(size_ - 1, true);
    }
}

void Series::reserve(size_t capacity) {
    switch (dtype_) {
        case DType::Float64:
        case DType::Float32:
            float64_data_.reserve(capacity);
            break;
        case DType::Int64:
        case DType::Int32:
            int64_data_.reserve(capacity);
            break;
        case DType::String:
            string_data_.reserve(capacity);
            break;
        case DType::Bool:
            bool_data_.reserve((capacity + 7) / 8);
            break;
        default:
            break;
    }
    null_bitmap_.reserve((capacity + 63) / 64);
}

// ============================================================================
// NULL Handling
// ============================================================================

size_t Series::nullCount() const {
    size_t count = 0;
    for (size_t i = 0; i < size_; ++i) {
        if (isNull(i)) count++;
    }
    return count;
}

std::vector<size_t> Series::nullIndices() const {
    std::vector<size_t> indices;
    for (size_t i = 0; i < size_; ++i) {
        if (isNull(i)) indices.push_back(i);
    }
    return indices;
}

Series Series::dropNull() const {
    std::vector<size_t> validIndices;
    for (size_t i = 0; i < size_; ++i) {
        if (!isNull(i)) validIndices.push_back(i);
    }
    return take(validIndices);
}

Series Series::fillNull(double value) const {
    Series result = copy();
    for (size_t i = 0; i < size_; ++i) {
        if (isNull(i)) {
            result.setDouble(i, value);
            result.setNullBit(i, true);
        }
    }
    return result;
}

Series Series::fillNullForward() const {
    Series result = copy();
    double lastValid = std::nan("");
    for (size_t i = 0; i < size_; ++i) {
        if (!isNull(i)) {
            lastValid = getDouble(i);
        } else if (!std::isnan(lastValid)) {
            result.setDouble(i, lastValid);
            result.setNullBit(i, true);
        }
    }
    return result;
}

Series Series::fillNullBackward() const {
    Series result = copy();
    double lastValid = std::nan("");
    for (size_t i = size_; i > 0; --i) {
        size_t idx = i - 1;
        if (!isNull(idx)) {
            lastValid = getDouble(idx);
        } else if (!std::isnan(lastValid)) {
            result.setDouble(idx, lastValid);
            result.setNullBit(idx, true);
        }
    }
    return result;
}

// ============================================================================
// SIMD Helper Functions
// ============================================================================

#if defined(__AVX2__) || defined(__AVX__)
double Series::simdSum(const double* data, size_t n) {
    __m256d sum = _mm256_setzero_pd();
    size_t i = 0;
    
    // Process 4 doubles at a time
    for (; i + 4 <= n; i += 4) {
        __m256d v = _mm256_loadu_pd(data + i);
        sum = _mm256_add_pd(sum, v);
    }
    
    // Horizontal sum
    __m128d lo = _mm256_extractf128_pd(sum, 0);
    __m128d hi = _mm256_extractf128_pd(sum, 1);
    lo = _mm_add_pd(lo, hi);
    __m128d shuf = _mm_shuffle_pd(lo, lo, 1);
    lo = _mm_add_sd(lo, shuf);
    double result = _mm_cvtsd_f64(lo);
    
    // Handle remaining elements
    for (; i < n; ++i) {
        result += data[i];
    }
    return result;
}

void Series::simdAdd(const double* a, const double* b, double* out, size_t n) {
    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        __m256d va = _mm256_loadu_pd(a + i);
        __m256d vb = _mm256_loadu_pd(b + i);
        __m256d vr = _mm256_add_pd(va, vb);
        _mm256_storeu_pd(out + i, vr);
    }
    for (; i < n; ++i) {
        out[i] = a[i] + b[i];
    }
}

void Series::simdMul(const double* a, const double* b, double* out, size_t n) {
    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        __m256d va = _mm256_loadu_pd(a + i);
        __m256d vb = _mm256_loadu_pd(b + i);
        __m256d vr = _mm256_mul_pd(va, vb);
        _mm256_storeu_pd(out + i, vr);
    }
    for (; i < n; ++i) {
        out[i] = a[i] * b[i];
    }
}

void Series::simdScalarAdd(const double* a, double scalar, double* out, size_t n) {
    __m256d vs = _mm256_set1_pd(scalar);
    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        __m256d va = _mm256_loadu_pd(a + i);
        __m256d vr = _mm256_add_pd(va, vs);
        _mm256_storeu_pd(out + i, vr);
    }
    for (; i < n; ++i) {
        out[i] = a[i] + scalar;
    }
}

void Series::simdScalarMul(const double* a, double scalar, double* out, size_t n) {
    __m256d vs = _mm256_set1_pd(scalar);
    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        __m256d va = _mm256_loadu_pd(a + i);
        __m256d vr = _mm256_mul_pd(va, vs);
        _mm256_storeu_pd(out + i, vr);
    }
    for (; i < n; ++i) {
        out[i] = a[i] * scalar;
    }
}

#else
// Fallback for non-AVX systems
double Series::simdSum(const double* data, size_t n) {
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        sum += data[i];
    }
    return sum;
}

void Series::simdAdd(const double* a, const double* b, double* out, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        out[i] = a[i] + b[i];
    }
}

void Series::simdMul(const double* a, const double* b, double* out, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        out[i] = a[i] * b[i];
    }
}

void Series::simdScalarAdd(const double* a, double scalar, double* out, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        out[i] = a[i] + scalar;
    }
}

void Series::simdScalarMul(const double* a, double scalar, double* out, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        out[i] = a[i] * scalar;
    }
}
#endif

// ============================================================================
// Aggregation Functions
// ============================================================================

double Series::sum() const {
    if (dtype_ == DType::Float64 || dtype_ == DType::Float32) {
        // Use SIMD for bulk sum, then handle nulls
        double total = 0.0;
        for (size_t i = 0; i < size_; ++i) {
            if (!isNull(i)) {
                total += float64_data_[i];
            }
        }
        return total;
    } else if (dtype_ == DType::Int64 || dtype_ == DType::Int32) {
        int64_t total = 0;
        for (size_t i = 0; i < size_; ++i) {
            if (!isNull(i)) {
                total += int64_data_[i];
            }
        }
        return static_cast<double>(total);
    }
    return 0.0;
}

double Series::mean() const {
    int64_t validCount = count();
    if (validCount == 0) return std::nan("");
    return sum() / static_cast<double>(validCount);
}

double Series::min() const {
    double minVal = std::numeric_limits<double>::infinity();
    bool found = false;
    for (size_t i = 0; i < size_; ++i) {
        if (!isNull(i)) {
            double v = getDouble(i);
            if (v < minVal) {
                minVal = v;
                found = true;
            }
        }
    }
    return found ? minVal : std::nan("");
}

double Series::max() const {
    double maxVal = -std::numeric_limits<double>::infinity();
    bool found = false;
    for (size_t i = 0; i < size_; ++i) {
        if (!isNull(i)) {
            double v = getDouble(i);
            if (v > maxVal) {
                maxVal = v;
                found = true;
            }
        }
    }
    return found ? maxVal : std::nan("");
}

double Series::var() const {
    double m = mean();
    if (std::isnan(m)) return std::nan("");
    
    double sumSq = 0.0;
    int64_t validCount = 0;
    for (size_t i = 0; i < size_; ++i) {
        if (!isNull(i)) {
            double diff = getDouble(i) - m;
            sumSq += diff * diff;
            validCount++;
        }
    }
    return validCount > 1 ? sumSq / (validCount - 1) : 0.0;
}

double Series::std() const {
    return std::sqrt(var());
}

double Series::median() const {
    return quantile(0.5);
}

double Series::quantile(double q) const {
    if (q < 0.0 || q > 1.0) return std::nan("");
    
    std::vector<double> valid;
    valid.reserve(size_);
    for (size_t i = 0; i < size_; ++i) {
        if (!isNull(i)) {
            valid.push_back(getDouble(i));
        }
    }
    
    if (valid.empty()) return std::nan("");
    
    std::sort(valid.begin(), valid.end());
    
    double pos = q * (valid.size() - 1);
    size_t lo = static_cast<size_t>(pos);
    size_t hi = lo + 1;
    double frac = pos - lo;
    
    if (hi >= valid.size()) return valid[lo];
    return valid[lo] * (1.0 - frac) + valid[hi] * frac;
}

int64_t Series::count() const {
    int64_t cnt = 0;
    for (size_t i = 0; i < size_; ++i) {
        if (!isNull(i)) cnt++;
    }
    return cnt;
}

// ============================================================================
// Element-wise Operations
// ============================================================================

Series Series::add(const Series& other) const {
    size_t n = std::min(size_, other.size_);
    std::vector<double> result(n);
    
    if (dtype_ == DType::Float64 && other.dtype_ == DType::Float64) {
        simdAdd(float64_data_.data(), other.float64_data_.data(), result.data(), n);
    } else {
        for (size_t i = 0; i < n; ++i) {
            result[i] = getDouble(i) + other.getDouble(i);
        }
    }
    
    Series s(name_, result);
    // Mark nulls where either operand was null
    for (size_t i = 0; i < n; ++i) {
        s.setNullBit(i, !isNull(i) && !other.isNull(i));
    }
    return s;
}

Series Series::add(double scalar) const {
    std::vector<double> result(size_);
    
    if (dtype_ == DType::Float64) {
        simdScalarAdd(float64_data_.data(), scalar, result.data(), size_);
    } else {
        for (size_t i = 0; i < size_; ++i) {
            result[i] = getDouble(i) + scalar;
        }
    }
    
    Series s(name_, result);
    for (size_t i = 0; i < size_; ++i) {
        s.setNullBit(i, !isNull(i));
    }
    return s;
}

Series Series::sub(const Series& other) const {
    size_t n = std::min(size_, other.size_);
    std::vector<double> result(n);
    for (size_t i = 0; i < n; ++i) {
        result[i] = getDouble(i) - other.getDouble(i);
    }
    Series s(name_, result);
    for (size_t i = 0; i < n; ++i) {
        s.setNullBit(i, !isNull(i) && !other.isNull(i));
    }
    return s;
}

Series Series::sub(double scalar) const {
    return add(-scalar);
}

Series Series::mul(const Series& other) const {
    size_t n = std::min(size_, other.size_);
    std::vector<double> result(n);
    
    if (dtype_ == DType::Float64 && other.dtype_ == DType::Float64) {
        simdMul(float64_data_.data(), other.float64_data_.data(), result.data(), n);
    } else {
        for (size_t i = 0; i < n; ++i) {
            result[i] = getDouble(i) * other.getDouble(i);
        }
    }
    
    Series s(name_, result);
    for (size_t i = 0; i < n; ++i) {
        s.setNullBit(i, !isNull(i) && !other.isNull(i));
    }
    return s;
}

Series Series::mul(double scalar) const {
    std::vector<double> result(size_);
    
    if (dtype_ == DType::Float64) {
        simdScalarMul(float64_data_.data(), scalar, result.data(), size_);
    } else {
        for (size_t i = 0; i < size_; ++i) {
            result[i] = getDouble(i) * scalar;
        }
    }
    
    Series s(name_, result);
    for (size_t i = 0; i < size_; ++i) {
        s.setNullBit(i, !isNull(i));
    }
    return s;
}

Series Series::div(const Series& other) const {
    size_t n = std::min(size_, other.size_);
    std::vector<double> result(n);
    for (size_t i = 0; i < n; ++i) {
        double b = other.getDouble(i);
        result[i] = (b != 0.0) ? getDouble(i) / b : std::nan("");
    }
    Series s(name_, result);
    for (size_t i = 0; i < n; ++i) {
        s.setNullBit(i, !isNull(i) && !other.isNull(i) && other.getDouble(i) != 0.0);
    }
    return s;
}

Series Series::div(double scalar) const {
    if (scalar == 0.0) {
        std::vector<double> result(size_, std::nan(""));
        return Series(name_, result);
    }
    return mul(1.0 / scalar);
}

Series Series::pow(double exponent) const {
    std::vector<double> result(size_);
    for (size_t i = 0; i < size_; ++i) {
        result[i] = std::pow(getDouble(i), exponent);
    }
    Series s(name_, result);
    for (size_t i = 0; i < size_; ++i) {
        s.setNullBit(i, !isNull(i));
    }
    return s;
}

Series Series::sqrt() const {
    std::vector<double> result(size_);
    for (size_t i = 0; i < size_; ++i) {
        result[i] = std::sqrt(getDouble(i));
    }
    Series s(name_, result);
    for (size_t i = 0; i < size_; ++i) {
        s.setNullBit(i, !isNull(i) && getDouble(i) >= 0);
    }
    return s;
}

Series Series::abs() const {
    std::vector<double> result(size_);
    for (size_t i = 0; i < size_; ++i) {
        result[i] = std::abs(getDouble(i));
    }
    Series s(name_, result);
    for (size_t i = 0; i < size_; ++i) {
        s.setNullBit(i, !isNull(i));
    }
    return s;
}

Series Series::neg() const {
    return mul(-1.0);
}

Series Series::log() const {
    std::vector<double> result(size_);
    for (size_t i = 0; i < size_; ++i) {
        result[i] = std::log(getDouble(i));
    }
    Series s(name_, result);
    for (size_t i = 0; i < size_; ++i) {
        s.setNullBit(i, !isNull(i) && getDouble(i) > 0);
    }
    return s;
}

Series Series::log10() const {
    std::vector<double> result(size_);
    for (size_t i = 0; i < size_; ++i) {
        result[i] = std::log10(getDouble(i));
    }
    Series s(name_, result);
    for (size_t i = 0; i < size_; ++i) {
        s.setNullBit(i, !isNull(i) && getDouble(i) > 0);
    }
    return s;
}

Series Series::exp() const {
    std::vector<double> result(size_);
    for (size_t i = 0; i < size_; ++i) {
        result[i] = std::exp(getDouble(i));
    }
    Series s(name_, result);
    for (size_t i = 0; i < size_; ++i) {
        s.setNullBit(i, !isNull(i));
    }
    return s;
}

Series Series::sin() const {
    std::vector<double> result(size_);
    for (size_t i = 0; i < size_; ++i) {
        result[i] = std::sin(getDouble(i));
    }
    Series s(name_, result);
    for (size_t i = 0; i < size_; ++i) {
        s.setNullBit(i, !isNull(i));
    }
    return s;
}

Series Series::cos() const {
    std::vector<double> result(size_);
    for (size_t i = 0; i < size_; ++i) {
        result[i] = std::cos(getDouble(i));
    }
    Series s(name_, result);
    for (size_t i = 0; i < size_; ++i) {
        s.setNullBit(i, !isNull(i));
    }
    return s;
}

Series Series::tan() const {
    std::vector<double> result(size_);
    for (size_t i = 0; i < size_; ++i) {
        result[i] = std::tan(getDouble(i));
    }
    Series s(name_, result);
    for (size_t i = 0; i < size_; ++i) {
        s.setNullBit(i, !isNull(i));
    }
    return s;
}

// ============================================================================
// Comparison Operations
// ============================================================================

Series Series::eq(const Series& other) const {
    size_t n = std::min(size_, other.size_);
    std::vector<bool> result(n);
    for (size_t i = 0; i < n; ++i) {
        result[i] = !isNull(i) && !other.isNull(i) && getDouble(i) == other.getDouble(i);
    }
    return Series::fromBools(name_, result);
}

Series Series::eq(double scalar) const {
    std::vector<bool> result(size_);
    for (size_t i = 0; i < size_; ++i) {
        result[i] = !isNull(i) && getDouble(i) == scalar;
    }
    return Series::fromBools(name_, result);
}

Series Series::ne(const Series& other) const {
    size_t n = std::min(size_, other.size_);
    std::vector<bool> result(n);
    for (size_t i = 0; i < n; ++i) {
        result[i] = isNull(i) || other.isNull(i) || getDouble(i) != other.getDouble(i);
    }
    return Series::fromBools(name_, result);
}

Series Series::ne(double scalar) const {
    std::vector<bool> result(size_);
    for (size_t i = 0; i < size_; ++i) {
        result[i] = isNull(i) || getDouble(i) != scalar;
    }
    return Series::fromBools(name_, result);
}

Series Series::lt(const Series& other) const {
    size_t n = std::min(size_, other.size_);
    std::vector<bool> result(n);
    for (size_t i = 0; i < n; ++i) {
        result[i] = !isNull(i) && !other.isNull(i) && getDouble(i) < other.getDouble(i);
    }
    return Series::fromBools(name_, result);
}

Series Series::lt(double scalar) const {
    std::vector<bool> result(size_);
    for (size_t i = 0; i < size_; ++i) {
        result[i] = !isNull(i) && getDouble(i) < scalar;
    }
    return Series::fromBools(name_, result);
}

Series Series::le(const Series& other) const {
    size_t n = std::min(size_, other.size_);
    std::vector<bool> result(n);
    for (size_t i = 0; i < n; ++i) {
        result[i] = !isNull(i) && !other.isNull(i) && getDouble(i) <= other.getDouble(i);
    }
    return Series::fromBools(name_, result);
}

Series Series::le(double scalar) const {
    std::vector<bool> result(size_);
    for (size_t i = 0; i < size_; ++i) {
        result[i] = !isNull(i) && getDouble(i) <= scalar;
    }
    return Series::fromBools(name_, result);
}

Series Series::gt(const Series& other) const {
    size_t n = std::min(size_, other.size_);
    std::vector<bool> result(n);
    for (size_t i = 0; i < n; ++i) {
        result[i] = !isNull(i) && !other.isNull(i) && getDouble(i) > other.getDouble(i);
    }
    return Series::fromBools(name_, result);
}

Series Series::gt(double scalar) const {
    std::vector<bool> result(size_);
    for (size_t i = 0; i < size_; ++i) {
        result[i] = !isNull(i) && getDouble(i) > scalar;
    }
    return Series::fromBools(name_, result);
}

Series Series::ge(const Series& other) const {
    size_t n = std::min(size_, other.size_);
    std::vector<bool> result(n);
    for (size_t i = 0; i < n; ++i) {
        result[i] = !isNull(i) && !other.isNull(i) && getDouble(i) >= other.getDouble(i);
    }
    return Series::fromBools(name_, result);
}

Series Series::ge(double scalar) const {
    std::vector<bool> result(size_);
    for (size_t i = 0; i < size_; ++i) {
        result[i] = !isNull(i) && getDouble(i) >= scalar;
    }
    return Series::fromBools(name_, result);
}

// ============================================================================
// Logical Operations
// ============================================================================

Series Series::logicalAnd(const Series& other) const {
    size_t n = std::min(size_, other.size_);
    std::vector<bool> result(n);
    for (size_t i = 0; i < n; ++i) {
        result[i] = getBool(i) && other.getBool(i);
    }
    return Series::fromBools(name_, result);
}

Series Series::logicalOr(const Series& other) const {
    size_t n = std::min(size_, other.size_);
    std::vector<bool> result(n);
    for (size_t i = 0; i < n; ++i) {
        result[i] = getBool(i) || other.getBool(i);
    }
    return Series::fromBools(name_, result);
}

Series Series::logicalNot() const {
    std::vector<bool> result(size_);
    for (size_t i = 0; i < size_; ++i) {
        result[i] = !getBool(i);
    }
    return Series::fromBools(name_, result);
}

// ============================================================================
// String Operations
// ============================================================================

Series Series::strLen() const {
    if (dtype_ != DType::String) return Series(name_);
    std::vector<int64_t> result(size_);
    for (size_t i = 0; i < size_; ++i) {
        result[i] = isNull(i) ? 0 : static_cast<int64_t>(string_data_[i].length());
    }
    return Series::fromInts(name_, result);
}

Series Series::strLower() const {
    if (dtype_ != DType::String) return Series(name_);
    std::vector<std::string> result(size_);
    for (size_t i = 0; i < size_; ++i) {
        if (!isNull(i)) {
            result[i] = string_data_[i];
            std::transform(result[i].begin(), result[i].end(), result[i].begin(), ::tolower);
        }
    }
    return Series::fromStrings(name_, result);
}

Series Series::strUpper() const {
    if (dtype_ != DType::String) return Series(name_);
    std::vector<std::string> result(size_);
    for (size_t i = 0; i < size_; ++i) {
        if (!isNull(i)) {
            result[i] = string_data_[i];
            std::transform(result[i].begin(), result[i].end(), result[i].begin(), ::toupper);
        }
    }
    return Series::fromStrings(name_, result);
}

Series Series::strContains(const std::string& pattern) const {
    if (dtype_ != DType::String) return Series(name_);
    std::vector<bool> result(size_);
    for (size_t i = 0; i < size_; ++i) {
        result[i] = !isNull(i) && string_data_[i].find(pattern) != std::string::npos;
    }
    return Series::fromBools(name_, result);
}

Series Series::strReplace(const std::string& from, const std::string& to) const {
    if (dtype_ != DType::String) return Series(name_);
    std::vector<std::string> result(size_);
    for (size_t i = 0; i < size_; ++i) {
        if (!isNull(i)) {
            result[i] = string_data_[i];
            size_t pos = 0;
            while ((pos = result[i].find(from, pos)) != std::string::npos) {
                result[i].replace(pos, from.length(), to);
                pos += to.length();
            }
        }
    }
    return Series::fromStrings(name_, result);
}

Series Series::strSplit(const std::string& delimiter, int index) const {
    if (dtype_ != DType::String) return Series(name_);
    std::vector<std::string> result(size_);
    for (size_t i = 0; i < size_; ++i) {
        if (!isNull(i)) {
            std::string s = string_data_[i];
            std::vector<std::string> parts;
            size_t pos = 0;
            while ((pos = s.find(delimiter)) != std::string::npos) {
                parts.push_back(s.substr(0, pos));
                s.erase(0, pos + delimiter.length());
            }
            parts.push_back(s);
            
            int idx = index < 0 ? static_cast<int>(parts.size()) + index : index;
            if (idx >= 0 && idx < static_cast<int>(parts.size())) {
                result[i] = parts[idx];
            }
        }
    }
    return Series::fromStrings(name_, result);
}

// ============================================================================
// Transformations
// ============================================================================

Series Series::shift(int periods) const {
    std::vector<double> result(size_, std::nan(""));
    for (size_t i = 0; i < size_; ++i) {
        int srcIdx = static_cast<int>(i) - periods;
        if (srcIdx >= 0 && srcIdx < static_cast<int>(size_)) {
            result[i] = getDouble(srcIdx);
        }
    }
    Series s(name_, result);
    for (size_t i = 0; i < size_; ++i) {
        int srcIdx = static_cast<int>(i) - periods;
        if (srcIdx >= 0 && srcIdx < static_cast<int>(size_) && !isNull(srcIdx)) {
            s.setNullBit(i, true);
        } else {
            s.setNullBit(i, false);
        }
    }
    return s;
}

Series Series::diff(int periods) const {
    return sub(shift(periods));
}

Series Series::pctChange(int periods) const {
    Series shifted = shift(periods);
    std::vector<double> result(size_);
    for (size_t i = 0; i < size_; ++i) {
        double prev = shifted.getDouble(i);
        if (!std::isnan(prev) && prev != 0.0) {
            result[i] = (getDouble(i) - prev) / prev;
        } else {
            result[i] = std::nan("");
        }
    }
    return Series(name_, result);
}

Series Series::cumsum() const {
    std::vector<double> result(size_);
    double acc = 0.0;
    for (size_t i = 0; i < size_; ++i) {
        if (!isNull(i)) {
            acc += getDouble(i);
        }
        result[i] = acc;
    }
    return Series(name_, result);
}

Series Series::cumprod() const {
    std::vector<double> result(size_);
    double acc = 1.0;
    for (size_t i = 0; i < size_; ++i) {
        if (!isNull(i)) {
            acc *= getDouble(i);
        }
        result[i] = acc;
    }
    return Series(name_, result);
}

Series Series::cummin() const {
    std::vector<double> result(size_);
    double minVal = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < size_; ++i) {
        if (!isNull(i)) {
            minVal = std::min(minVal, getDouble(i));
        }
        result[i] = minVal;
    }
    return Series(name_, result);
}

Series Series::cummax() const {
    std::vector<double> result(size_);
    double maxVal = -std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < size_; ++i) {
        if (!isNull(i)) {
            maxVal = std::max(maxVal, getDouble(i));
        }
        result[i] = maxVal;
    }
    return Series(name_, result);
}

Series Series::rank() const {
    std::vector<std::pair<double, size_t>> indexed(size_);
    for (size_t i = 0; i < size_; ++i) {
        indexed[i] = {getDouble(i), i};
    }
    std::sort(indexed.begin(), indexed.end(), [](const auto& a, const auto& b) {
        if (std::isnan(a.first)) return false;
        if (std::isnan(b.first)) return true;
        return a.first < b.first;
    });
    
    std::vector<double> result(size_);
    for (size_t i = 0; i < size_; ++i) {
        result[indexed[i].second] = static_cast<double>(i + 1);
    }
    return Series(name_, result);
}

Series Series::normalize() const {
    double m = mean();
    double s = std();
    if (std::isnan(m) || std::isnan(s) || s == 0.0) {
        return copy();
    }
    return sub(m).div(s);
}

Series Series::minMaxScale(double newMin, double newMax) const {
    double minVal = min();
    double maxVal = max();
    if (std::isnan(minVal) || std::isnan(maxVal) || minVal == maxVal) {
        return copy();
    }
    double range = maxVal - minVal;
    double newRange = newMax - newMin;
    return sub(minVal).div(range).mul(newRange).add(newMin);
}

// ============================================================================
// Rolling Window Operations
// ============================================================================

Series Series::rollingSum(size_t window) const {
    std::vector<double> result(size_, std::nan(""));
    for (size_t i = window - 1; i < size_; ++i) {
        double sum = 0.0;
        bool valid = true;
        for (size_t j = i - window + 1; j <= i; ++j) {
            if (isNull(j)) {
                valid = false;
                break;
            }
            sum += getDouble(j);
        }
        if (valid) result[i] = sum;
    }
    return Series(name_, result);
}

Series Series::rollingMean(size_t window) const {
    std::vector<double> result(size_, std::nan(""));
    for (size_t i = window - 1; i < size_; ++i) {
        double sum = 0.0;
        bool valid = true;
        for (size_t j = i - window + 1; j <= i; ++j) {
            if (isNull(j)) {
                valid = false;
                break;
            }
            sum += getDouble(j);
        }
        if (valid) result[i] = sum / window;
    }
    return Series(name_, result);
}

Series Series::rollingMin(size_t window) const {
    std::vector<double> result(size_, std::nan(""));
    for (size_t i = window - 1; i < size_; ++i) {
        double minVal = std::numeric_limits<double>::infinity();
        bool valid = true;
        for (size_t j = i - window + 1; j <= i; ++j) {
            if (isNull(j)) {
                valid = false;
                break;
            }
            minVal = std::min(minVal, getDouble(j));
        }
        if (valid) result[i] = minVal;
    }
    return Series(name_, result);
}

Series Series::rollingMax(size_t window) const {
    std::vector<double> result(size_, std::nan(""));
    for (size_t i = window - 1; i < size_; ++i) {
        double maxVal = -std::numeric_limits<double>::infinity();
        bool valid = true;
        for (size_t j = i - window + 1; j <= i; ++j) {
            if (isNull(j)) {
                valid = false;
                break;
            }
            maxVal = std::max(maxVal, getDouble(j));
        }
        if (valid) result[i] = maxVal;
    }
    return Series(name_, result);
}

Series Series::rollingStd(size_t window) const {
    std::vector<double> result(size_, std::nan(""));
    for (size_t i = window - 1; i < size_; ++i) {
        double sum = 0.0, sumSq = 0.0;
        bool valid = true;
        for (size_t j = i - window + 1; j <= i; ++j) {
            if (isNull(j)) {
                valid = false;
                break;
            }
            double v = getDouble(j);
            sum += v;
            sumSq += v * v;
        }
        if (valid) {
            double mean = sum / window;
            double var = sumSq / window - mean * mean;
            result[i] = std::sqrt(var);
        }
    }
    return Series(name_, result);
}

Series Series::ewm(double alpha) const {
    std::vector<double> result(size_, std::nan(""));
    if (alpha < 0.0 || alpha > 1.0) return Series(name_, result);
    
    double ewm = std::nan("");
    for (size_t i = 0; i < size_; ++i) {
        if (!isNull(i)) {
            if (std::isnan(ewm)) {
                ewm = getDouble(i);
            } else {
                ewm = alpha * getDouble(i) + (1.0 - alpha) * ewm;
            }
            result[i] = ewm;
        }
    }
    return Series(name_, result);
}

// ============================================================================
// Filtering and Selection
// ============================================================================

Series Series::head(size_t n) const {
    return slice(0, std::min(n, size_));
}

Series Series::tail(size_t n) const {
    if (n >= size_) return copy();
    return slice(size_ - n, size_);
}

Series Series::slice(size_t start, size_t end) const {
    if (start >= size_ || start >= end) return Series(name_);
    end = std::min(end, size_);
    
    std::vector<size_t> indices;
    indices.reserve(end - start);
    for (size_t i = start; i < end; ++i) {
        indices.push_back(i);
    }
    return take(indices);
}

Series Series::take(const std::vector<size_t>& indices) const {
    Series result(name_);
    result.dtype_ = dtype_;
    result.reserve(indices.size());
    
    for (size_t idx : indices) {
        if (idx < size_) {
            switch (dtype_) {
                case DType::Float64:
                case DType::Float32:
                    result.pushDouble(float64_data_[idx]);
                    break;
                case DType::Int64:
                case DType::Int32:
                    result.pushInt(int64_data_[idx]);
                    break;
                case DType::String:
                    result.pushString(string_data_[idx]);
                    break;
                case DType::Bool:
                    result.pushBool(getBool(idx));
                    break;
                default:
                    break;
            }
            result.setNullBit(result.size_ - 1, !isNull(idx));
        }
    }
    return result;
}

Series Series::where(const Series& condition) const {
    std::vector<size_t> indices;
    indices.reserve(size_);
    for (size_t i = 0; i < std::min(size_, condition.size()); ++i) {
        if (condition.getBool(i)) {
            indices.push_back(i);
        }
    }
    return take(indices);
}

// ============================================================================
// Sorting
// ============================================================================

Series Series::sort(bool ascending) const {
    std::vector<size_t> indices = argsort(ascending);
    return take(indices);
}

std::vector<size_t> Series::argsort(bool ascending) const {
    std::vector<size_t> indices(size_);
    std::iota(indices.begin(), indices.end(), 0);
    
    auto cmp = [this, ascending](size_t a, size_t b) {
        bool aNull = isNull(a);
        bool bNull = isNull(b);
        if (aNull && bNull) return false;
        if (aNull) return false;  // Nulls at end
        if (bNull) return true;
        
        double va = getDouble(a);
        double vb = getDouble(b);
        return ascending ? (va < vb) : (va > vb);
    };
    
    std::stable_sort(indices.begin(), indices.end(), cmp);
    return indices;
}

std::vector<size_t> Series::argmin(size_t n) const {
    std::vector<size_t> indices = argsort(true);
    if (indices.size() > n) {
        indices.resize(n);
    }
    return indices;
}

std::vector<size_t> Series::argmax(size_t n) const {
    std::vector<size_t> indices = argsort(false);
    if (indices.size() > n) {
        indices.resize(n);
    }
    return indices;
}

// ============================================================================
// Unique Values
// ============================================================================

Series Series::unique() const {
    if (dtype_ == DType::String) {
        std::vector<std::string> seen;
        std::unordered_set<std::string> seenSet;
        for (size_t i = 0; i < size_; ++i) {
            if (!isNull(i)) {
                const std::string& s = string_data_[i];
                if (seenSet.find(s) == seenSet.end()) {
                    seenSet.insert(s);
                    seen.push_back(s);
                }
            }
        }
        return Series::fromStrings(name_, seen);
    } else {
        std::vector<double> seen;
        std::unordered_set<double> seenSet;
        for (size_t i = 0; i < size_; ++i) {
            if (!isNull(i)) {
                double v = getDouble(i);
                if (seenSet.find(v) == seenSet.end()) {
                    seenSet.insert(v);
                    seen.push_back(v);
                }
            }
        }
        return Series::fromDoubles(name_, seen);
    }
}

size_t Series::nunique() const {
    return unique().size();
}

std::unordered_map<std::string, size_t> Series::valueCounts() const {
    std::unordered_map<std::string, size_t> counts;
    for (size_t i = 0; i < size_; ++i) {
        if (!isNull(i)) {
            std::string key = getString(i);
            counts[key]++;
        }
    }
    return counts;
}

// ============================================================================
// Type Conversion
// ============================================================================

Series Series::astype(DType newType) const {
    switch (newType) {
        case DType::Float64:
        case DType::Float32: {
            std::vector<double> data(size_);
            for (size_t i = 0; i < size_; ++i) {
                data[i] = getDouble(i);
            }
            Series s(name_, data);
            for (size_t i = 0; i < size_; ++i) {
                s.setNullBit(i, !isNull(i));
            }
            return s;
        }
        case DType::Int64:
        case DType::Int32: {
            std::vector<int64_t> data(size_);
            for (size_t i = 0; i < size_; ++i) {
                data[i] = getInt(i);
            }
            Series s(name_, data);
            for (size_t i = 0; i < size_; ++i) {
                s.setNullBit(i, !isNull(i));
            }
            return s;
        }
        case DType::String: {
            std::vector<std::string> data(size_);
            for (size_t i = 0; i < size_; ++i) {
                data[i] = getString(i);
            }
            Series s(name_, data);
            for (size_t i = 0; i < size_; ++i) {
                s.setNullBit(i, !isNull(i));
            }
            return s;
        }
        case DType::Bool: {
            std::vector<bool> data(size_);
            for (size_t i = 0; i < size_; ++i) {
                data[i] = getBool(i);
            }
            Series s(name_, data);
            for (size_t i = 0; i < size_; ++i) {
                s.setNullBit(i, !isNull(i));
            }
            return s;
        }
        default:
            return copy();
    }
}

// ============================================================================
// Copy
// ============================================================================

Series Series::copy() const {
    Series s;
    s.dtype_ = dtype_;
    s.name_ = name_;
    s.size_ = size_;
    s.float64_data_ = float64_data_;
    s.int64_data_ = int64_data_;
    s.string_data_ = string_data_;
    s.bool_data_ = bool_data_;
    s.null_bitmap_ = null_bitmap_;
    return s;
}

// ============================================================================
// Utility
// ============================================================================

std::string Series::repr(size_t maxRows) const {
    std::ostringstream oss;
    oss << "Series('" << name_ << "', dtype=" << dtypeToString(dtype_) 
        << ", size=" << size_ << ")\n";
    
    size_t showRows = std::min(maxRows, size_);
    for (size_t i = 0; i < showRows; ++i) {
        oss << "  [" << i << "] ";
        if (isNull(i)) {
            oss << "null";
        } else {
            oss << getString(i);
        }
        oss << "\n";
    }
    if (size_ > maxRows) {
        oss << "  ... (" << (size_ - maxRows) << " more rows)\n";
    }
    return oss.str();
}

std::vector<double> Series::toVector() const {
    std::vector<double> result(size_);
    for (size_t i = 0; i < size_; ++i) {
        result[i] = getDouble(i);
    }
    return result;
}

}  // namespace qz_data
