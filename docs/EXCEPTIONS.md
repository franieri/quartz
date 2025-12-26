# Exception Handling in Quartz Language

## Overview

Quartz provides a robust exception handling system with Java-like syntax. Exceptions allow you to handle errors gracefully without crashing your program.

## Basic Syntax

### Try-Catch

```java
try {
    // Code that might throw an exception
    throw new Exception("Something went wrong!");
} catch (Exception e) {
    // Handle the exception
    io.out.println("Caught: \{e.message}");
}
```

### Try-Catch-Finally

```java
try {
    // Risky operation
    throw new RuntimeError("Operation failed");
} catch (RuntimeError e) {
    io.out.println("Error: \{e.message}");
} finally {
    // Always executed, even if exception occurred
    io.out.println("Cleanup complete");
}
```

## Throwing Exceptions

### Throw a New Exception
```java
throw new Exception("Error message");
throw new ValueError("Invalid input");
throw new RuntimeError("Operation failed");
```

### Throw a String (Creates Exception automatically)
```java
throw "Simple error message";
```

### Rethrow an Exception
```java
try {
    // ...
} catch (Exception e) {
    // Log and rethrow
    io.out.println("Logging error: \{e.message}");
    throw e;
}
```

## Built-in Exception Types

The following exception types are available globally without importing:

| Exception Type | Use Case |
|---------------|----------|
| `Exception` | Base exception type, catches all exceptions |
| `RuntimeError` | General runtime errors |
| `ValueError` | Invalid value or argument |
| `TypeError` | Type mismatch errors |
| `IndexError` | Index out of bounds |
| `NullError` | Null/undefined reference |

## Exception Object Properties

When you catch an exception, the exception object has the following properties:

- `e.message` - The error message string
- `e.type` - The exception type name

```java
try {
    throw new ValueError("Invalid input");
} catch (ValueError e) {
    io.out.println("Type: \{e.type}");      // "ValueError"
    io.out.println("Message: \{e.message}"); // "Invalid input"
}
```

## Catch Patterns

### Catch Specific Type
```java
try {
    throw new IndexError("Out of bounds");
} catch (IndexError e) {
    // Only catches IndexError
}
```

### Catch All Exceptions
```java
try {
    // ...
} catch (Exception e) {
    // Catches any exception type
}
```

### Shorthand (Catches All)
```java
try {
    // ...
} catch (e) {
    // Same as catch (Exception e)
}
```

## Nested Exception Handling

```java
try {
    try {
        throw new IndexError("Inner error");
    } catch (IndexError e) {
        // Handle and rethrow as different type
        throw new Exception("Wrapped: \{e.message}");
    }
} catch (Exception e) {
    io.out.println("Outer catch: \{e.message}");
}
```

## system.error Extension

Import the system.error extension for additional error utilities:

```java
import system.error as error;

// Immediately abort with error
error.panic("Fatal error occurred");

// Assert a condition
error.assert(x > 0, "x must be positive");

// Get stack trace (informational)
error.stackTrace();
```

### Functions

| Function | Description |
|----------|-------------|
| `error.panic(message)` | Immediately terminate with error message |
| `error.assert(condition, message)` | Assert condition, panic if false |
| `error.stackTrace()` | Print stack trace (informational) |
| `error.getMessage(e)` | Get exception message |
| `error.getType(e)` | Get exception type name |

## Best Practices

1. **Be specific with exception types**: Catch specific exceptions when you can handle them differently.

2. **Use finally for cleanup**: Always release resources in a finally block.

3. **Don't catch and ignore**: Always handle or rethrow exceptions.

4. **Provide meaningful messages**: Exception messages should be helpful for debugging.

```java
// Good
throw new ValueError("Expected positive number, got: \{value}");

// Bad
throw new Exception("Error");
```

## Example: File Processing with Exceptions

```java
import system.io as io;

fn processFile(filename: string) {
    try {
        // Simulate file operation
        if (filename == "") {
            throw new ValueError("Filename cannot be empty");
        }
        io.out.println("Processing: \{filename}");
    } catch (ValueError e) {
        io.out.println("Invalid input: \{e.message}");
    } finally {
        io.out.println("Operation complete");
    }
}

processFile("data.txt");
processFile("");  // Will catch ValueError
```
