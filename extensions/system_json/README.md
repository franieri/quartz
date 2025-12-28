# System.JSON Extension

A JSON parsing and manipulation extension for Quartz using the [nlohmann/json](https://github.com/nlohmann/json) library.

## Installation

The extension is built automatically with the Quartz build system. The nlohmann/json header is downloaded automatically during the build process.

## Functions

### `system.json.parse(str) -> value`
Parse a JSON string and return the corresponding Quartz value (dict, array, int, double, string, or bool).

```quartz
let jsonStr = system.json.stringify({"name": "John", "age": 30});
let obj = system.json.parse(jsonStr);
```

### `system.json.stringify(value, indent?) -> string`
Convert a Quartz value to a JSON string. Optional `indent` parameter for pretty-printing (number of spaces).

```quartz
let obj = {"name": "Alice", "age": 25};
let compact = system.json.stringify(obj);        // {"age":25,"name":"Alice"}
let pretty = system.json.stringify(obj, 2);      // Indented with 2 spaces
```

### `system.json.isValid(str) -> bool`
Check if a string contains valid JSON.

```quartz
let valid = system.json.isValid("{\"test\": true}");  // true
let invalid = system.json.isValid("not json");        // false
```

### `system.json.prettify(str, indent?) -> string`
Pretty-print a JSON string. Default indent is 2 spaces.

```quartz
let compact = "{\"a\":1,\"b\":2}";
let pretty = system.json.prettify(compact);
```

### `system.json.minify(str) -> string`
Remove all whitespace from a JSON string.

```quartz
let spaced = "{ \"a\": 1, \"b\": 2 }";
let compact = system.json.minify(spaced);  // {"a":1,"b":2}
```

### `system.json.type(str) -> string`
Get the JSON type of a value. Returns: "null", "boolean", "number", "string", "array", "object", or "invalid".

```quartz
println(system.json.type("123"));       // "number"
println(system.json.type("[1,2,3]"));   // "array"
println(system.json.type("{}"));        // "object"
```

### `system.json.merge(obj1, obj2) -> obj`
Merge two JSON objects. Values from `obj2` overwrite those in `obj1` for duplicate keys.

```quartz
let a = {"x": 1, "y": 2};
let b = {"y": 3, "z": 4};
let merged = system.json.merge(a, b);  // {"x":1,"y":3,"z":4}
```

### `system.json.get(obj, path) -> value`
Get a value using a JSON Pointer path (e.g., "/foo/bar").

```quartz
let data = {"user": {"name": "Bob", "age": 30}};
let name = system.json.get(data, "/user/name");  // "Bob"
```

### `system.json.set(obj, path, value) -> obj`
Set a value using a JSON Pointer path. Returns a new modified object.

```quartz
let data = {"user": {"age": 30}};
let updated = system.json.set(data, "/user/age", 31);
```

## Usage Example

```quartz
import system.io.*;
import system.json.*;

// Create and stringify an object
let user = {"name": "Alice", "age": 25, "active": true};
let jsonStr = system.json.stringify(user, 2);
println(jsonStr);

// Parse JSON
let parsed = system.json.parse(jsonStr);

// Merge objects
let defaults = {"theme": "dark", "notifications": true};
let userPrefs = {"theme": "light"};
let settings = system.json.merge(defaults, userPrefs);

// Use JSON Pointers
let config = {"database": {"host": "localhost", "port": 5432}};
let host = system.json.get(config, "/database/host");
let updated = system.json.set(config, "/database/port", 5433);
```

## Implementation Details

- Uses nlohmann/json v3.11.3 (single-header library)
- Supports conversion between Quartz types (int, double, string, bool, dict, array) and JSON
- Handles nested structures recursively
- Error handling returns default values (0, empty string, etc.) on failure

## Building

The extension is built automatically when running:
```bash
./rebuild_extensions.sh
```

Or to build just this extension:
```bash
./rebuild_extensions.sh system_json
```

## Testing

Run the test suite:
```bash
./build/quartz samples/stdlib/test_json_focused.qz
```
