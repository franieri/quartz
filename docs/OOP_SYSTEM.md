# Quartz OOP System - Design & Implementation

## Overview

Quartz now features a comprehensive Object-Oriented Programming (OOP) system inspired by Java and C++, with modern, elegant syntax that prioritizes developer experience.

## Core OOP Features Implemented

### 1. Class Declarations

```java
class ClassName {
    fieldType fieldName;
    
    fn methodName() -> returnType {
        // method body
    }
}
```

**Features:**
- Simple, clean syntax similar to Java/C++
- Support for class fields (instance variables)
- Support for methods (instance functions)
- Built-in type support: `int`, `double`, `string`, `bool`
- Custom type support for user-defined classes
- Access modifiers: `public`, `private`, `protected` (parsed, runtime enforcement pending)
- Static methods: `static fn methodName()`
- Inheritance: `class Child extends Parent {}`

### 2. Object Instantiation

```java
let obj = new ClassName();
```

**Features:**
- `new` keyword for object creation
- Constructor argument support (parsed, execution pending)
- Automatic instance creation and registration
- Object identity tracking via internal IDs

### 3. Instance Fields

```java
class Person {
    string name;
    int age;
    bool active;
}
```

**Features:**
- Multiple field declarations
- Type annotations required for clarity
- Optional initialization: `string name = "default";`
- Field visibility modifiers supported

### 4. Instance Methods

```java
class Calculator {
    fn add(a: int, b: int) -> int {
        return a + b;
    }
    
    fn describe() {
        io.println("I am a calculator");
    }
}
```

**Features:**
- Named parameters with type annotations
- Return type specification via `->` operator
- Method visibility modifiers
- `this` binding support (framework prepared)
- Implicit return support (last expression as return value)

### 5. Return Statements

```java
fn getValue() -> int {
    return 42;
}

fn noReturn() {
    io.println("Done");
    return;
}
```

**Features:**
- Explicit return with optional value
- Optional return for void methods
- Line and column error reporting

## Language Syntax Examples

### Basic Class Definition

```java
class Person {
    string name;
    int age;
    
    fn greet() {
        io.out.println("Hello, I am:", name);
    }
    
    fn haveBirthday() {
        age += 1;
    }
}

let person = new Person();
```

### Inheritance Structure

```java
class Animal {
    string species;
    
    fn speak() {
        io.out.println("Animal sound");
    }
}

class Dog extends Animal {
    string breed;
    
    fn speak() {
        io.out.println("Woof!");
    }
}
```

### Multiple Classes

```java
class Circle {
    double radius;
    
    fn getArea() -> double {
        return 3.14159 * radius * radius;
    }
}

class Rectangle {
    double width;
    double height;
    
    fn getArea() -> double {
        return width * height;
    }
}

let circle = new Circle();
let rect = new Rectangle();
```

## Implementation Architecture

### Parser (`src/core/parser.cpp`)

**New Methods:**
- `classDeclaration()` - Parses complete class definitions
  - Handles visibility modifiers (public, private, protected)
  - Parses fields with type annotations
  - Parses methods with parameters and return types
  - Supports inheritance via `extends` keyword
  - Recognizes built-in types and custom identifiers

**Enhanced Methods:**
- `statement()` - Added CLASS token recognition
- `primary()` - Added NEW keyword for instantiation
- Field type parsing accepts keywords: INT, DOUBLE, STRING, BOOL
- Method parameter types accept keywords
- Return type annotations

### Runtime (`src/core/runtime.cpp`)

**New Class System:**
- `ObjectInstance` class - Runtime representation of objects
  - Field storage via unordered_map
  - Get/set field operations
  - Class name tracking

**Class Registry:**
- `classRegistry` map stores all defined classes
- `ClassDef` structure contains:
  - Class name and parent class
  - Field definitions with types
  - Method definitions
  - Abstract method flags

**Object Management:**
- `objects` map tracks instantiated objects
- `createObject()` - Factory method for new instances
- `getObject()` - Lookup existing instances
- `isObjectVariable()` - Type checking for objects

**Execution Support:**
- `executeNode()` handles ClassDef nodes
- `evaluate()` handles New nodes
- Class definition registration
- Object instantiation with ID tracking

### Type System (`include/core/types.h`)

**New Structures:**

```cpp
struct MethodDef {
    std::string name;
    std::vector<std::pair<std::string, TypeAnnotation>> parameters;
    TypeAnnotation returnType;
    ASTNodePtr body;
    bool isStatic;
    bool isAbstract;
    std::string visibility;
};

struct ClassDef {
    std::string name;
    std::string parentClass;
    std::vector<std::pair<std::string, TypeAnnotation>> fields;
    std::vector<MethodDef> methods;
    MethodDef* constructor;
    std::vector<std::string> interfaces;
    bool isAbstract;
};
```

