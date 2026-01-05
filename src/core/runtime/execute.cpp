// ============================================================================
// Runtime - Execute Node Implementation
// Handles the executeNode() method for executing AST nodes
// ============================================================================

#include "runtime.h"
#include "types.h"
#include "parser.h"
#include "lexer.h"
#include "syntax.h"
#include "logger.h"
#include "function_registry.h"

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>

namespace fs = std::filesystem;

static inline void appendDoubleFast(std::string& out, double v) {
    if (std::isnan(v)) {
        out += "nan";
        return;
    }
    if (std::isinf(v)) {
        out += (v < 0) ? "-inf" : "inf";
        return;
    }

    char buf[64];
    auto res = std::to_chars(std::begin(buf), std::end(buf), v, std::chars_format::general);
    if (res.ec != std::errc{}) {
        out += std::to_string(v);
        return;
    }

    char* begin = buf;
    char* end = res.ptr;
    char* ePos = static_cast<char*>(memchr(begin, 'e', end - begin));
    if (!ePos) ePos = static_cast<char*>(memchr(begin, 'E', end - begin));
    char* dotPos = static_cast<char*>(memchr(begin, '.', (ePos ? (ePos - begin) : (end - begin))));
    if (dotPos) {
        char* trimEnd = ePos ? ePos : end;
        while (trimEnd > dotPos + 1 && *(trimEnd - 1) == '0') --trimEnd;
        if (trimEnd > dotPos && *(trimEnd - 1) == '.') --trimEnd;
        out.append(begin, trimEnd - begin);
        if (ePos) out.append(ePos, end - ePos);
        return;
    }
    out.append(begin, end - begin);
}

// Helper to append value to string (avoids repeated branching)
static inline void appendValueRepr(std::string& out, const Value& val) {
    std::visit([&out](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, int>) {
            out += std::to_string(arg);
        } else if constexpr (std::is_same_v<T, double>) {
            appendDoubleFast(out, arg);
        } else if constexpr (std::is_same_v<T, std::string>) {
            out += '"';
            out += arg;
            out += '"';
        } else if constexpr (std::is_same_v<T, bool>) {
            out += arg ? "true" : "false";
        }
    }, val);
}

