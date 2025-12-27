#ifndef FUNCTION_REGISTRY_H
#define FUNCTION_REGISTRY_H

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <vector>
#include <set>
#include "types.h"
#include "qz_export.h"

class FunctionRegistry {
public:
    FunctionRegistry() = default;
    
    // Registration and lookup
    static FunctionRegistry& instance();
    void registerFunction(const std::string& name, std::function<Value(const std::vector<Value>&)> f);
    bool exists(const std::string& name) const;
    bool hasNamespace(const std::string& ns) const;  // Check if any functions exist with this namespace prefix
    Value call(const std::string& name, const std::vector<Value>& args);
    
    // Validation and diagnostics
    size_t getFunctionCount() const;
    std::vector<std::string> listFunctions() const;
    std::vector<std::string> findFunctions(const std::string& prefix) const;
    bool validateFunctionName(const std::string& name) const;

private:
    std::unordered_map<std::string, std::function<Value(const std::vector<Value>&)>> funcs;
    mutable std::unordered_set<std::string> namespaceCache;  // Cache of known namespaces
    
    // Helpers for validation
    bool is_valid_identifier(const char c, bool first = false) const;
    bool is_valid_namespace_char(const char c) const;
    void updateNamespaceCache(const std::string& name);
};

// Quartz naming convention (Qz prefix)
using QzFunctionRegistry = FunctionRegistry;

extern QZ_CORE_API FunctionRegistry* global_reg_ptr;

#endif // FUNCTION_REGISTRY_H
