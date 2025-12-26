# Extension Development Guide

## Overview

Extensions are dynamically-loaded libraries that add functionality to the Quartz language at runtime. They are implemented as C++ shared libraries (.dylib on macOS) and are discovered and loaded automatically when the language runtime starts.

## Extension Architecture

### Loading Mechanism

1. **Discovery**: Runtime scans `build/extensions/` directory recursively for `.dylib` files
2. **Loading**: Uses `dlopen()` with `RTLD_LAZY | RTLD_GLOBAL` flags
3. **Initialization**: Calls `_init_extension(FunctionRegistry&)` function
4. **Registration**: Extension registers functions with FunctionRegistry

### Symbol Resolution

On macOS:
- Function symbols use C naming convention with underscore prefix: `_init_extension`
- Visibility set to default with `-fvisibility=default` compiler flag
- Global RTLD flag ensures symbols are visible to other loaded libraries

## Creating an Extension

### Step 1: Create Extension Directory

```bash
mkdir extensions/myext
cd extensions/myext
```

### Step 2: Create CMakeLists.txt

```cmake
file(GLOB SOURCES *.cpp)

set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "${CMAKE_BINARY_DIR}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=default")

add_library(myext SHARED ${SOURCES})
target_include_directories(myext PRIVATE ../../include/core ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(myext PRIVATE qz-core)
```

### Step 3: Create Extension Source Files

**myext.cpp** - Main extension file with initialization:

```cpp
#include "function_registry.h"
#include "types.h"
#include <iostream>

// Forward declarations for function registration
void register_myext_functions(FunctionRegistry& reg);

extern "C" __attribute__((visibility("default"))) void init_extension(FunctionRegistry& reg) {
    register_myext_functions(reg);
}
```

**functions.cpp** - Function implementations:

```cpp
#include "function_registry.h"
#include "types.h"
#include <iostream>

void register_myext_functions(FunctionRegistry& reg) {
    // Example function
    auto greet = [](const std::vector<Value>& args) -> Value {
        std::cout << "Hello from myext!" << std::endl;
        return Value();
    };
    
    reg.registerFunction("myext.greet", greet);
    
    // Function with arguments
    auto add = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(0);
        if (std::holds_alternative<int>(args[0]) && std::holds_alternative<int>(args[1])) {
            return Value(std::get<int>(args[0]) + std::get<int>(args[1]));
        }
        return Value(0);
    };
    
    reg.registerFunction("myext.add", add);
}
```

**metadata.json** - Extension metadata:

```json
{
    "name": "myext",
    "version": "1.0.0",
    "description": "My custom extension",
    "functions": [
        {
            "name": "myext.greet",
            "description": "Greet function",
            "params": [],
            "returns": "void"
        },
        {
            "name": "myext.add",
            "description": "Add two integers",
            "params": ["int", "int"],
            "returns": "int"
        }
    ]
}
```

### Step 4: Update Parent CMakeLists.txt

If creating a new extension, ensure `extensions/CMakeLists.txt` includes it:

```cmake
add_subdirectory(myext)
```

## Function Registration

### Function Signature

All registered functions must have this signature:

```cpp
std::function<Value(const std::vector<Value>&)>
```

### Supported Value Types

From `types.h`:

```cpp
using Value = std::variant<int, double, std::string, bool>;
```

### Return Value Handling

**Critical: All function calls must return a Value.** Return values flow through the runtime's evaluation pipeline and can be:
- **Assigned to variables**: `string w = io.stdin.readln();`
- **Used in expressions**: `int sum = math.add(2, 3) + 5;`
- **Passed to other functions**: `io.out.println("Result:", compute.getValue());`

The return value lifecycle:
1. Function is invoked with evaluated arguments
2. Function executes and returns a `Value`
3. Runtime's `evaluate()` captures the return value
4. Value can be assigned to variables or used in expressions
5. Never discard return values from functions that compute results

### String Return Values

String returns are especially important - they enable data flow back from I/O operations:

```cpp
// Correct: Wrap result in Value
auto readln = [](const std::vector<Value>& args) -> Value {
    std::string line;
    std::getline(std::cin, line);
    return Value(line);  // IMPORTANT: Return the string value
};
```

```julia
// In Quartz - assignment works because readln() returns Value
string input = io.stdin.readln();
io.out.println("You entered:", input);
```

### Function Example

```cpp
auto my_function = [](const std::vector<Value>& args) -> Value {
    // Check argument count
    if (args.size() < 1) {
        return Value(0);  // Return default, don't throw
    }
    
    // Extract and process arguments
    if (std::holds_alternative<int>(args[0])) {
        int value = std::get<int>(args[0]);
        // Process...
        return Value(result);  // Return computed value
    }
    
    return Value();  // Return empty if type mismatch
};
```

