#ifndef QZ_BYTECODE_VM_H
#define QZ_BYTECODE_VM_H

#include "bytecode.h"
#include "runtime.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class BytecodeVM {
public:
    explicit BytecodeVM(Runtime& runtime) : runtime(runtime) {}

    bool run(const bc::Program& program, std::string* error);

private:
    Runtime& runtime;

    struct BCLambda {
        uint32_t functionIndex = bc::kInvalidIndex;
        std::unordered_map<std::string, Value> captures;
    };

    struct BCMethod {
        uint32_t functionIndex = bc::kInvalidIndex;
        std::vector<std::string> paramNames;
        bool isStatic = false;
    };

    struct BCClass {
        std::string name;
        std::string parent;
        std::vector<std::string> fields;
        std::unordered_map<std::string, BCMethod> methods;
        std::unordered_map<std::string, BCMethod> staticMethods;

        // constructor
        bool hasConstructor = false;
        uint32_t ctorFunctionIndex = bc::kInvalidIndex;
        std::vector<std::string> ctorParams;
        std::vector<std::pair<std::string, uint32_t>> ctorFieldInits; // fieldName -> expr function index

        // static fields
        std::vector<std::string> staticFieldNames;
        std::vector<std::pair<std::string, uint32_t>> staticFieldInitExprs; // fieldName -> expr function index

        // module association
        std::string modulePath;
    };

    const bc::Program* prog = nullptr;

    // Runtime tables for bytecode mode
    std::unordered_map<std::string, BCLambda> lambdas;
    std::unordered_map<std::string, BCClass> classes;
    std::unordered_map<std::string, std::string> classToModule;
    std::unordered_set<std::string> executedModules;

    std::string currentLoadingModule;
    uint32_t nextLambdaId = 0;

    // Execution
    Value runFunction(uint32_t functionIndex, const std::vector<Value>& args,
                      const std::unordered_map<std::string, Value>* overrideVars,
                      const std::string* overrideThis,
                      std::string* error);

    bool evalCondition(const Value& v) const;
    Value applyBinary(const Value& left, const Value& right, bc::BinaryOp op) const;
    Value applyUnary(const Value& operand, bc::UnaryOp op) const;

    // Calls / objects
    Value callName(const std::string& name, const std::vector<Value>& args, std::string* error);
    Value newObject(const std::string& fullClassName, const std::vector<Value>& args, std::string* error);
    Value indexGet(const std::string& varName, const Value& indexValue);

    // Import/module execution
    bool execImportString(const std::string& importStr, std::string* error);
    bool runModuleInit(const std::string& modulePath, std::string* error);

    // Definitions
    bool execDefClass(const std::vector<uint8_t>& code, size_t& ip, std::string* error);
    bool execDefInterface(const std::vector<uint8_t>& code, size_t& ip, std::string* error);

    // Byte reading helpers (advance ip)
    uint8_t readU8(const std::vector<uint8_t>& code, size_t& ip, bool* ok);
    uint16_t readU16(const std::vector<uint8_t>& code, size_t& ip, bool* ok);
    uint32_t readU32(const std::vector<uint8_t>& code, size_t& ip, bool* ok);
    int32_t readI32(const std::vector<uint8_t>& code, size_t& ip, bool* ok);
    double readF64(const std::vector<uint8_t>& code, size_t& ip, bool* ok);

    std::string str(uint32_t stringIndex) const;
};

#endif // QZ_BYTECODE_VM_H
