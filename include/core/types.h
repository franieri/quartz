#ifndef TYPES_H
#define TYPES_H

#include <string>
#include <variant>
#include <vector>
#include <memory>
#include <optional>
#include <map>
#include <stdexcept>

// Opaque handles for runtime-managed containers.
// Arrays/Dicts live in Runtime storage; these values reference them by ID.
struct ArrayRef {
    std::string id;
};

struct DictRef {
    std::string id;
};

// TaskRef: handle to an async task (background work).
// Tasks live in Runtime storage; Quartz can poll/get the result.
struct TaskRef {
    std::string id;
};

// BufferRef: handle to a runtime-managed byte buffer.
// Buffers live in Runtime storage; used for binary I/O operations.
struct BufferRef {
    std::string id;
};

// ============================================================================
// Node Type Enumeration
// ============================================================================

enum class NodeType {
    // Literals and Identifiers
    Literal,
    Identifier,
    
    // Operators
    Binary,
    Unary,
    
    // Statements
    NoOp,
    Assign,
    Declare,
    VarDecl,
    Block,
    Return,
    Break,
    Continue,
    
    // Control Flow
    If,
    For,
    While,
    DoWhile,
    Loop,
    
    // Exception Handling
    Try,
    Throw,
    
    // Functions and Classes
    FunctionDef,
    ClassDef,
    Method,
    Field,
    Parameter,
    Call,
    New,
    Constructor,      // Constructor definition
    
    // Lambdas/Closures
    Lambda,           // Lambda expression
    Capture,          // Captured variable in closure
    
    // OOP Advanced
    InterfaceDef,     // Interface definition
    StaticField,      // Static class field
    StaticMethod,     // Static class method
    Super,            // Super class reference
    
    // Generics
    GenericType,      // Generic type parameter
    TypeParam,        // Type parameter in definition
    
    // Data Structures
    Array,
    Dict,
    Pair,
    Index,
    
    // String Interpolation
    InterpolatedString,
    StringPart,
    InterpExpr,
    
    // Module System
    Import,
    
    // Error
    Error
};

// Convert NodeType to string for debugging
inline const char* nodeTypeToString(NodeType type) {
    switch (type) {
        case NodeType::Literal: return "Literal";
        case NodeType::Identifier: return "Identifier";
        case NodeType::Binary: return "Binary";
        case NodeType::Unary: return "Unary";
        case NodeType::NoOp: return "NoOp";
        case NodeType::Assign: return "Assign";
        case NodeType::Declare: return "Declare";
        case NodeType::VarDecl: return "VarDecl";
        case NodeType::Block: return "Block";
        case NodeType::Return: return "Return";
        case NodeType::Break: return "Break";
        case NodeType::Continue: return "Continue";
        case NodeType::If: return "If";
        case NodeType::For: return "For";
        case NodeType::While: return "While";
        case NodeType::DoWhile: return "DoWhile";
        case NodeType::Loop: return "Loop";
        case NodeType::Try: return "Try";
        case NodeType::Throw: return "Throw";
        case NodeType::FunctionDef: return "FunctionDef";
        case NodeType::ClassDef: return "ClassDef";
        case NodeType::Method: return "Method";
        case NodeType::Field: return "Field";
        case NodeType::Parameter: return "Parameter";
        case NodeType::Call: return "Call";
        case NodeType::New: return "New";
        case NodeType::Constructor: return "Constructor";
        case NodeType::Lambda: return "Lambda";
        case NodeType::Capture: return "Capture";
        case NodeType::InterfaceDef: return "InterfaceDef";
        case NodeType::StaticField: return "StaticField";
        case NodeType::StaticMethod: return "StaticMethod";
        case NodeType::Super: return "Super";
        case NodeType::GenericType: return "GenericType";
        case NodeType::TypeParam: return "TypeParam";
        case NodeType::Array: return "Array";
        case NodeType::Dict: return "Dict";
        case NodeType::Pair: return "Pair";
        case NodeType::Index: return "Index";
        case NodeType::InterpolatedString: return "InterpolatedString";
        case NodeType::StringPart: return "StringPart";
        case NodeType::InterpExpr: return "InterpExpr";
        case NodeType::Import: return "Import";
        case NodeType::Error: return "Error";
        default: return "Unknown";
    }
}

// ============================================================================
// Type System
// ============================================================================