**AST Updates:**
- Added ClassDef, Method, Field, New, Return node types
- Enhanced AST struct with classes vector

### Token System (`include/core/token.h`)

**New Keywords:**
- `class` - Class declarations
- `extends` - Inheritance
- `implements` - Interface implementation (parsed, pending)
- `interface` - Interface definitions (parsed, pending)
- `abstract` - Abstract classes/methods (parsed, pending)
- `constructor` - Constructor keyword (parsed, pending)
- `return` - Return statement

**Existing Keywords (Enhanced):**
- `public`, `private`, `protected` - Access modifiers
- `static` - Static members
- `final` - Final members (parsed, enforcement pending)
- `new` - Object instantiation

## Pleasure & Elegance Considerations

### What Makes Quartz Pleasant

1. **Clean Syntax**
   - No semicolons required after class blocks
   - Simple `new ClassName()` for instantiation
   - Consistent type annotation with `:`
   - Clear method syntax with `fn name() {}`

2. **Modern Features**
   - Type inference where possible
   - Short method syntax: `fn getX() -> int { return x; }`
   - Optional return for last expression
   - Compound assignments: `+=`, `-=`, `*=`, `/=`

3. **Developer Experience**
   - Clear error messages with line/column info
   - Consistent keyword naming (class, extends, interface)
   - Java-like but more concise
   - C++-inspired template readiness

4. **Familiar to Developers**
   - Java developers: Class syntax, new keyword, extends
   - C++ developers: Method syntax, access modifiers
   - Python developers: Optional types, clean structure
   - Rust developers: Pattern matching framework ready

### Comparison with Similar Languages

| Feature | Quartz | Java | C++ | Python |
|---------|----------|------|-----|--------|
| Class syntax | `class X { }` | `class X { }` | `class X { };` | `class X:` |
| Object creation | `new X()` | `new X()` | `X()` or `new X()` | `X()` |
| Methods | `fn m() { }` | `void m() { }` | `void m() { }` | `def m():` |
| Access modifiers | `public/private/protected` | `public/private/protected` | `public:/private:/protected:` | No native |
| Inheritance | `extends` | `extends` | `:` | `:` |
| Constructors | `constructor` | `ClassName()` | `ClassName()` | `__init__()` |
| Field types | Optional: `int x;` | Required: `int x;` | Required: `int x;` | Not needed |

## Future Enhancements

### Planned Features (Phase 2-3)

1. **Method Invocation**
   - `obj.methodName(args)`
   - `this` binding in methods
   - Method overriding
   - Virtual methods

2. **Constructors**
   - `constructor()` method
   - Parameter forwarding
   - Constructor chaining

3. **Interfaces**
   - `interface` declarations
   - `implements` keyword
   - Multiple interface inheritance

4. **Advanced OOP**
   - Abstract methods
   - Abstract classes
   - Static initializers
   - Final members enforcement
   - Protected access enforcement

5. **Modern Features**
   - Properties with getters/setters: `person.age` calls `getAge()`
   - Operator overloading
   - Extension methods on built-in types
   - Inner classes
   - Anonymous classes/lambdas

6. **Memory & Performance**
   - Smart pointers optimization
   - Destructor support
   - Copy/move semantics
   - Resource management (RAII-style)

## Testing & Validation

### Test Files Created
- `samples/language/oop/oop_basics_classes_objects.qz` - Basic class definitions and instantiation
- `samples/language/oop/oop_showcase.qz` - Comprehensive OOP showcase with multiple classes

### Features Tested ✓
- Class declaration with fields
- Class declaration with methods
- Object instantiation with `new`
- Multiple classes in same program
- Return statements in methods
- Access modifiers parsing
- Inheritance syntax parsing
- Array/dict functionality with OOP
- Compound assignments
- Loop constructs

### Known Limitations (Phase 1)
- Method invocation not yet executed (`obj.method()` parsed but not called)
- Constructors parsed but not executed
- Access modifiers not enforced at runtime
- Static methods not yet executed
- Inheritance framework ready but not yet functional
- `this` binding framework prepared but not active
- No garbage collection (relies on C++ smart pointers)

## Code Quality

### Build Status
- ✓ Compiles successfully (C++17)
- ✓ All header files consistent
- ✓ No compilation warnings
- ✓ CMake build system working
- ✓ Extensions load correctly

### Error Handling
- Parse errors with line/column info
- Clear error messages for missing syntax
- Debug logging for class registration
- Validation of type annotations

## Summary

Quartz now has a solid foundation for object-oriented programming with:
- **Pleasant syntax** inspired by Java and C++
- **Clear type system** with optional annotations
- **Clean instantiation** via `new` keyword
- **Method support** with parameters and return types
- **Inheritance framework** ready for implementation
- **Excellent foundation** for future enhancements

The implementation prioritizes developer experience with readable, expressive syntax while maintaining the power and flexibility needed for larger applications.
