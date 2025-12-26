#include "types.h"
#include "runtime.h"

static inline std::string primitive_to_string(const Value& v) {
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, int>) {
            return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, double>) {
            return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return arg;
        } else if constexpr (std::is_same_v<T, bool>) {
            return arg ? "true" : "false";
        } else if constexpr (std::is_same_v<T, ArrayRef>) {
            return "<array>";
        } else if constexpr (std::is_same_v<T, DictRef>) {
            return "<dict>";
        } else {
            return "unknown";
        }
    }, v);
}

std::string to_string(const Value& v) {
    if (std::holds_alternative<ArrayRef>(v) || std::holds_alternative<DictRef>(v)) {
        if (global_runtime_ptr) return global_runtime_ptr->formatValue(v);
    }
    return primitive_to_string(v);
}

std::string TypeAnnotation::toString() const {
    std::string result = name;

    if (!generic_params.empty()) {
        result += "<";
        for (size_t i = 0; i < generic_params.size(); ++i) {
            if (i) result += ", ";
            result += generic_params[i];
        }
        result += ">";
    }
    
    if (is_array) result += "[]";
    if (is_optional) result += "?";
    
    if (is_result) {
        result = "Result<" + name + ", " + error_type + ">";
    }
    
    return result;
}

bool TypeAnnotation::isCompatible(const TypeAnnotation& other) const {
    auto compatibleName = [](const std::string& a, const std::string& b) -> bool {
        if (a == b) return true;
        const bool aNum = (a == "int" || a == "double");
        const bool bNum = (b == "int" || b == "double");
        return aNum && bNum;
    };

    // Same type always compatible
    if (name == other.name && 
        is_array == other.is_array && 
        is_optional == other.is_optional &&
        is_result == other.is_result &&
        generic_params == other.generic_params) {
        return true;
    }
    
    // Generic types: require same arity and pairwise compatibility.
    if (name == other.name && generic_params.size() == other.generic_params.size() && !generic_params.empty()) {
        for (size_t i = 0; i < generic_params.size(); ++i) {
            if (!compatibleName(generic_params[i], other.generic_params[i])) return false;
        }
        return is_array == other.is_array && is_optional == other.is_optional && is_result == other.is_result;
    }

    // Numeric types are loosely compatible
    if (compatibleName(name, other.name)) return true;
    
    return false;
}
