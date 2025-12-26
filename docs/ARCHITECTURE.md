# Architecture Overview

## System Design

The Quartz language consists of four main components:

```
Source Code
    ↓
┌─────────────────────────────────────┐
│         LEXER (Tokenizer)           │
│  Converts source to token stream    │
└─────────────────────────────────────┘
    ↓
Token Stream
    ↓
┌─────────────────────────────────────┐
│      PARSER (AST Generator)         │
│  Converts tokens to abstract tree   │
└─────────────────────────────────────┘
    ↓
Abstract Syntax Tree (AST)
    ↓
┌─────────────────────────────────────┐
│     RUNTIME (Interpreter)           │
│  Executes AST, manages state        │
│  Loads extensions dynamically       │
└─────────────────────────────────────┘
    ↓
Program Output
```

## Component Details

### 1. Lexer (src/core/lexer.cpp)

**Responsibility**: Tokenization

**Key Classes**:
- `Lexer`: Main lexer class

**Key Methods**:
- `tokenize()` - Convert source code to tokens
- `scanToken()` - Scan single token
- `skipWhitespace()` - Handle whitespace
- `scanString()` - Parse string literals
- `scanNumber()` - Parse numeric literals

**Output**: Vector of `Token` objects

**Token Types**:
- Keywords: `if`, `else`, `while`, `for`, `import`, `as`, etc.
- Operators: `+`, `-`, `*`, `/`, `=`, `==`, `!=`, `<`, `>`, etc.
- Literals: integers, doubles, strings, identifiers
- Delimiters: `(`, `)`, `{`, `}`, `;`, `,`, `.`

### 2. Parser (src/core/parser.cpp)

**Responsibility**: Syntax analysis and AST construction

**Key Classes**:
- `Parser`: Recursive descent parser
- `ASTNode`: AST node structure
- `AST`: Root of the tree

**Parsing Strategy**: Recursive descent with operator precedence

**Grammar (Simplified)**:
```
program     → statement*
statement   → if | while | for | import | expression ";"
expression  → assignment
assignment  → equality ("=" assignment)?
equality    → comparison (("==" | "!=") comparison)*
comparison  → term (("<" | "<=" | ">" | ">=") term)*
term        → factor (("+" | "-") factor)*
factor      → unary (("*" | "/") unary)*
unary       → ("!" | "-") unary | postfix
postfix     → primary ("(" arguments? ")" | "." IDENTIFIER)*
primary     → NUMBER | STRING | IDENTIFIER | "(" expression ")"
```

**Key Methods**:
- `parse()` - Main entry point
- `statement()` - Parse statement
- `expression()` - Parse expression
- `assignment()`, `equality()`, `comparison()`, etc. - Precedence levels

### 3. Runtime (src/core/runtime.cpp)

**Responsibility**: AST execution and state management

**Key Classes**:
- `Runtime`: Execution engine
- `RuntimeState`: Execution state enum
- `Value`: Variant type for values

**Key Features**:
- Variable storage (stack)
- Function registry access
- Control flow (halt states)
- Extension loading

**Key Methods**:
- `execute(AST)` - Execute program
- `executeNode(ASTNode)` - Execute single node
- `evaluate(ASTNode)` - Evaluate expression
- `setVariable()` / `getVariable()` - Variable management
- `loadExtensions()` - Load dynamic extensions
- `initStandardLibrary()` - Initialize runtime

**Value Type**:
```cpp
using Value = std::variant<int, double, std::string, bool>;
```

**Node Types Supported**:
- Program nodes
- Variable declarations
- Assignments
- Control flow (If, While, For)
- Function calls
- Binary/unary operations
- Literals
- Identifiers

### 4. Function Registry (src/core/function_registry.cpp)

**Responsibility**: Function storage and lookup

**Key Classes**:
- `FunctionRegistry`: Registry singleton

**Architecture**:
- Global pointer: `extern FunctionRegistry* global_reg_ptr`
- Lazy initialization on first access
- Shared across main executable and extensions

**Key Methods**:
- `instance()` - Get singleton instance
- `registerFunction()` - Register function
- `exists()` - Check if function exists
- `call()` - Call function by name

