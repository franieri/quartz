#include "function_registry.h"
#include "types.h"
#include <iostream>
#include <string>

void register_stdin_functions(FunctionRegistry& reg) {
    auto readln = [](const std::vector<Value>& args) -> Value {
        std::string line;
        std::getline(std::cin, line);
        return Value(line);
    };
    reg.registerFunction("system.io.stdin.readln", readln);
}