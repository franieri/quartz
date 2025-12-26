#include "function_registry.h"
#include <algorithm>

FunctionRegistry* global_reg_ptr __attribute__((visibility("default"))) = nullptr;

FunctionRegistry& FunctionRegistry::instance() {
    if (!global_reg_ptr) {
        global_reg_ptr = new FunctionRegistry();
    }
    return *global_reg_ptr;
}

void FunctionRegistry::updateNamespaceCache(const std::string& name) {
    // Extract all namespace prefixes from the function name
    size_t pos = 0;
    while ((pos = name.find('.', pos)) != std::string::npos) {
        namespaceCache.insert(name.substr(0, pos));
        ++pos;
    }
}

void FunctionRegistry::registerFunction(const std::string& name, std::function<Value(const std::vector<Value>&)> f) {
    if (name.empty()) {
        return;  // Reject empty names
    }
    
    if (!validateFunctionName(name)) {
        return;  // Reject invalid names
    }
    
    // Update namespace cache
    updateNamespaceCache(name);
    
    // Overwrite if already exists (allows extensions to replace functions)
    funcs[name] = std::move(f);
}

bool FunctionRegistry::exists(const std::string& name) const {
    return funcs.find(name) != funcs.end();
}

bool FunctionRegistry::hasNamespace(const std::string& ns) const {
    // Fast path: check namespace cache first (O(1) lookup)
    return namespaceCache.find(ns) != namespaceCache.end();
}

Value FunctionRegistry::call(const std::string& name, const std::vector<Value>& args) {
    auto it = funcs.find(name);
    if (it != funcs.end()) {
        return it->second(args);  // Let exceptions propagate for proper error handling
    }
    return Value();  // Return empty value if function not found
}

size_t FunctionRegistry::getFunctionCount() const {
    return funcs.size();
}

std::vector<std::string> FunctionRegistry::listFunctions() const {
    std::vector<std::string> result;
    for (const auto& pair : funcs) {
        result.push_back(pair.first);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::string> FunctionRegistry::findFunctions(const std::string& prefix) const {
    std::vector<std::string> result;
    for (const auto& pair : funcs) {
        if (pair.first.find(prefix) == 0) {
            result.push_back(pair.first);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

bool FunctionRegistry::validateFunctionName(const std::string& name) const {
    if (name.empty()) return false;
    
    // Split by dots to validate namespace components
    size_t pos = 0;
    size_t prev_pos = 0;
    
    while ((pos = name.find('.', prev_pos)) != std::string::npos) {
        std::string component = name.substr(prev_pos, pos - prev_pos);
        
        // Each component must be a valid identifier
        if (component.empty()) return false;
        if (!is_valid_identifier(component[0], true)) return false;
        
        for (size_t i = 1; i < component.length(); ++i) {
            if (!is_valid_identifier(component[i], false)) return false;
        }
        
        prev_pos = pos + 1;
    }
    
    // Validate final component
    std::string component = name.substr(prev_pos);
    if (component.empty()) return false;
    if (!is_valid_identifier(component[0], true)) return false;
    
    for (size_t i = 1; i < component.length(); ++i) {
        if (!is_valid_identifier(component[i], false)) return false;
    }
    
    return true;
}

bool FunctionRegistry::is_valid_identifier(const char c, bool first) const {
    if (first) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    } else {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    }
}

bool FunctionRegistry::is_valid_namespace_char(const char c) const {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
}
