// ============================================================================
// Runtime - Evaluate Implementation
// Handles the evaluate() method for evaluating expressions
// ============================================================================

#include "runtime.h"
#include "types.h"
#include "logger.h"
#include "function_registry.h"

// Helper to append value to string (avoids repeated branching)
static inline void appendValueToString(std::string& out, const Value& val, bool quote_strings = false) {
    std::visit([&out, quote_strings](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, int>) {
            out += std::to_string(arg);
        } else if constexpr (std::is_same_v<T, double>) {
            out += std::to_string(arg);
        } else if constexpr (std::is_same_v<T, std::string>) {
            if (quote_strings) {
                out += '"';
                out += arg;
                out += '"';
            } else {
                out += arg;
            }
        } else if constexpr (std::is_same_v<T, bool>) {
            out += arg ? "true" : "false";
        } else if constexpr (std::is_same_v<T, TaskRef>) {
            out += "<task:";
            out += arg.id;
            out += ">";
        } else if constexpr (std::is_same_v<T, BufferRef>) {
            out += "<buffer:";
            out += arg.id;
            out += ">";
        }
    }, val);
}

Value Runtime::evaluate(const ASTNodePtr& node) {
    switch (node->type) {
    case NodeType::Literal:
        return node->value;

    case NodeType::Identifier: {
        std::string name = std::get<std::string>(node->value);
        
        // Check for dot notation - field access on object
        size_t dotPos = name.find('.');
        if (dotPos != std::string::npos) {
            std::string objName = name.substr(0, dotPos);
            std::string fieldName = name.substr(dotPos + 1);
            
            // Check if objName is an object
            auto objIt = objects.find(objName);
            if (objIt != objects.end()) {
                // Direct object access
                return objIt->second->getField(fieldName);
            }
            
            // Also check in variables for object references
            if (hasVariable(objName)) {
                Value varVal = getVariable(objName);
                if (std::holds_alternative<std::string>(varVal)) {
                    std::string objId = std::get<std::string>(varVal);
                    auto objIdIt = objects.find(objId);
                    if (objIdIt != objects.end()) {
                        return objIdIt->second->getField(fieldName);
                    }
                }
            }
        }
        
        return getVariable(name);
    }

    case NodeType::InterpolatedString: {
        // String interpolation: concatenate all parts
        std::string result;
        result.reserve(64);  // Pre-allocate reasonable size
        for (const auto& child : node->children) {
            if (child->type == NodeType::StringPart) {
                // Plain string part
                result += std::get<std::string>(child->value);
            } else if (child->type == NodeType::InterpExpr) {
                // Interpolated expression
                Value val = evaluate(child->children[0]);
                appendValueToString(result, val);
            }
        }
        return Value(std::move(result));
    }

    case NodeType::Array: {
        // Array literal evaluation - store actual vector and return handle
        std::vector<Value> arrayVec;
        arrayVec.reserve(node->children.size());

        for (const auto& child : node->children) {
            arrayVec.push_back(evaluate(child));
        }

        // Store the actual array and return the handle
        std::string arrayId = "__array_" + std::to_string(nextArrayId++);
        arrayStorage[arrayId] = std::move(arrayVec);
        return Value(ArrayRef{arrayId});
    }

    case NodeType::Dict: {
        // Dictionary literal evaluation - store actual map and return handle
        std::unordered_map<std::string, Value> dictMap;
        dictMap.reserve(node->children.size());
        
        for (size_t i = 0; i < node->children.size(); ++i) {
            const ASTNodePtr& pair = node->children[i];
            if (!pair->children.empty()) {
                Value val = evaluate(pair->children[0]);
                dictMap[pair->name] = val;
            }
        }

        // Store the actual dict and return the handle
        std::string dictId = "__dict_" + std::to_string(nextDictId++);
        dictStorage[dictId] = std::move(dictMap);
        return Value(DictRef{dictId});
    }

    case NodeType::Index: {
        // Array/Dict indexing
        ASTNodePtr containerNode = node->children[0];
        ASTNodePtr indexNode = node->children[1];

        Value containerVal = evaluate(containerNode);
        Value indexValue = evaluate(indexNode);

        if (std::holds_alternative<ArrayRef>(containerVal) && std::holds_alternative<int>(indexValue)) {
            const auto& id = std::get<ArrayRef>(containerVal).id;
            int idx = std::get<int>(indexValue);
            auto it = arrayStorage.find(id);
            if (it != arrayStorage.end()) {
                auto& vec = it->second;
                if (idx >= 0 && idx < (int)vec.size()) return vec[(size_t)idx];
            }
            return Value{};
        }

        if (std::holds_alternative<DictRef>(containerVal) && std::holds_alternative<std::string>(indexValue)) {
            const auto& id = std::get<DictRef>(containerVal).id;
            const std::string& key = std::get<std::string>(indexValue);
            auto it = dictStorage.find(id);
            if (it != dictStorage.end()) {
                auto& dict = it->second;
                auto kIt = dict.find(key);
                if (kIt != dict.end()) return kIt->second;
            }
            return Value{};
        }

        // Back-compat: identifier-based indexing using var->id mapping
        if (containerNode->type == NodeType::Identifier) {
            std::string varName = std::get<std::string>(containerNode->value);
            auto aIt = varToArrayId.find(varName);
            if (aIt != varToArrayId.end() && std::holds_alternative<int>(indexValue)) {
                int idx = std::get<int>(indexValue);
                auto it = arrayStorage.find(aIt->second);
                if (it != arrayStorage.end()) {
                    auto& vec = it->second;
                    if (idx >= 0 && idx < (int)vec.size()) return vec[(size_t)idx];
                }
            }
            auto dIt = varToDictId.find(varName);
            if (dIt != varToDictId.end() && std::holds_alternative<std::string>(indexValue)) {
                const std::string& key = std::get<std::string>(indexValue);
                auto it = dictStorage.find(dIt->second);
                if (it != dictStorage.end()) {
                    auto& dict = it->second;
                    auto kIt = dict.find(key);
                    if (kIt != dict.end()) return kIt->second;
                }
            }
        }

        return Value{};
    }

    case NodeType::New: {
        // Object instantiation: new ClassName(args) or new module.ClassName(args)
        std::string fullClassName = std::get<std::string>(node->value);
        std::string actualClassName = fullClassName;
        
        // Evaluate constructor arguments
        std::vector<Value> constructorArgs;
        for (const auto& arg : node->children) {
            constructorArgs.push_back(evaluate(arg));
        }
        
        // Check if this is a qualified class name (module.ClassName)
        size_t dotPos = fullClassName.rfind('.');
        if (dotPos != std::string::npos) {
            std::string moduleRef = fullClassName.substr(0, dotPos);
            std::string className = fullClassName.substr(dotPos + 1);
            
            // Resolve module reference (could be alias or full path)
            std::string modulePath;
            auto aliasIt = imports.find(moduleRef);
            if (aliasIt != imports.end()) {
                modulePath = aliasIt->second;
            } else {
                modulePath = moduleRef;
            }
            
            // Find the class that belongs to this module
            bool found = false;
            for (const auto& pair : classToModule) {
                if (pair.first == className && pair.second == modulePath) {
                    actualClassName = className;
                    found = true;
                    break;
                }
            }
            
            if (!found) {
                Logger::instance().log(LogLevel::ERROR, "Class '" + className + "' not found in module '" + modulePath + "'");
                return Value(fullClassName + "_null");
            }
        }
        
        // Look up class definition
        auto it = classRegistry.find(actualClassName);
        if (it != classRegistry.end()) {
            const ClassDef& classDef = it->second;
            
            // Create new instance
            auto instance = createObject(actualClassName);
            
            // Initialize fields with default values (or from parent class)
            if (!classDef.parentClass.empty()) {
                // Copy fields from parent class
                auto parentIt = classRegistry.find(classDef.parentClass);
                if (parentIt != classRegistry.end()) {
                    for (const auto& field : parentIt->second.fields) {
                        instance->setField(field.first, Value{});
                    }
                }
            }
            
            // Initialize own fields with default values
            for (const auto& field : classDef.fields) {
                instance->setField(field.first, Value{});
            }
            
            // Store instance first (for 'this' reference in constructor)
            static int objectCounter = 0;
            std::string objId = actualClassName + "_" + std::to_string(objectCounter++);
            objects[objId] = instance;
            
            // Execute constructor if defined
            if (classDef.hasConstructor) {
                const ConstructorDef& constructor = classDef.constructor;
                
                // Save current scope
                auto savedVars = variables;
                auto savedThisObject = currentThisObject;
                currentThisObject = objId;
                
                // Bind constructor parameters to arguments
                for (size_t i = 0; i < constructor.parameters.size() && i < constructorArgs.size(); ++i) {
                    variables[constructor.parameters[i].first] = constructorArgs[i];
                }
                
                // Execute field initializers
                for (const auto& init : constructor.fieldInits) {
                    Value initVal = evaluate(init.second);
                    instance->setField(init.first, initVal);
                }
                
                // TODO: Call super constructor if callsSuper is true
                
                // Execute constructor body
                if (constructor.body) {
                    executeMethodBody(constructor.body);
                }
                
                // Restore scope
                variables = savedVars;
                currentThisObject = savedThisObject;
            }
            
            return Value(objId);
        }
        
        Logger::instance().log(LogLevel::ERROR, "Class '" + actualClassName + "' not found");
        return Value(fullClassName + "_null");
    }

    case NodeType::Unary: {
        // Unary operator (-, !)
        Value operand = evaluate(node->children[0]);
        std::string op = std::get<std::string>(node->value);
        
        if (op == "-") {
            // Negation
            if (std::holds_alternative<int>(operand)) {
                return Value(-std::get<int>(operand));
            } else if (std::holds_alternative<double>(operand)) {
                return Value(-std::get<double>(operand));
            }
        } else if (op == "!") {
            // Logical NOT
            bool val = std::visit([](auto&& arg) -> bool {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, bool>) return arg;
                else if constexpr (std::is_arithmetic_v<T>) return arg != 0;
                else if constexpr (std::is_same_v<T, TaskRef>) return true;  // Task handle is truthy
                else if constexpr (std::is_same_v<T, BufferRef>) return true;  // Buffer handle is truthy
                else return false;
            }, operand);
            return Value(!val);
        }
        return operand;
    }

    case NodeType::Binary: {
        Value left = evaluate(node->children[0]);
        Value right = evaluate(node->children[1]);
        std::string op = std::get<std::string>(node->value);
        return applyBinaryOp(left, right, op);
    }

    case NodeType::Call: {
        try {
            std::string name = std::get<std::string>(node->value);
            std::vector<Value> args;
            size_t argIndex = 0;
            for (const auto &arg : node->children) {
                try {
                    args.push_back(evaluate(arg));
                } catch (const LanguageException& e) {
                    throw;  // Re-throw language exceptions as-is
                } catch (const std::exception& e) {
                    std::string msg = "Error evaluating argument " + std::to_string(argIndex + 1) + 
                                      " in call to '" + name + "': " + std::string(e.what());
                    if (arg->line >= 0) {
                        msg += " [argument at line " + std::to_string(arg->line) + 
                               ", col " + std::to_string(arg->column) + "]";
                    }
                    if (node->line >= 0) {
                        msg += " [function call at line " + std::to_string(node->line) + 
                               ", col " + std::to_string(node->column) + "]";
                    }
                    notifyError("call", msg, node->line, node->column, false);
                    return Value{};
                }
                argIndex++;
            }
            
            // Check if name is a lambda variable
            auto lambdaVarIt = varToLambdaId.find(name);
            if (lambdaVarIt != varToLambdaId.end()) {
                return invokeLambda(lambdaVarIt->second, args);
            }
            
            // Check if name resolves to a lambda ID directly (from variable lookup)
            if (hasVariable(name)) {
                Value varVal = getVariable(name);
                if (std::holds_alternative<std::string>(varVal)) {
                    std::string possibleLambda = std::get<std::string>(varVal);
                    if (possibleLambda.substr(0, 9) == "__lambda_") {
                        return invokeLambda(possibleLambda, args);
                    }
                }
            }
            
            // Check if this is a method call on an object (e.g., calc.abs)
            size_t dotPos = name.find('.');
            if (dotPos != std::string::npos) {
                std::string prefix = name.substr(0, dotPos);
                std::string memberName = name.substr(dotPos + 1);
                
                // Check if prefix is an object variable
                auto objIt = objects.find(prefix);
                if (objIt == objects.end()) {
                    // Try to find object by looking up variable value
                    if (hasVariable(prefix)) {
                        Value varVal = getVariable(prefix);
                        if (std::holds_alternative<std::string>(varVal)) {
                            std::string objId = std::get<std::string>(varVal);
                            objIt = objects.find(objId);
                        }
                    }
                }
                
                if (objIt != objects.end()) {
                    // This is a member access on an object
                    ObjectInstancePtr obj = objIt->second;
                    std::string className = obj->getClassName();
                    
                    // First, check if it's a field access (no args and field exists)
                    if (args.empty() && obj->hasField(memberName)) {
                        return obj->getField(memberName);
                    }
                    
                    // Look up the class definition for method
                    auto classIt = classRegistry.find(className);
                    if (classIt != classRegistry.end()) {
                        // Find the method
                        for (const auto& method : classIt->second.methods) {
                            if (method.name == memberName) {
                                // Built-in exception methods
                                if (className == "Exception" || className == "RuntimeError" ||
                                    className == "ValueError" || className == "TypeError" ||
                                    className == "IndexError" || className == "NullError") {
                                    if (memberName == "getMessage") {
                                        return obj->getField("message");
                                    } else if (memberName == "getType") {
                                        return obj->getField("type");
                                    }
                                }
                                
                                if (!method.body) {
                                    // Built-in method with no body - check for field access
                                    return obj->getField(memberName);
                                }
                                
                                // Found the method - execute it
                                // Save current variables and set up method scope
                                auto savedVars = variables;
                                auto savedThisObject = currentThisObject;
                                currentThisObject = objIt->first;
                                
                                // Bind parameters to arguments
                                for (size_t i = 0; i < method.parameters.size() && i < args.size(); ++i) {
                                    variables[method.parameters[i].first] = args[i];
                                }
                                
                                // Execute method body
                                Value result = executeMethodBody(method.body);
                                
                                // Restore variables
                                variables = savedVars;
                                currentThisObject = savedThisObject;
                                
                                return result;
                            }
                        }
                        
                        // No method found - try field access as fallback
                        if (obj->hasField(memberName)) {
                            return obj->getField(memberName);
                        }
                        
                        Logger::instance().log(LogLevel::ERROR, "Member not found: " + memberName + " in class " + className);
                        return Value{};
                    }
                }
            }
            
            if (name.find('.') == std::string::npos) {
                // unqualified - search in imports
                for (const auto& pair : imports) {
                    std::string full = pair.second + "." + name;
                    if (FunctionRegistry::instance().exists(full)) {
                        return FunctionRegistry::instance().call(full, args);
                    }
                }
                std::string msg = std::string("Unknown function: ") + name;
                notifyError("call", msg, node->line, node->column, false);
                return Value{};
            } else {
                // qualified call
                std::string resolved = resolveFunctionName(name);
                if (FunctionRegistry::instance().exists(resolved)) {
                    return FunctionRegistry::instance().call(resolved, args);
                } else {
                    std::string msg = std::string("Unknown function: ") + resolved;
                    notifyError("call", msg, node->line, node->column, false);
                    return Value{};
                }
            }
        } catch (const LanguageException& e) {
            // Re-throw language exceptions to be caught by higher level handlers
            throw;
        } catch (const std::exception& e) {
            std::string funcName = std::get<std::string>(node->value);
            std::string resolved = resolveFunctionName(funcName);
            std::string msg = "Error in function call '" + funcName + "'";
            if (funcName != resolved) {
                msg += " (resolved to '" + resolved + "')";
            }
            msg += ": " + std::string(e.what());
            // Add information about current module context if available
            if (!currentLoadingModule.empty()) {
                msg += " [in module " + currentLoadingModule + "]";
            }
            notifyError("call", msg, node->line, node->column, false);
            return Value{};
        } catch (...) {
            std::string funcName = std::get<std::string>(node->value);
            std::string resolved = resolveFunctionName(funcName);
            std::string msg = "Unknown error in function call '" + funcName + "'";
            if (funcName != resolved) {
                msg += " (resolved to '" + resolved + "')";
            }
            msg += " - an unhandled exception occurred during execution";
            if (!currentLoadingModule.empty()) {
                msg += " [in module " + currentLoadingModule + "]";
            }
            notifyError("call", msg, node->line, node->column, false);
            return Value{};
        }
    }

    case NodeType::Lambda: {
        // Lambda expression - create closure and return lambda ID
        std::string lambdaId = "__lambda_" + std::to_string(nextLambdaId++);
        
        // Capture current scope variables (simple copy for now)
        StoredLambda storedLambda;
        storedLambda.node = node;
        storedLambda.captures = variables;  // Capture all variables
        
        lambdaStorage[lambdaId] = std::move(storedLambda);
        
        return Value(lambdaId);
    }

    case NodeType::Super: {
        // Super class method call - handled by Call case
        return Value{};
    }

    default:
        return Value{};
    }
}

