#include "function_registry.h"

// Forward declarations
void register_stdout_functions(FunctionRegistry& reg);
void register_stdin_functions(FunctionRegistry& reg);

extern "C" __attribute__((visibility("default"))) void init_extension(FunctionRegistry& reg) {
    register_stdout_functions(reg);
    register_stdin_functions(reg);
}