**Function Storage**:
```cpp
std::unordered_map<std::string, 
    std::function<Value(const std::vector<Value>&)>> funcs;
```

## Data Flow

### Typical Execution Flow

1. **Startup**
   - Main reads source file
   - Creates Lexer with source
   - Calls `tokenize()` → tokens

2. **Parsing**
   - Creates Parser with tokens
   - Calls `parse()` → AST
   - Validates syntax errors

3. **Initialization**
   - Creates Runtime
   - Calls `initStandardLibrary()`
   - `loadExtensions()` loads dynamic libraries
   - Extensions register functions

4. **Execution**
   - Calls `runtime.execute(ast)`
   - `executeNode()` recursively processes AST
   - Functions looked up in registry
   - Variables stored in runtime map
   - Output produced

5. **Completion**
   - Return success/failure status

### Extension Loading Details

```
build/extensions/system_io/libsystem_io.dylib
    ↓
dlopen(path, RTLD_LAZY | RTLD_GLOBAL)
    ↓
dlsym(handle, "_init_extension")
    ↓
init_extension(FunctionRegistry::instance())
    ↓
register_stdin_functions(reg)
register_stdout_functions(reg)
    ↓
Functions available: system.io.stdin.readln, system.io.println, etc.
```

## Key Design Decisions

### 1. Recursive Descent Parsing

**Reason**: Simple, easy to understand and modify

**Trade-off**: Not as efficient as LR parsers, but adequate for language size

### 2. Tree-Walking Interpreter

**Reason**: Direct AST execution without intermediate bytecode

**Trade-off**: Slower than bytecode VM, but simpler implementation

### 3. Global Function Registry

**Reason**: Enable extensions to register functions from different modules

**Trade-off**: Not thread-safe, but extensions load once at startup

### 4. Dynamic Extension Loading

**Reason**: Modularity and extensibility without recompiling core

**Trade-off**: Platform-specific (uses dlopen/dlsym)

### 5. Parser Defers Function Validation

**Reason**: Support dynamic functions registered at runtime

**Trade-off**: Runtime errors instead of compile-time checks

## Type System

### Value Representation

```cpp
using Value = std::variant<int, double, std::string, bool>;

// Checking type:
if (std::holds_alternative<int>(value)) {
    int v = std::get<int>(value);
}

// Conversion:
std::string to_string(const Value& v);
```

### No Implicit Conversion

Values maintain their types explicitly:
- `5` is int, not double
- `"5"` is string, not int
- Type mismatches handled at runtime

## Memory Management

### Stack-Based Variables

```cpp
std::unordered_map<std::string, Value> variables;
```

- Variables stored in runtime's map
- Scoped to program execution
- No separate function scopes

### Function Storage

```cpp
std::function<Value(const std::vector<Value>&)>
```

- Functions are lambdas with captures
- Stored in registry map
- Persist for program lifetime

### Extension Libraries

- Loaded with `dlopen()` (handles ownership)
- Not explicitly unloaded (leak acceptable for small projects)
- Symbols remain available for program duration

## Thread Safety

**Current Status**: Not thread-safe

- Single global registry
- No mutex protection
- Extensions load serially at startup
- Suitable for single-threaded use only

## Platform Dependencies

### macOS-Specific
- Symbol naming: underscore prefix `_init_extension`
- Library format: `.dylib` (Mach-O)
- dlopen availability
- RPATH load command

### Portable Elements
- C++17 standard library
- Filesystem (std::filesystem)
- Core parsing logic

## Performance Characteristics

### Parsing
- O(n) where n = source length
- Single pass token consumption

### Execution
- Direct AST traversal (no optimization)
- O(1) variable lookup (hash map)
- O(1) function lookup (hash map)
- Overhead: virtual function calls, variant visits

### Scalability
- Suitable for small-to-medium programs
- No optimization passes
- Could handle hundreds of lines efficiently

## Future Architecture Improvements

1. **Bytecode Compiler**: Intermediate representation for faster execution
2. **Scope Management**: Proper lexical scoping with call stack
3. **Type Checking**: Compile-time or runtime type validation
4. **Error Recovery**: Better error messages with recovery
5. **Optimization**: Constant folding, dead code elimination
6. **JIT Compilation**: Runtime code generation for hot paths