Value Runtime::applyBinaryOp(const Value& left, const Value& right, const std::string& op) {
    // Helper to check truthiness
    auto isTruthy = [](const Value& v) -> bool {
        return std::visit([](auto&& arg) -> bool {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, bool>) return arg;
            else if constexpr (std::is_arithmetic_v<T>) return arg != 0;
            else if constexpr (std::is_same_v<T, std::string>) return !arg.empty();
            else return false;
        }, v);
    };
    
    // Handle logical operators first (work on any truthy/falsy values)
    if (op == "&&") {
        bool leftTrue = isTruthy(left);
        bool rightTrue = isTruthy(right);
        return Value(leftTrue && rightTrue ? 1 : 0);
    }
    if (op == "||") {
        bool leftTrue = isTruthy(left);
        bool rightTrue = isTruthy(right);
        return Value(leftTrue || rightTrue ? 1 : 0);
    }
    
    // Handle string concatenation
    if (op == "+") {
        bool leftIsString = std::holds_alternative<std::string>(left);
        bool rightIsString = std::holds_alternative<std::string>(right);
        if (leftIsString || rightIsString) {
            // Convert both to strings and concatenate
            std::string leftStr = formatValue(left);
            std::string rightStr = formatValue(right);
            return Value(leftStr + rightStr);
        }
    }
    
    // Handle string equality comparison
    if (op == "==" || op == "!=") {
        if (std::holds_alternative<std::string>(left) && std::holds_alternative<std::string>(right)) {
            const std::string& l = std::get<std::string>(left);
            const std::string& r = std::get<std::string>(right);
            if (op == "==") return Value(l == r ? 1 : 0);
            if (op == "!=") return Value(l != r ? 1 : 0);
        }
    }
    
    return std::visit([&](auto&& l, auto&& r) -> Value {
        using L = std::decay_t<decltype(l)>;
        using R = std::decay_t<decltype(r)>;
        if constexpr (std::is_arithmetic_v<L> && std::is_arithmetic_v<R>) {
            if (op == "+") return l + r;
            if (op == "-") return l - r;
            if (op == "*") return l * r;
            if (op == "/") {
                // Check for division by zero
                if constexpr (std::is_integral_v<R>) {
                    if (r == 0) {
                        throw LanguageException("ArithmeticError", "Division by zero");
                    }
                } else if constexpr (std::is_floating_point_v<R>) {
                    if (r == 0.0) {
                        throw LanguageException("ArithmeticError", "Division by zero");
                    }
                }
                return l / r;
            }
            if (op == "==") return l == r;
            if (op == "!=") return l != r;
            if (op == "<") return l < r;
            if (op == ">") return l > r;
            if (op == "<=") return l <= r;
            if (op == ">=") return l >= r;
        }
        return Value{};
    }, left, right);
}