// Forward declarations
struct ASTNode;
struct FunctionDef;
struct TypeAnnotation;

using ASTNodePtr = std::shared_ptr<ASTNode>;

// Type representation for annotations
struct TypeAnnotation {
    std::string name;           // "int", "string", "bool", "double", "void"
    bool is_optional = false;   // Type?
    bool is_array = false;      // Type[]
    bool is_result = false;     // Result<T, E>
    bool is_generic = false;    // T, K, V etc.
    bool is_function = false;   // (int, int) -> int (lambda type)
    std::string element_type;   // For arrays: element type name
    std::string error_type;     // For Result: error type name
    bool is_immutable = true;   // let = true, var = false
    std::vector<std::string> generic_params;  // Generic type parameters <T, U>
    std::vector<TypeAnnotation> param_types;  // For function types: parameter types
    std::shared_ptr<TypeAnnotation> return_type;  // For function types: return type
    
    TypeAnnotation() = default;
    TypeAnnotation(const std::string& t) : name(t) {}
    
    std::string toString() const;
    bool isCompatible(const TypeAnnotation& other) const;
    bool isGenericParameter() const { return is_generic && generic_params.empty(); }
};

// Runtime Value type
using Value = std::variant<int, double, std::string, bool, ArrayRef, DictRef, TaskRef, BufferRef>;

// Optional value (implements Option<T>)
template<typename T>
struct Option {
    bool has_value = false;
    T value = T();
    
    Option() = default;
    Option(const T& v) : has_value(true), value(v) {}
    
    static Option Some(const T& v) { return Option(v); }
    static Option None() { return Option(); }
    
    bool isSome() const { return has_value; }
    bool isNone() const { return !has_value; }
    T unwrap() const { 
        if (!has_value) throw std::runtime_error("Attempted to unwrap None");
        return value;
    }
};

// Result type (implements Result<T, E>)
template<typename T, typename E>
struct Result {
    bool is_ok = false;
    T ok_value = T();
    E err_value = E();
    
    Result() = default;
    Result(const T& v) : is_ok(true), ok_value(v) {}
    Result(const E& e, bool) : is_ok(false), err_value(e) {}
    
    static Result Ok(const T& v) { return Result(v); }
    static Result Err(const E& e) { return Result(e, false); }
    
    bool isOk() const { return is_ok; }
    bool isErr() const { return !is_ok; }
    T unwrap() const {
        if (!is_ok) throw std::runtime_error("Attempted to unwrap Err");
        return ok_value;
    }
    E unwrapErr() const {
        if (is_ok) throw std::runtime_error("Attempted to unwrapErr on Ok");
        return err_value;
    }
};

// ============================================================================
// AST Structures
// ============================================================================

struct ASTNode {
    NodeType type;
    Value value;
    std::vector<ASTNodePtr> children;
    TypeAnnotation annotationType;
    bool isMutable = false;  // true for 'var', false for 'let'
    std::string name;        // For identifiers, function names, etc.
    int line = -1;           // Line number for error reporting
    int column = -1;         // Column number for error reporting
    
    // Constructors
    ASTNode() : type(NodeType::NoOp), value(), annotationType() {}
    ASTNode(NodeType t) : type(t), value(), annotationType() {}
    ASTNode(NodeType t, const Value& v) : type(t), value(v), annotationType() {}
    
    // Helper to get type as string (for debugging/logging)
    const char* typeString() const { return nodeTypeToString(type); }
};

struct FunctionDef {
    std::string name;
    std::vector<std::pair<std::string, TypeAnnotation>> parameters;  // (param_name, type)
    TypeAnnotation returnType;
    ASTNodePtr body;
    bool isMutable = false;
    int line = -1;
};

// Method definition (member function)
struct MethodDef {
    std::string name;
    std::vector<std::pair<std::string, TypeAnnotation>> parameters;
    TypeAnnotation returnType;
    ASTNodePtr body;
    bool isStatic = false;
    bool isAbstract = false;
    bool isVirtual = false;   // Can be overridden
    bool isOverride = false;  // Overrides parent method
    std::string visibility;   // "public", "private", "protected"
    int line = -1;
};

