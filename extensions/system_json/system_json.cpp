#include "function_registry.h"

// Forward declarations
void register_json_functions(FunctionRegistry& reg);

extern "C" __attribute__((visibility("default"))) void init_extension(FunctionRegistry& reg) {
    register_json_functions(reg);
}