// ============================================================================
// Helper Methods
// ============================================================================

Value Runtime::executeMethodBody(const ASTNodePtr& body) {
    Value returnValue;
    
    // Save and clear return flag - nested calls shouldn't affect our return state
    bool savedShouldReturn = shouldReturn;
    Value savedPendingReturn = pendingReturnValue;
    shouldReturn = false;
    pendingReturnValue = Value{};
    
    if (body->type == NodeType::Block) {
        for (const auto& stmt : body->children) {
            if (stmt->type == NodeType::Return) {
                // Return statement - evaluate the return value
                if (!stmt->children.empty()) {
                    returnValue = evaluate(stmt->children[0]);
                }
                // Restore saved state before returning
                shouldReturn = savedShouldReturn;
                pendingReturnValue = savedPendingReturn;
                return returnValue;
            } else {
                // Execute other statements
                executeNode(stmt);
                
                // Check if a return was triggered inside nested blocks (if, while, etc.)
                if (shouldReturn) {
                    returnValue = pendingReturnValue;
                    // Clear our return flag but keep the value
                    shouldReturn = savedShouldReturn;
                    pendingReturnValue = savedPendingReturn;
                    return returnValue;
                }
            }
        }
    } else if (body->type == NodeType::Return) {
        if (!body->children.empty()) {
            returnValue = evaluate(body->children[0]);
        }
        shouldReturn = savedShouldReturn;
        pendingReturnValue = savedPendingReturn;
        return returnValue;
    } else {
        // Single expression body
        shouldReturn = savedShouldReturn;
        pendingReturnValue = savedPendingReturn;
        return evaluate(body);
    }
    
    // Restore state before returning
    shouldReturn = savedShouldReturn;
    pendingReturnValue = savedPendingReturn;
    return returnValue;
}

