# Quartz Standard Library Documentation

## Overview
Quartz includes a comprehensive standard library organized into system modules. All modules are loaded automatically at runtime.

## Available Modules

### 1. system.io
Input/Output operations for console communication.

**Functions:**
- `println(args...)` - Print arguments to console with newline
  - Example: `io.out.println("Hello", "World");`

**Usage:**
```quartz
import system.io as io;
io.out.println("Hello, Quartz!");
```

---

### 2. system.math
Mathematical operations and trigonometric functions.

**Functions:**
- `abs(x)` - Absolute value
- `sqrt(x)` - Square root
- `pow(base, exp)` - Power function
- `floor(x)` - Floor (round down)
- `ceil(x)` - Ceiling (round up)
- `round(x)` - Round to nearest integer
- `min(a, b)` - Minimum of two values
- `max(a, b)` - Maximum of two values
- `sin(x)` - Sine function
- `cos(x)` - Cosine function

**Usage:**
```quartz
import system.math as math;
let x = math.sqrt(16);           // x = 4.0
let y = math.pow(2, 3);          // y = 8.0
let z = math.max(5, 3);          // z = 5
```

---

### 3. system.string
String manipulation and analysis functions.

**Functions:**
- `length(str)` - Get string length
- `uppercase(str)` or `toUpperCase(str)` - Convert to uppercase
- `lowercase(str)` or `toLowerCase(str)` - Convert to lowercase
- `trim(str)` - Remove leading/trailing whitespace
- `contains(str, substr)` - Check if string contains substring
- `startsWith(str, prefix)` - Check if string starts with prefix
- `endsWith(str, suffix)` - Check if string ends with suffix
- `substring(str, start, end?)` - Extract substring
- `replace(str, find, replace_with)` - Replace all occurrences

**Usage:**
```quartz
import system.string as strfunc;
let text = "Hello World";
let len = strfunc.length(text);                    // len = 11
let upper = strfunc.uppercase(text);               // "HELLO WORLD"
let has_world = strfunc.contains(text, "World");   // true
let replaced = strfunc.replace(text, "World", "Quartz");  // "Hello Quartz"
```

---

### 4. system.convert
Type conversion between primitive types.

**Functions:**
- `toInt(value)` or `toInteger(value)` - Convert to integer
- `toDouble(value)` or `toFloat(value)` - Convert to double
- `toString(value)` - Convert to string
- `toBool(value)` or `toBoolean(value)` - Convert to boolean

**Conversion Rules:**
- String to Int: Uses `stoi()`, returns 0 on parse failure
- String to Double: Uses `stod()`, returns 0.0 on parse failure
- String to Bool: "true", "1", "yes" are true; others are false
- Number to Bool: 0/0.0 is false; non-zero is true
- Bool to Number: true becomes 1/1.0; false becomes 0/0.0

**Usage:**
```quartz
import system.convert as convert;
let i = convert.toInt("42");         // i = 42
let d = convert.toDouble("3.14");    // d = 3.14
let s = convert.toString(123);       // s = "123"
let b = convert.toBool("true");      // b = true
```

---

## Import Syntax

All modules use the standard import syntax:

```quartz
import system.MODULE as ALIAS;
```

**Note:** Keyword-named modules (like `system.string`) require aliasing due to syntax constraints:
```quartz
import system.string as strfunc;  // Required for string module
```

---

## Common Patterns

### String Processing
```quartz
import system.string as strfunc;
import system.io as io;

let text = "hello world";
let processed = strfunc.uppercase(text);
io.out.println("Result:", processed);
```

### Mathematical Computation
```quartz
import system.math as math;
import system.io as io;

let radius = 5.0;
let area = math.pow(radius, 2) * 3.14159;
io.out.println("Area:", area);
```

### Type Conversion Chain
```quartz
import system.convert as convert;
import system.io as io;

let user_input = "100";
let number = convert.toInt(user_input);
let doubled = number * 2;
let result = convert.toString(doubled);
io.out.println("Result:", result);
```

---

## Extension Architecture

All standard library modules are built as shared library extensions:
- **Location:** `extensions/system_*/`
- **Auto-loaded:** Yes (via `Runtime::loadExtensions()`)
- **Registration:** Functions registered in `init_extension()` function
- **Thread-safe:** Functions use closures capturing necessary context

---

## Future Extensions

Potential modules for future development:
- `system.file` - File I/O operations
- `system.time` - Date/time functions
- `system.json` - JSON parsing and serialization
- `system.network` - Network I/O operations
- `system.crypto` - Cryptographic functions
- `system.collections` - Advanced data structures

---

## Notes

1. All math functions work with both `int` and `double` types
2. String functions handle empty strings gracefully
3. Conversion functions never throw exceptions; they return defaults on failure
4. Module functions are case-sensitive
5. Import aliases must be valid identifiers (not keywords)
