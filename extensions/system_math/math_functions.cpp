#include "function_registry.h"
#include "types.h"
#include <cmath>

void register_math_functions(FunctionRegistry& reg) {
    // Basic math operations
    auto abs_func = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value();
        double val = 0;
        if (std::holds_alternative<int>(args[0])) {
            val = std::abs(std::get<int>(args[0]));
            return Value(static_cast<int>(val));
        } else if (std::holds_alternative<double>(args[0])) {
            val = std::abs(std::get<double>(args[0]));
            return Value(val);
        }
        return Value();
    };
    reg.registerFunction("system.math.abs", abs_func);
    
    auto sqrt_func = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value();
        double val = 0;
        if (std::holds_alternative<int>(args[0])) {
            val = std::sqrt(static_cast<double>(std::get<int>(args[0])));
        } else if (std::holds_alternative<double>(args[0])) {
            val = std::sqrt(std::get<double>(args[0]));
        }
        return Value(val);
    };
    reg.registerFunction("system.math.sqrt", sqrt_func);
    
    auto pow_func = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value();
        double base = 0, exp = 0;
        if (std::holds_alternative<int>(args[0])) {
            base = static_cast<double>(std::get<int>(args[0]));
        } else if (std::holds_alternative<double>(args[0])) {
            base = std::get<double>(args[0]);
        }
        if (std::holds_alternative<int>(args[1])) {
            exp = static_cast<double>(std::get<int>(args[1]));
        } else if (std::holds_alternative<double>(args[1])) {
            exp = std::get<double>(args[1]);
        }
        return Value(std::pow(base, exp));
    };
    reg.registerFunction("system.math.pow", pow_func);
    
    auto floor_func = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value();
        double val = 0;
        if (std::holds_alternative<int>(args[0])) {
            val = std::get<int>(args[0]);
        } else if (std::holds_alternative<double>(args[0])) {
            val = std::get<double>(args[0]);
        }
        return Value(static_cast<int>(std::floor(val)));
    };
    reg.registerFunction("system.math.floor", floor_func);
    
    auto ceil_func = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value();
        double val = 0;
        if (std::holds_alternative<int>(args[0])) {
            val = std::get<int>(args[0]);
        } else if (std::holds_alternative<double>(args[0])) {
            val = std::get<double>(args[0]);
        }
        return Value(static_cast<int>(std::ceil(val)));
    };
    reg.registerFunction("system.math.ceil", ceil_func);
    
    auto round_func = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value();
        double val = 0;
        if (std::holds_alternative<int>(args[0])) {
            val = std::get<int>(args[0]);
        } else if (std::holds_alternative<double>(args[0])) {
            val = std::get<double>(args[0]);
        }
        return Value(static_cast<int>(std::round(val)));
    };
    reg.registerFunction("system.math.round", round_func);
    
    auto min_func = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value();
        double a = 0, b = 0;
        if (std::holds_alternative<int>(args[0])) {
            a = std::get<int>(args[0]);
        } else if (std::holds_alternative<double>(args[0])) {
            a = std::get<double>(args[0]);
        }
        if (std::holds_alternative<int>(args[1])) {
            b = std::get<int>(args[1]);
        } else if (std::holds_alternative<double>(args[1])) {
            b = std::get<double>(args[1]);
        }
        if (a <= b) return args[0];
        return args[1];
    };
    reg.registerFunction("system.math.min", min_func);
    
    auto max_func = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value();
        double a = 0, b = 0;
        if (std::holds_alternative<int>(args[0])) {
            a = std::get<int>(args[0]);
        } else if (std::holds_alternative<double>(args[0])) {
            a = std::get<double>(args[0]);
        }
        if (std::holds_alternative<int>(args[1])) {
            b = std::get<int>(args[1]);
        } else if (std::holds_alternative<double>(args[1])) {
            b = std::get<double>(args[1]);
        }
        if (a >= b) return args[0];
        return args[1];
    };
    reg.registerFunction("system.math.max", max_func);
    
    auto sin_func = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value();
        double val = 0;
        if (std::holds_alternative<int>(args[0])) {
            val = std::sin(static_cast<double>(std::get<int>(args[0])));
        } else if (std::holds_alternative<double>(args[0])) {
            val = std::sin(std::get<double>(args[0]));
        }
        return Value(val);
    };
    reg.registerFunction("system.math.sin", sin_func);
    
    auto cos_func = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value();
        double val = 0;
        if (std::holds_alternative<int>(args[0])) {
            val = std::cos(static_cast<double>(std::get<int>(args[0])));
        } else if (std::holds_alternative<double>(args[0])) {
            val = std::cos(std::get<double>(args[0]));
        }
        return Value(val);
    };
    reg.registerFunction("system.math.cos", cos_func);
}