// Constructor definition
struct ConstructorDef {
    std::vector<std::pair<std::string, TypeAnnotation>> parameters;
    ASTNodePtr body;
    std::string visibility = "public";
    std::vector<std::pair<std::string, ASTNodePtr>> fieldInits;  // Field initializer list
    bool callsSuper = false;
    std::vector<ASTNodePtr> superArgs;  // Arguments to super()
    int line = -1;
};

// Interface definition
struct InterfaceDef {
    std::string name;
    std::vector<MethodDef> methods;  // Abstract method signatures
    std::vector<std::string> extends;  // Extended interfaces
    std::vector<std::string> genericParams;  // Generic type parameters
    int line = -1;
};

// Lambda/Closure definition
struct LambdaDef {
    std::vector<std::pair<std::string, TypeAnnotation>> parameters;
    TypeAnnotation returnType;
    ASTNodePtr body;
    std::vector<std::string> captures;  // Captured variables from enclosing scope
    bool isExpression = false;  // Single expression body vs block
    int line = -1;
};

// Class definition
struct ClassDef {
    std::string name;
    std::string parentClass;  // For inheritance (extends)
    std::vector<std::pair<std::string, TypeAnnotation>> fields;  // Instance fields
    std::vector<std::pair<std::string, TypeAnnotation>> staticFields;  // Static fields
    std::vector<MethodDef> methods;  // Instance methods
    std::vector<MethodDef> staticMethods;  // Static methods
    ConstructorDef constructor;  // Constructor with parameters
    bool hasConstructor = false;
    std::vector<std::string> interfaces;  // Implemented interfaces
    std::vector<std::string> genericParams;  // Generic type parameters <T, U>
    bool isAbstract = false;
    int line = -1;
};

// AST structure
struct AST {
    std::vector<ASTNodePtr> nodes;
    std::vector<FunctionDef> functions;  // Function definitions
    std::vector<ClassDef> classes;  // Class definitions
    std::map<std::string, TypeAnnotation> variables;  // Variable type registry
};

// ============================================================================
// Type Checking & Utilities
// ============================================================================

namespace ValueUtils {
    // Type checking predicates
    inline bool is_int(const Value& v) {
        return std::holds_alternative<int>(v);
    }
    
    inline bool is_double(const Value& v) {
        return std::holds_alternative<double>(v);
    }
    
    inline bool is_string(const Value& v) {
        return std::holds_alternative<std::string>(v);
    }
    
    inline bool is_bool(const Value& v) {
        return std::holds_alternative<bool>(v);
    }
    
    inline bool is_numeric(const Value& v) {
        return is_int(v) || is_double(v);
    }
    
    // Safe extraction with defaults
    inline int get_int(const Value& v, int default_val = 0) {
        if (is_int(v)) return std::get<int>(v);
        if (is_double(v)) return static_cast<int>(std::get<double>(v));
        if (is_bool(v)) return std::get<bool>(v) ? 1 : 0;
        return default_val;
    }
    
    inline double get_double(const Value& v, double default_val = 0.0) {
        if (is_double(v)) return std::get<double>(v);
        if (is_int(v)) return static_cast<double>(std::get<int>(v));
        if (is_bool(v)) return std::get<bool>(v) ? 1.0 : 0.0;
        return default_val;
    }
    
    inline std::string get_string(const Value& v, const std::string& default_val = "") {
        if (is_string(v)) return std::get<std::string>(v);
        return default_val;
    }
    
    inline bool get_bool(const Value& v, bool default_val = false) {
        if (is_bool(v)) return std::get<bool>(v);
        if (is_int(v)) return std::get<int>(v) != 0;
        if (is_double(v)) return std::get<double>(v) != 0.0;
        if (is_string(v)) return !std::get<std::string>(v).empty();
        return default_val;
    }
    
    inline std::string get_type_name(const Value& v) {
        return std::visit([](auto&& arg) -> std::string {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, int>) return "int";
            else if constexpr (std::is_same_v<T, double>) return "double";
            else if constexpr (std::is_same_v<T, std::string>) return "string";
            else if constexpr (std::is_same_v<T, bool>) return "bool";
            else if constexpr (std::is_same_v<T, ArrayRef>) return "array";
            else if constexpr (std::is_same_v<T, DictRef>) return "dict";
            else return "unknown";
        }, v);
    }
}

// Defined in src/core/types.cpp (runtime-aware for ArrayRef/DictRef)
std::string to_string(const Value& v);

#endif // TYPES_H
