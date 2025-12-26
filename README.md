# Quartz Language

A small but brave programming language with a familiar curly-brace syntax, a tree-walking interpreter, a bytecode compiler/VM, and a dynamic extension system.

## Quick Start

```bash
# Build
bash rebuild_core.sh

# Run a sample
./build/quartz samples/sample_import.qz

# Run in bytecode mode
./build/quartz --compile-run samples/sample_import.qz

# Run with input
echo "your input" | ./build/quartz samples/sample_import.qz
```

## Features

- ✅ **Interpreter + Bytecode VM**: Run programs directly or via `--compile-run`
- ✅ **Modules & Imports**: Namespace-based imports with aliases
- ✅ **First-class Values**: `int`, `double`, `string`, `bool`, arrays, dicts
- ✅ **Lambdas**: Pass functions around (works in interpreter and bytecode)
- ✅ **Dynamic Extensions**: Add native functions via shared libraries (loaded at runtime)
- ✅ **Standard Library**: `system.*` extensions (I/O, strings, math, collections, ...)

## Project Structure

```
.
├── src/                   # Core implementation (parser, runtime, bytecode)
├── include/               # Public headers
├── extensions/            # Native extensions (shared libraries)
├── samples/               # Example programs
├── docs/                  # Design + reference docs
├── tools/                 # Dev utilities
└── build/                 # Build output (generated)
```

## Building

### From Scratch
```bash
bash rebuild_core.sh
```

### Incremental Build
```bash
cd build && make
```

## Running

### Sample Programs
```bash
# Basic import and namespace usage
./build/quartz samples/sample_import.qz

# With input
echo "test" | ./build/quartz samples/sample_import.qz
```

### Command Line
```bash
./build/quartz <source-file>
```

### Bytecode Mode
```bash
./build/quartz --compile-run <source-file>
```

## Language Syntax

### Variables
```text
int x = 42;
double pi = 3.14;
string name = "Quartz";
bool flag = true;
```

### Arrays, Dicts, Lambdas
```text
import system.collection as c;

let nums = [1, 2, 3];
let doubled = c.map(nums, (x) => x * 2);

let d = {"a": 1, "b": 2};
system.io.println(doubled);
system.io.println(d["a"]);
```

### Control Flow
```text
if (condition) {
    // ...
} else {
    // ...
}

while (condition) {
    // ...
}

for (init; condition; update) {
    // ...
}
```

### Functions
```text
system.io.println("Hello, World!");
string input = system.io.stdin.readln();
```

### Imports
```text
import system.io as io;

io.println("Using alias");
io.stdin.readln();
```

## Architecture

At a high level:
- **Lexer** tokenizes source text into tokens
- **Parser** produces an AST via recursive descent
- **Runtime** executes the AST (interpreter) and loads extensions
- **Bytecode** compiler + VM supports `--compile-run`

## Extension System

Extensions are dynamic libraries that extend the language functionality.

### Extension Structure
```
extensions/system_io/
├── CMakeLists.txt
├── system_io.cpp         # Extension initialization
├── stdin.cpp             # Input functions
├── stdout.cpp            # Output functions
└── metadata.json         # Extension metadata
```

### Creating an Extension

1. Create a directory under `extensions/`
2. Implement `init_extension(FunctionRegistry& reg)` function
3. Register functions with `reg.registerFunction()`
4. Add CMakeLists.txt for building
5. Extension loads automatically at runtime

## Documentation

### Core Documentation
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) - System design and components
- [docs/EXTENSION_GUIDE.md](docs/EXTENSION_GUIDE.md) - Developing extensions with examples
- [docs/SYNTAX.md](docs/SYNTAX.md) - Language syntax reference
- [docs/STDLIB.md](docs/STDLIB.md) - Standard library overview
- [docs/EXCEPTIONS.md](docs/EXCEPTIONS.md) - Exception semantics
- [docs/README_OOP.md](docs/README_OOP.md) - Object model overview

### Build Scripts
- [rebuild_core.sh](rebuild_core.sh) - Full build with `--help` option
- [rebuild_extensions.sh](rebuild_extensions.sh) - Extension management with `--help` option

## Development

License information has not been added yet.

## Contributing

Issues and pull requests are welcome. If you're adding a native extension, see [docs/EXTENSION_GUIDE.md](docs/EXTENSION_GUIDE.md).
### macOS-Specific Details

- Symbol naming: `_init_extension` with extern "C"
- Visibility attributes: `-fvisibility=default` for symbol export
- RTLD flags: `RTLD_LAZY | RTLD_GLOBAL` for proper symbol resolution
- Load commands: RPATH set for runtime library discovery

## Usage
To run the Quartz interpreter, execute the compiled binary with a source file as an argument:
```
./build/quartz <source-file.qz>
```

## Language Features
- Familiar Java-like syntax
- Support for basic data types (int, float, string, etc.)
- Variable management and execution through the runtime environment

## Contributing
Contributions are welcome! Please submit a pull request or open an issue for any enhancements or bug fixes.

## License
This project is licensed under the Apache-2.0 license.