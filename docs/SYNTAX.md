# Quartz Language Syntax Guide

## Data Types

### Primitives

| Type | Size | Range | Example |
|------|------|-------|---------|
| `int` | 32-bit | -2,147,483,648 to 2,147,483,647 | `int x = 42;` |
| `double` | 64-bit | ±1.7976931348623157e+308 | `double pi = 3.14159;` |
| `String` | Variable | N/A | `String name = "Alice";` |
| `bool` | 1 byte | true/false | `bool flag = true;` |

## Variables

```java
int count = 10;
double price = 19.99;
String message = "Hello";
bool active = true;
```

## Operators

### Arithmetic
```java
int a = 5 + 3;    // Addition: 8
int b = 5 - 3;    // Subtraction: 2
int c = 5 * 3;    // Multiplication: 15
int d = 6 / 2;    // Division: 3
```

### Comparison
```java
5 == 5     // Equal to: true
5 != 3     // Not equal: true
5 > 3      // Greater than: true
5 >= 5     // Greater or equal: true
3 < 5      // Less than: true
3 <= 5     // Less or equal: true
```

### Logical
```java
true && false      // Logical AND: false
true || false      // Logical OR: true
!true              // Logical NOT: false
```

## Control Flow

### If Statement

```java
if (condition) {
    // executed if condition is true
} else if (another_condition) {
    // executed if another_condition is true
} else {
    // executed if none of above are true
}
```

Example:
```java
int age = 20;
if (age >= 18) {
    system.io.println("You are an adult");
} else {
    system.io.println("You are a minor");
}
```

### While Loop

```java
while (condition) {
    // loop body executed while condition is true
}
```

Example:
```java
int i = 0;
while (i < 10) {
    system.io.println("i is", i);
    i = i + 1;
}
```

### For Loop

```java
for (init; condition; update) {
    // loop body
}
```

Example:
```java
for (int i = 0; i < 5; i = i + 1) {
    system.io.println("Iteration", i);
}
```

## Functions

### Calling Functions

Functions are called using dot notation with parentheses:

```java
system.io.println("Hello, World!");
String line = system.io.stdin.readln();
```

### Function Arguments

Pass multiple arguments as comma-separated values:

```java
system.io.println("Value is", 42);
system.io.println("Name:", name, "Age:", age);
```

## Imports and Namespaces

### Import Statement

```java
import namespace.path;
```

### Import with Alias

```java
import system.io as io;
io.println("Using alias");
```

### Available Namespaces

#### system.io - Input/Output

- `system.io.println(...)` - Print to stdout with newline
- `system.io.out.println(...)` - Alternative syntax
- `system.io.stdin.readln()` - Read a line from stdin

Example:
```java
import system.io as io;

io.println("What is your name?");
String name = io.stdin.readln();
io.println("Hello,", name);
```

## Comments

Comments are not currently supported in the syntax.

## Complete Example

```java
import system.io as io;

// Calculate factorial (simple version)
int number = 5;
io.println("Calculating factorial of", number);

int result = 1;
int i = 1;
while (i <= number) {
    result = result * i;
    i = i + 1;
}

io.println("Result:", result);

// Read user input
io.println("Enter your name:");
String name = io.stdin.readln();
io.println("Nice to meet you,", name);
```

## Type Conversion

Implicit conversions are not automatically performed. Use explicit operations:

```java
int a = 5;
double b = 5.0;        // Different types, explicit decimal
String s = "42";       // String literal
```

## Best Practices

1. **Use meaningful variable names**: `user_age` instead of `ua`
2. **Import with aliases**: Makes code more readable
3. **Use println for debugging**: Quick way to see values
4. **Proper indentation**: Improves code readability

## Limitations and Constraints

- No string concatenation operator (use multiple print args)
- No array/list types yet
- No custom functions/methods
- No classes or objects
- No exception handling
- Limited operator precedence (use parentheses for clarity)

## Quick Reference

| Concept | Syntax |
|---------|--------|
| Variable | `type name = value;` |
| If | `if (cond) { } else { }` |
| While | `while (cond) { }` |
| For | `for (init; cond; update) { }` |
| Import | `import namespace;` |
| Alias | `import namespace as alias;` |
| Function call | `namespace.function(...);` |

