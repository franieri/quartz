#include "function_registry.h"

void register_convert_functions(FunctionRegistry& reg);

extern "C" __attribute__((visibility("default"))) void init_extension(FunctionRegistry& reg) {
    register_convert_functions(reg);
}
