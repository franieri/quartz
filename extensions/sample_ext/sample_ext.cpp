#include "function_registry.h"
#include "types.h"

extern "C" void init_extension(FunctionRegistry& reg) {
    auto hello = [](const std::vector<Value>& args) -> Value {
        return Value("Hello from sample extension!");
    };
    reg.registerFunction("sample.hello", hello);
}