void Runtime::executeNode(const ASTNodePtr& node) {
    switch (node->type) {
    case NodeType::NoOp:
        return;

    case NodeType::Literal: {
        // Literals as statements are no-ops (no output)
        // Only function calls and explicit print statements should output
        break;
    }

    case NodeType::Identifier: {
        // Identifier as statement is a no-op (just variable lookup, no output)
        break;
    }

    case NodeType::Assign: {
        // Assignment - no output
        std::string varName = std::get<std::string>(node->children[0]->value);
        Value val = evaluate(node->children[1]);
        setVariable(varName, val);
        break;
    }

    case NodeType::Declare: {
        // Declaration - no output
        std::string varName = std::get<std::string>(node->children[0]->value);
        Value val = evaluate(node->children[1]);
        setVariable(varName, val);
        break;
    }

    case NodeType::VarDecl: {
        // New-style variable declaration (let/var)
        // The variable name is stored in node->value, not node->name
        std::string varName;
        if (std::holds_alternative<std::string>(node->value)) {
            varName = std::get<std::string>(node->value);
        } else {
            varName = node->name;
        }
        
        // Check if the value is an array or dict
        if (node->children[0]->type == NodeType::Array) {
            // Store the array with this variable name using ARC allocation
            const auto& elements = node->children[0]->children;
            std::vector<Value> arrayVec;
            arrayVec.reserve(elements.size());
            for (const auto& elem : elements) {
                arrayVec.push_back(evaluate(elem));
            }
            Value arrVal = makeArray(std::move(arrayVec));
            size_t arrayId = std::get<ArrayRef>(arrVal).id;
            varToArrayId[varName] = arrayId;
            setVariable(varName, arrVal);
        } else if (node->children[0]->type == NodeType::Dict) {
            // Store the dict with this variable name using ARC allocation
            const auto& pairs = node->children[0]->children;
            std::unordered_map<std::string, Value> dictMap;
            dictMap.reserve(pairs.size());
            for (const auto& pair : pairs) {
                if (!pair->children.empty()) {
                    dictMap[pair->name] = evaluate(pair->children[0]);
                }
            }
            Value dictVal = makeDict(std::move(dictMap));
            size_t dictId = std::get<DictRef>(dictVal).id;
            varToDictId[varName] = dictId;
            setVariable(varName, dictVal);
        } else if (node->children[0]->type == NodeType::Lambda) {
            // Lambda/closure assignment
            Value lambdaVal = evaluate(node->children[0]);
            if (std::holds_alternative<std::string>(lambdaVal)) {
                std::string lambdaId = std::get<std::string>(lambdaVal);
                varToLambdaId[varName] = lambdaId;
            }
            setVariable(varName, lambdaVal);
        } else {
            // Regular value assignment
            Value val = evaluate(node->children[0]);
            setVariable(varName, val);
        }
        break;
    }

    case NodeType::Binary: {
        // Binary operation as statement - evaluate but no output
        evaluate(node);
        break;
    }

    case NodeType::If: {
        Value condition = evaluate(node->children[0]);
        bool cond = std::visit([](auto&& arg) -> bool {
            if constexpr (std::is_same_v<std::decay_t<decltype(arg)>, bool>) return arg;
            else if constexpr (std::is_arithmetic_v<std::decay_t<decltype(arg)>>) return arg != 0;
            else return false;
        }, condition);
        if (cond) {
            executeNode(node->children[1]);
        } else if (node->children.size() > 2) {
            executeNode(node->children[2]);
        }
        break;
    }

    case NodeType::For: {
        // For loop: for (var in iterable) { ... }
        std::string varName = node->name;
        ASTNodePtr iterable = node->children[0];
        ASTNodePtr body = node->children[1];
        
        // Evaluate iterable (array)
        Value iterValue = evaluate(iterable);
        
        // For now, skip for loop execution (would need array support)
        Logger::instance().log(LogLevel::NOTICE, "For loops not yet fully supported");
        break;
    }

    case NodeType::While: {
        // While loop
        ASTNodePtr condition = node->children[0];
        ASTNodePtr body = node->children[1];
        
        while (true) {
            Value cond = evaluate(condition);
            bool conditionMet = std::visit([](auto&& arg) -> bool {
                if constexpr (std::is_same_v<std::decay_t<decltype(arg)>, bool>) return arg;
                else if constexpr (std::is_arithmetic_v<std::decay_t<decltype(arg)>>) return arg != 0;
                else return false;
            }, cond);
            
            if (!conditionMet) break;
            shouldBreak = false;
            shouldContinue = false;
            executeNode(body);
            
            if (shouldBreak) {
                shouldBreak = false;
                break;
            }
            if (shouldContinue) {
                shouldContinue = false;
                continue;
            }
            if (shouldReturn) {
                // Don't clear shouldReturn - let it propagate
                break;
            }
        }
        break;
    }

    case NodeType::DoWhile: {
        // Do-While loop
        ASTNodePtr body = node->children[0];
        ASTNodePtr condition = node->children[1];
        
        do {
            shouldBreak = false;
            shouldContinue = false;
            executeNode(body);
            
            if (shouldBreak) {
                shouldBreak = false;
                break;
            }
            
            Value cond = evaluate(condition);
            bool conditionMet = std::visit([](auto&& arg) -> bool {
                if constexpr (std::is_same_v<std::decay_t<decltype(arg)>, bool>) return arg;
                else if constexpr (std::is_arithmetic_v<std::decay_t<decltype(arg)>>) return arg != 0;
                else return false;
            }, cond);
            if (!conditionMet) break;
            
            if (shouldContinue) {
                shouldContinue = false;
            }
        } while (true);
        break;
    }

    case NodeType::Break:
        shouldBreak = true;
        break;

    case NodeType::Continue:
        shouldContinue = true;
        break;

    case NodeType::Return: {
        // Return statement - set flag and store value
        shouldReturn = true;
        if (!node->children.empty()) {
            pendingReturnValue = evaluate(node->children[0]);
        } else {
            pendingReturnValue = Value{};
        }
        break;
    }

    case NodeType::Try: {
        // Try-catch-finally statement
        // children[0] = try body
        // children[1] = catch variable (Identifier with type in annotationType)
        // children[2] = catch body
        // children[3] = finally body (optional)
        
        ASTNodePtr tryBody = node->children[0];
        ASTNodePtr catchVar = node->children[1];
        ASTNodePtr catchBody = node->children[2];
        ASTNodePtr finallyBody = (node->children.size() > 3) ? node->children[3] : nullptr;
        
        std::string varName = std::get<std::string>(catchVar->value);
        std::string exceptionType = catchVar->annotationType.name;
        
        bool exceptionCaught = false;
        
        try {
            executeNode(tryBody);
        } catch (const LanguageException& e) {
            // Check if exception type matches (or catch all if type is "Exception")
            if (exceptionType == "Exception" || e.getType() == exceptionType) {
                exceptionCaught = true;
                
                // Create exception object and bind to variable
                // Store exception data in a special format: "Exception:message"
                // Create an object for the exception
                auto excObj = std::make_shared<ObjectInstance>(e.getType());
                excObj->setField("message", Value(e.getMessage()));
                excObj->setField("type", Value(e.getType()));
                
                // Copy any additional fields from the exception
                for (const auto& [fieldName, fieldVal] : e.getFields()) {
                    excObj->setField(fieldName, fieldVal);
                }
                
                objects[varName] = excObj;
                
                executeNode(catchBody);
                
                // Clean up exception variable
                objects.erase(varName);
            } else {
                // Re-throw if type doesn't match
                if (finallyBody) executeNode(finallyBody);
                throw;
            }
        }
        
        // Execute finally block if present
        if (finallyBody) {
            executeNode(finallyBody);
        }
        break;
    }

    case NodeType::Throw: {
        // Throw statement - throws a LanguageException
        // children[0] = expression to throw (new Exception(...) or string or variable)
        
        ASTNodePtr expr = node->children[0];
        
        // Evaluate the expression
        if (expr->type == NodeType::New) {
            // new Exception("message") or new CustomException(...)
            // For New nodes, class name is stored in value, not name
            std::string className = std::get<std::string>(expr->value);
            
            // Evaluate constructor arguments
            std::string message;
            if (!expr->children.empty()) {
                Value msgVal = evaluate(expr->children[0]);
                message = std::visit([](auto&& arg) -> std::string {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, std::string>) return arg;
                    else if constexpr (std::is_same_v<T, int>) return std::to_string(arg);
                    else if constexpr (std::is_same_v<T, double>) return std::to_string(arg);
                    else if constexpr (std::is_same_v<T, bool>) return arg ? "true" : "false";
                    else return "";
                }, msgVal);
            }
            
            LanguageException exc(className, message, node->line, node->column);
            
            // If there are more constructor args, add as fields
            for (size_t i = 1; i < expr->children.size(); ++i) {
                Value fieldVal = evaluate(expr->children[i]);
                exc.setField("arg" + std::to_string(i), fieldVal);
            }
            
            throw exc;
        } else if (expr->type == NodeType::Literal) {
            // throw "error message" - create Exception with message
            Value val = evaluate(expr);
            std::string message = std::visit([](auto&& arg) -> std::string {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::string>) return arg;
                else return "Unknown error";
            }, val);
            throw LanguageException("Exception", message, node->line, node->column);
        } else if (expr->type == NodeType::Identifier) {
            // throw existingException - rethrow an exception object
            std::string varName = std::get<std::string>(expr->value);
            auto it = objects.find(varName);
            if (it != objects.end()) {
                std::string type = it->second->getClassName();
                Value msgVal = it->second->getField("message");
                std::string message = std::visit([](auto&& arg) -> std::string {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, std::string>) return arg;
                    else return "";
                }, msgVal);
                throw LanguageException(type, message, node->line, node->column);
            } else {
                // Maybe it's a string variable
                Value val = getVariable(varName);
                std::string message = std::visit([](auto&& arg) -> std::string {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, std::string>) return arg;
                    else return "Unknown error";
                }, val);
                throw LanguageException("Exception", message, node->line, node->column);
            }
        } else {
            // Evaluate as expression
            Value val = evaluate(expr);
            std::string message = std::visit([](auto&& arg) -> std::string {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::string>) return arg;
                else if constexpr (std::is_same_v<T, int>) return std::to_string(arg);
                else if constexpr (std::is_same_v<T, double>) return std::to_string(arg);
                else if constexpr (std::is_same_v<T, bool>) return arg ? "true" : "false";
                else return "Unknown error";
            }, val);
            throw LanguageException("Exception", message, node->line, node->column);
        }
        break;
    }

    case NodeType::FunctionDef: {
        // Store function definition for later invocation
        // Function name is stored in node->value (as string), not node->name
        std::string funcName;
        if (std::holds_alternative<std::string>(node->value)) {
            funcName = std::get<std::string>(node->value);
        } else {
            funcName = node->name;  // fallback
        }
        StoredFunction storedFunc;
        storedFunc.node = node;
        
        // Extract parameter names from function definition
        // Parameters are stored as children with NodeType::Parameter
        for (const auto& child : node->children) {
            if (child->type == NodeType::Parameter) {
                std::string paramName;
                if (std::holds_alternative<std::string>(child->value)) {
                    paramName = std::get<std::string>(child->value);
                } else {
                    paramName = child->name;
                }
                storedFunc.params.push_back(paramName);
            }
        }
        
        // Store the function by its name
        userFunctions[funcName] = std::move(storedFunc);
        Logger::instance().log(LogLevel::DEBUG, "Function defined: " + funcName + 
                              " with " + std::to_string(storedFunc.params.size()) + " parameters");
        break;
    }

    case NodeType::ClassDef: {
        // Register class definition
        ClassDef classDef;
        classDef.name = node->name;
        classDef.parentClass = node->annotationType.name;  // Stored parent class name
        
        // Parse interfaces from element_type (comma-separated)
        std::string interfaces = node->annotationType.element_type;
        if (!interfaces.empty()) {
            size_t start = 0;
            size_t end;
            while ((end = interfaces.find(',', start)) != std::string::npos) {
                classDef.interfaces.push_back(interfaces.substr(start, end - start));
                start = end + 1;
            }
            classDef.interfaces.push_back(interfaces.substr(start));
        }
        
        // Parse methods, fields, constructor, static members from node->children
        for (const auto& child : node->children) {
            if (child->type == NodeType::GenericType) {
                // Generic type parameters
                for (const auto& typeParam : child->children) {
                    if (typeParam->type == NodeType::TypeParam) {
                        classDef.genericParams.push_back(std::get<std::string>(typeParam->value));
                    }
                }
            } else if (child->type == NodeType::Constructor) {
                // Constructor with parameters
                ConstructorDef constructor;
                constructor.visibility = child->name;
                
                // Extract parameters (children before the last Block)
                for (const auto& param : child->children) {
                    if (param->type == NodeType::Parameter) {
                        std::string paramName = std::get<std::string>(param->value);
                        constructor.parameters.push_back({paramName, param->annotationType});
                    } else if (param->type == NodeType::Block) {
                        if (param->name == "initializers") {
                            // Field initializer list
                            for (const auto& init : param->children) {
                                if (init->type == NodeType::Super) {
                                    constructor.callsSuper = true;
                                    constructor.superArgs = init->children;
                                } else if (init->type == NodeType::Assign) {
                                    constructor.fieldInits.push_back({init->name, init->children[0]});
                                }
                            }
                        } else {
                            constructor.body = param;
                        }
                    }
                }
                
                classDef.constructor = constructor;
                classDef.hasConstructor = true;
            } else if (child->type == NodeType::Method) {
                MethodDef method;
                method.name = std::get<std::string>(child->value);
                method.visibility = child->name;
                method.isStatic = false;
                method.returnType = TypeAnnotation(child->annotationType.name);
                method.isAbstract = child->isMutable;
                
                // Extract parameters and body
                for (const auto& param : child->children) {
                    if (param->type == NodeType::Parameter) {
                        std::string paramName = std::get<std::string>(param->value);
                        method.parameters.push_back({paramName, param->annotationType});
                    } else if (param->type == NodeType::GenericType) {
                        // Method-level generics (skip for now)
                    } else if (param->type == NodeType::Block) {
                        method.body = param;
                    }
                }
                
                classDef.methods.push_back(method);
            } else if (child->type == NodeType::StaticMethod) {
                MethodDef method;
                method.name = std::get<std::string>(child->value);
                method.visibility = child->name;
                method.isStatic = true;
                method.returnType = TypeAnnotation(child->annotationType.name);
                
                for (const auto& param : child->children) {
                    if (param->type == NodeType::Parameter) {
                        std::string paramName = std::get<std::string>(param->value);
                        method.parameters.push_back({paramName, param->annotationType});
                    } else if (param->type == NodeType::Block) {
                        method.body = param;
                    }
                }
                
                classDef.staticMethods.push_back(method);
            } else if (child->type == NodeType::Field) {
                std::string fieldName = std::get<std::string>(child->value);
                classDef.fields.push_back({fieldName, child->annotationType});
                
                // Initialize with default value if provided
                if (!child->children.empty()) {
                    // TODO: store initial value
                }
            } else if (child->type == NodeType::StaticField) {
                std::string fieldName = std::get<std::string>(child->value);
                classDef.staticFields.push_back({fieldName, child->annotationType});
                
                // Initialize static field value
                std::string staticKey = classDef.name + "::" + fieldName;
                if (!child->children.empty()) {
                    Value initVal = evaluate(child->children[0]);
                    staticFields[staticKey] = initVal;
                } else {
                    staticFields[staticKey] = Value{};
                }
            }
        }
        
        classRegistry[classDef.name] = classDef;
        
        // Associate class with current module if loading from a module
        if (!currentLoadingModule.empty()) {
            classToModule[classDef.name] = currentLoadingModule;
            Logger::instance().log(LogLevel::DEBUG, "Class defined: " + classDef.name + " (module: " + currentLoadingModule + ")");
        } else {
            Logger::instance().log(LogLevel::DEBUG, "Class defined: " + classDef.name);
        }
        break;
    }

    case NodeType::InterfaceDef: {
        // Register interface definition
        InterfaceDef interfaceDef;
        interfaceDef.name = node->name;
        
        // Parse extended interfaces from annotationType.name (comma-separated)
        std::string extended = node->annotationType.name;
        if (!extended.empty()) {
            size_t start = 0;
            size_t end;
            while ((end = extended.find(',', start)) != std::string::npos) {
                interfaceDef.extends.push_back(extended.substr(start, end - start));
                start = end + 1;
            }
            interfaceDef.extends.push_back(extended.substr(start));
        }
        
        // Parse generic params and method signatures
        for (const auto& child : node->children) {
            if (child->type == NodeType::GenericType) {
                for (const auto& typeParam : child->children) {
                    if (typeParam->type == NodeType::TypeParam) {
                        interfaceDef.genericParams.push_back(std::get<std::string>(typeParam->value));
                    }
                }
            } else if (child->type == NodeType::Method) {
                MethodDef method;
                method.name = std::get<std::string>(child->value);
                method.visibility = "public";
                method.isAbstract = true;
                method.returnType = TypeAnnotation(child->annotationType.name);
                
                for (const auto& param : child->children) {
                    if (param->type == NodeType::Parameter) {
                        std::string paramName = std::get<std::string>(param->value);
                        method.parameters.push_back({paramName, param->annotationType});
                    }
                }
                
                interfaceDef.methods.push_back(method);
            }
        }
        
        interfaceRegistry[interfaceDef.name] = interfaceDef;
        Logger::instance().log(LogLevel::DEBUG, "Interface defined: " + interfaceDef.name);
        break;
    }

    case NodeType::Block:
        for (const auto& child : node->children) {
            if (shouldBreak || shouldContinue || shouldReturn) break;
            executeNode(child);
        }
        break;

    case NodeType::Array: {
        // Array literal as statement - evaluate but no output
        evaluate(node);
        break;
    }

    case NodeType::Dict: {
        // Dict literal as statement - evaluate but no output
        evaluate(node);
        break;
    }

    case NodeType::Import:
        try {
            std::string importStr = std::get<std::string>(node->value);
            
            // Parse import string format:
            // "path.*" - wildcard import
            // "alias:path.*" - wildcard with alias
            // "path.specific" - specific item import
            // "path:{item1,item2}" - multiple specific items
            // "alias:path:{item1,item2}" - multiple with alias
            
            std::string alias;
            std::string modulePath;
            bool isWildcard = false;
            std::vector<std::string> specificItems;
            
            // First check for specific items with braces (could have alias prefix)
            // Format: "alias:path:{items}" or "path:{items}"
            size_t bracePos = importStr.find(":{");
            if (bracePos != std::string::npos) {
                std::string beforeBrace = importStr.substr(0, bracePos);
                
                // Check if there's an alias before the path
                size_t colonPos = beforeBrace.find(':');
                if (colonPos != std::string::npos) {
                    alias = beforeBrace.substr(0, colonPos);
                    modulePath = beforeBrace.substr(colonPos + 1);
                } else {
                    modulePath = beforeBrace;
                }
                
                // Parse items inside braces
                size_t endBrace = importStr.find('}', bracePos);
                if (endBrace != std::string::npos) {
                    std::string itemsStr = importStr.substr(bracePos + 2, endBrace - bracePos - 2);
                    // Parse comma-separated items
                    size_t pos = 0;
                    while (pos < itemsStr.length()) {
                        size_t commaPos = itemsStr.find(',', pos);
                        if (commaPos == std::string::npos) commaPos = itemsStr.length();
                        specificItems.push_back(itemsStr.substr(pos, commaPos - pos));
                        pos = commaPos + 1;
                    }
                }
            }
            // No braces - check for alias (format: "alias:rest")
            else {
                size_t colonPos = importStr.find(':');
                if (colonPos != std::string::npos) {
                    alias = importStr.substr(0, colonPos);
                    importStr = importStr.substr(colonPos + 1);
                }
                
                // Check for wildcard: path.*
                if (importStr.length() >= 2 && importStr.substr(importStr.length() - 2) == ".*") {
                    modulePath = importStr.substr(0, importStr.length() - 2);
                    isWildcard = true;
                }
                // Specific single item import: system.io.println
                else {
                    // The last component is the specific item
                    size_t lastDot = importStr.rfind('.');
                    if (lastDot != std::string::npos) {
                        modulePath = importStr.substr(0, lastDot);
                        specificItems.push_back(importStr.substr(lastDot + 1));
                    } else {
                        modulePath = importStr;
                        isWildcard = true;  // Just a bare module name = wildcard
                    }
                }
            }
            
            Logger::instance().log(LogLevel::DEBUG, "Import: module=" + modulePath + 
                ", wildcard=" + (isWildcard ? "true" : "false") + 
                ", alias=" + (alias.empty() ? "(none)" : alias) +
                ", items=" + std::to_string(specificItems.size()));
            
            // First, check if this is a loaded extension
            if (isExtensionLoaded(modulePath)) {
                Logger::instance().log(LogLevel::DEBUG, "Import resolved to extension: " + modulePath);
                // Register the import with alias if provided
                if (!alias.empty()) {
                    imports[alias] = modulePath;
                } else if (isWildcard) {
                    // Wildcard without alias - import directly
                    imports[modulePath] = modulePath;
                } else {
                    // Specific items
                    for (const auto& item : specificItems) {
                        imports[item] = modulePath;
                    }
                }
            }
            // Otherwise, try to load as a file-based module
            else if (loadModule(modulePath)) {
                Logger::instance().log(LogLevel::DEBUG, "Import resolved to file module: " + modulePath);
                // Register the import with alias if provided
                if (!alias.empty()) {
                    imports[alias] = modulePath;
                } else {
                    imports[modulePath] = modulePath;
                }
            }
            // Neither extension nor file module found - error
            else {
                std::string errMsg = "Import error: Module '" + modulePath + "' not found. ";
                errMsg += "Checked extensions and directory: " + sourceDirectory + "/" + modulePath;
                Logger::instance().log(LogLevel::ERROR, errMsg);
                state = RuntimeState::HALTED;
                throw std::runtime_error(errMsg);
            }
        } catch(const std::runtime_error&) {
            throw;  // Re-throw runtime errors
        } catch(...) {
            Logger::instance().log(LogLevel::WARNING, "Invalid import value.");
        }
        break;

    case NodeType::Call:
        // Just evaluate the call node - evaluate() handles all the logic
        evaluate(node);
        break;

    default:
        // Unhandled node type
        break;
    }
}
