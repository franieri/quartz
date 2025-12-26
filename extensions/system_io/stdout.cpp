#include "function_registry.h"
#include "types.h"
#include <iostream>

void register_stdout_functions(FunctionRegistry& reg) {
    auto println = [](const std::vector<Value>& args) -> Value {
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) std::cout << " ";
            std::cout << to_string(args[i]);
        }
        std::cout << std::endl;
        return Value();
    };
    reg.registerFunction("system.io.println", println);
    reg.registerFunction("system.io.out.println", println);
}