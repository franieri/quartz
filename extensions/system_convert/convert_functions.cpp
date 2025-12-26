#include "function_registry.h"
#include "types.h"

void register_convert_functions(FunctionRegistry& reg) {
    auto toInt_func = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(0);
        if (std::holds_alternative<int>(args[0])) {
            return args[0];
        } else if (std::holds_alternative<double>(args[0])) {
            return Value(static_cast<int>(std::get<double>(args[0])));
        } else if (std::holds_alternative<std::string>(args[0])) {
            try {
                return Value(std::stoi(std::get<std::string>(args[0])));
            } catch (...) {
                return Value(0);
            }
        } else if (std::holds_alternative<bool>(args[0])) {
            return Value(std::get<bool>(args[0]) ? 1 : 0);
        }
        return Value(0);
    };
    reg.registerFunction("system.convert.toInt", toInt_func);
    reg.registerFunction("system.convert.toInteger", toInt_func);
    
    auto toDouble_func = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(0.0);
        if (std::holds_alternative<double>(args[0])) {
            return args[0];
        } else if (std::holds_alternative<int>(args[0])) {
            return Value(static_cast<double>(std::get<int>(args[0])));
        } else if (std::holds_alternative<std::string>(args[0])) {
            try {
                return Value(std::stod(std::get<std::string>(args[0])));
            } catch (...) {
                return Value(0.0);
            }
        } else if (std::holds_alternative<bool>(args[0])) {
            return Value(std::get<bool>(args[0]) ? 1.0 : 0.0);
        }
        return Value(0.0);
    };
    reg.registerFunction("system.convert.toDouble", toDouble_func);
    reg.registerFunction("system.convert.toFloat", toDouble_func);
    
    auto toString_func = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value("");
        return Value(to_string(args[0]));
    };
    reg.registerFunction("system.convert.toString", toString_func);
    
    auto toBool_func = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(false);
        if (std::holds_alternative<bool>(args[0])) {
            return args[0];
        } else if (std::holds_alternative<int>(args[0])) {
            return Value(std::get<int>(args[0]) != 0);
        } else if (std::holds_alternative<double>(args[0])) {
            return Value(std::get<double>(args[0]) != 0.0);
        } else if (std::holds_alternative<std::string>(args[0])) {
            std::string str = std::get<std::string>(args[0]);
            return Value(str == "true" || str == "1" || str == "yes");
        }
        return Value(false);
    };
    reg.registerFunction("system.convert.toBool", toBool_func);
    reg.registerFunction("system.convert.toBoolean", toBool_func);
}