bool Runtime::hasImport(const std::string& alias) const {
    return imports.find(alias) != imports.end();
}

std::string Runtime::getNodeTypeString(const ASTNodePtr& node) const {
    if (!node) return "null";
    return node->typeString();
}

void Runtime::reportError(const std::string& context, const std::string& message) {
    notifyError(context, message, -1, -1, true);
}

Value Runtime::invokeLambda(const std::string& lambdaId, const std::vector<Value>& args) {
    auto it = lambdaStorage.find(lambdaId);
    if (it == lambdaStorage.end()) {
        Logger::instance().log(LogLevel::ERROR, "Lambda not found: " + lambdaId);
        return Value{};
    }
    
    const StoredLambda& lambda = it->second;
    const ASTNodePtr& lambdaNode = lambda.node;
    
    // Save current variables
    auto savedVars = variables;
    
    // Restore captured variables
    variables = lambda.captures;
    
    // Bind parameters to arguments
    // Lambda children: [param1, param2, ..., body]
    size_t paramCount = lambdaNode->children.size() - 1;  // Last child is body
    for (size_t i = 0; i < paramCount && i < args.size(); ++i) {
        const ASTNodePtr& param = lambdaNode->children[i];
        if (param->type == NodeType::Parameter) {
            std::string paramName = std::get<std::string>(param->value);
            variables[paramName] = args[i];
        }
    }
    
    // Get the body (last child)
    ASTNodePtr body = lambdaNode->children.back();
    
    // Execute: expression body (isMutable=true) vs block body (isMutable=false)
    Value result;
    if (lambdaNode->isMutable) {
        // Expression body - evaluate directly
        result = evaluate(body);
    } else {
        // Block body - execute as statements
        result = executeMethodBody(body);
    }
    
    // Restore variables
    variables = savedVars;
    
    return result;
}
