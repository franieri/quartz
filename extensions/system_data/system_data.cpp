#include "function_registry.h"
#include "dataframe.h"

void register_data_functions(FunctionRegistry& reg);
void register_io_functions(FunctionRegistry& reg);

extern "C" __attribute__((visibility("default"))) void init_extension(FunctionRegistry& reg) {
    register_data_functions(reg);
    register_io_functions(reg);
}