## API Stability & Best Practices

### 1. Naming Conventions

Use hierarchical namespace naming:
```
extension_name.category.function
system.io.stdin.readln
system.io.out.println
```

### 2. Argument Validation

Always validate arguments before using them:

```cpp
auto add = [](const std::vector<Value>& args) -> Value {
    // Check count
    if (args.size() < 2) return Value(0);
    
    // Check types
    if (!std::holds_alternative<int>(args[0]) ||
        !std::holds_alternative<int>(args[1])) {
        return Value(0);
    }
    
    int a = std::get<int>(args[0]);
    int b = std::get<int>(args[1]);
    return Value(a + b);
};
```

### 3. Error Handling

Return default values instead of throwing exceptions:

```cpp
// GOOD: Return sensible default
auto parse = [](const std::vector<Value>& args) -> Value {
    try {
        return Value(parse_result);
    } catch(...) {
        return Value(std::string(""));  // Empty string = error
    }
};

// BAD: Exception halts the program
auto bad_func = [](const std::vector<Value>& args) -> Value {
    throw std::runtime_error("Error!");  // Causes halt
};
```

### 4. No Debug Output in Extensions

Keep extensions clean - do not use Logger or cout:

```cpp
// GOOD: Silent operation
auto readln = [](const std::vector<Value>& args) -> Value {
    std::string line;
    std::getline(std::cin, line);
    return Value(line);
};

// BAD: Debug output pollutes execution
auto bad_readln = [](const std::vector<Value>& args) -> Value {
    std::cerr << "Reading line..." << std::endl;  // Don't do this
    std::string line;
    std::getline(std::cin, line);
    return Value(line);
};
```

### 5. Pure vs Side-Effect Functions

Design clearly:

```cpp
// Side effect (I/O) - acceptable with clear purpose
auto println = [](const std::vector<Value>& args) -> Value {
    for (const auto& arg : args) {
        std::cout << to_string(arg) << " ";
    }
    std::cout << std::endl;
    return Value();  // Return empty
};

// Pure computation - preferred when possible
auto add = [](const std::vector<Value>& args) -> Value {
    // No I/O, no state changes, deterministic
    return Value(std::get<int>(args[0]) + std::get<int>(args[1]));
};
```

## Building Extensions

### Automatic Build

```bash
bash rebuild_core.sh
# Automatically rebuilds all extensions
```

### Manual Build

```bash
cd build
cmake ..
cmake --build . --target myext
```

## Testing Your Extension

### Sample Program

Create `samples/test_myext.qz`:

```java
import myext;

myext.greet();

int result = myext.add(3, 5);
system.io.println("Result:", result);
```

### Run Test

```bash
./build/quartz samples/test_myext.qz
```

## Extension Examples

### system_io Extension

Location: `extensions/system_io/`

Functions:
- `system.io.println(...)` - Print with newline
- `system.io.out.println(...)` - Alternative name
- `system.io.stdin.readln()` - Read line from input

### structure.txt Extension

Location: `extensions/sample_ext/`

A simple example extension for reference.

## Debugging Extensions

### Enable Debug Output

Use Logger in your extension:

```cpp
#include "logger.h"

Logger::instance().log(LogLevel::DEBUG, "Extension loaded");
```

### Run Under Debugger

```bash
lldb ./build/quartz
(lldb) run samples/test_myext.qz
(lldb) run samples/test_myext.qz
```

### Check Symbols

```bash
nm -D build/extensions/myext/libmyext.dylib
```

Should show `_init_extension` symbol as `T` (text/code symbol).

## Common Issues

### Extension not loading

**Problem**: Function throws "Unknown function" error

**Solutions**:
1. Check `.dylib` exists in build directory
2. Verify `init_extension` symbol exists: `nm -D libmyext.dylib | grep init`
3. Check error with `dlerror()` in runtime output
4. Ensure extension was rebuilt: `bash scripts/rebuild_core.sh`

### Symbol not found

**Problem**: `dlsym` fails to find `_init_extension`

**Solutions**:
1. Ensure `extern "C"` and visibility attribute are present
2. Recompile with `-fvisibility=default`
3. Check symbol naming matches (with underscore on macOS)

### Function returns wrong type

**Problem**: Function compiles but returns incorrect result type

**Solutions**:
1. Verify return type is `Value`
2. Check lambda captures correct variables
3. Use `std::holds_alternative<T>()` to check types before accessing

## Robustness Patterns

### Input Validation

Always validate argument count and types at the start of functions:

```cpp
auto safe_add = [](const std::vector<Value>& args) -> Value {
    // Check minimum argument count
    if (args.size() < 2) {
        return Value(0);  // Return sensible default
    }
    
    // Check types
    using namespace ValueUtils;
    if (!is_int(args[0]) || !is_int(args[1])) {
        return Value(0);  // Type mismatch - return default
    }
    
    return Value(get_int(args[0]) + get_int(args[1]));
};
```

Use the `ValueUtils` helpers from `types.h`:
- `is_int(v)`, `is_double(v)`, `is_string(v)`, `is_bool(v)` - type checking
- `is_numeric(v)` - check for int or double
- `get_int(v, default)`, `get_double(v, default)`, `get_string(v, default)`, `get_bool(v, default)` - safe extraction
- `get_type_name(v)` - get human-readable type name

### Exception Handling

Functions must not throw exceptions - they break the runtime:

```cpp
// GOOD: Return error value
auto parse_json = [](const std::vector<Value>& args) -> Value {
    try {
        // parsing logic
        return Value(result);
    } catch (const std::exception& e) {
        // Return empty string to indicate parse failure
        return Value(std::string(""));
    }
};

// ALSO GOOD: Log and return default
auto dangerous_op = [](const std::vector<Value>& args) -> Value {
    try {
        // potentially dangerous operation
        return Value(result);
    } catch (...) {
        // Broad catch to be extra safe
        return Value(0);
    }
};
```

### Function Name Validation

Function names are validated by the registry - use the hierarchical namespace pattern:

```cpp
// Names are validated: must be identifiers separated by dots
reg.registerFunction("namespace.submodule.function", func);  // Valid
reg.registerFunction("math.arithmetic.add", func);            // Valid
reg.registerFunction("io.file.read", func);                   // Valid

// Invalid names are rejected silently:
reg.registerFunction("math-arithmetic.add", func);           // Invalid char
reg.registerFunction("", func);                              // Empty
reg.registerFunction("math.", func);                         // Invalid structure
```

### State Management

Extensions can maintain internal state through lambda captures:

```cpp
void register_counter_functions(FunctionRegistry& reg) {
    // Shared mutable state (be careful!)
    auto counter = std::make_shared<int>(0);
    
    auto increment = [counter](const std::vector<Value>& args) -> Value {
        (*counter)++;
        return Value(*counter);
    };
    
    auto get_count = [counter](const std::vector<Value>& args) -> Value {
        return Value(*counter);
    };
    
    reg.registerFunction("counter.inc", increment);
    reg.registerFunction("counter.get", get_count);
}
```

**Warning**: Shared state across functions can cause issues. Use sparingly and document clearly.

### Numeric Operations

When working with numbers, be explicit about type handling:

```cpp
auto divide = [](const std::vector<Value>& args) -> Value {
    using namespace ValueUtils;
    
    if (args.size() < 2) return Value(0.0);
    if (!is_numeric(args[0]) || !is_numeric(args[1])) return Value(0.0);
    
    // Convert to double for division
    double a = is_double(args[0]) ? get_double(args[0]) : (double)get_int(args[0]);
    double b = is_double(args[1]) ? get_double(args[1]) : (double)get_int(args[1]);
    
    if (b == 0.0) return Value(0.0);  // Avoid division by zero
    return Value(a / b);
};
```

### String Operations

String results are common for I/O operations:

```cpp
auto concatenate = [](const std::vector<Value>& args) -> Value {
    using namespace ValueUtils;
    
    std::string result;
    for (const auto& arg : args) {
        result += to_string(arg);  // Converts any Value to string
    }
    return Value(result);
};
```

## Performance Considerations

- Functions are called with vector copies (reasonable for small arg counts)
- Use lambda captures for state if needed
- Avoid heavy processing in registration (called once at startup)

## API Reference

### FunctionRegistry

```cpp
static FunctionRegistry& instance();
void registerFunction(const std::string& name, 
                     std::function<Value(const std::vector<Value>&)> f);
bool exists(const std::string& name) const;
Value call(const std::string& name, const std::vector<Value>& args);

// Diagnostics (new)
size_t getFunctionCount() const;
std::vector<std::string> listFunctions() const;
std::vector<std::string> findFunctions(const std::string& prefix) const;
bool validateFunctionName(const std::string& name) const;
```

### Logger

```cpp
Logger::instance().log(LogLevel level, const std::string& message);
```

LogLevel values: `DEBUG, INFO, NOTICE, WARNING, ERROR`

### Runtime Diagnostics

The Runtime class now provides introspection:

```cpp
// Variable management
bool hasVariable(const std::string& name) const;
void clearVariables();
size_t getVariableCount() const;
std::vector<std::string> listVariables() const;

// Import management
bool hasImport(const std::string& alias) const;
```

## Security Considerations

- Extensions run with same privileges as main process
- Validate all inputs from user code
- Sanitize output if needed
- No sandboxing or isolation
