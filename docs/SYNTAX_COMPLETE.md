# Quartz Language Syntax Guide - Complete

## Table of Contents
1. Basics
2. Data Types & Variables
3. Arrays & Dictionaries
4. Functions
5. Classes & OOP
6. Control Flow
7. Comments & Style

---

## 1. Basics

### Hello World
```java
import system.io as io;
io.out.println("Hello, World!");
```

### Variable Declaration
```java
let x = 10;              // Immutable
var y = 20;              // Mutable
let name = "Alice";      // String
let pi = 3.14159;        // Double
let active = true;       // Boolean
```

### Type Annotations
```java
let x: int = 42;
let name: string = "Bob";
let ratio: double = 2.5;
let flag: bool = false;
```

---

## 2. Data Types & Variables

### Primitive Types
```java
int count = 100;
double price = 99.99;
string message = "Hello";
bool isValid = true;
```

### Type Conversion (Implicit)
```java
let i: int = 42;
let d: double = i + 0.5;  // int converts to double in expression
```

### Immutability
```java
let constant = 42;        // Cannot be reassigned
var mutable = 42;         // Can be reassigned
mutable = 100;            // OK
```

---

## 3. Arrays & Dictionaries

### Array Literals
```java
let numbers = [1, 2, 3, 4, 5];
let strings = ["apple", "banana", "cherry"];
let mixed = [1, "two", 3.0, true];
```

### Array Indexing
```java
let arr = [10, 20, 30];
let first = arr[0];       // 10
let second = arr[1];      // 20
arr[1] = 25;              // Modify element
```

### Dictionary Literals
```java
let person = {
    "name": "Alice",
    "age": 30,
    "city": "NYC"
};

let settings = {
    "dark_mode": true,
    "volume": 0.8
};
```

### Dictionary Access
```java
let dict = {"name": "Bob", "age": 25};
let name = dict["name"];       // "Bob"
let age = dict["age"];         // 25
dict["city"] = "London";       // Add new key
```

---

## 4. Functions

### Basic Function
```java
fn greet() {
    io.out.println("Hello!");
}
greet();
```

### Function with Parameters
```java
fn add(a: int, b: int) -> int {
    return a + b;
}
let result = add(5, 3);  // 8
```

### Function with Return Type
```java
fn getName() -> string {
    return "Alice";
}

fn calculateArea(radius: double) -> double {
    return 3.14159 * radius * radius;
}
```

### Optional Return
```java
fn greetPerson(name: string) {
    io.out.println("Hello, ", name);
    return;  // or just end function
}
```

### Multiple Parameters
```java
fn buildMessage(greeting: string, name: string, punctuation: string) -> string {
    return greeting + " " + name + punctuation;
}
let msg = buildMessage("Hello", "Alice", "!");
```

---

## 5. Classes & OOP

### Class Definition
```java
class Person {
    string name;
    int age;
    
    fn greet() {
        io.out.println("Hello, I am", name);
    }
    
    fn haveBirthday() {
        age += 1;
    }
}
```

### Object Creation
```java
let person = new Person();
```

### Class with Constructor
```java
class Circle {
    double radius;
    
    constructor(r: double) {
        radius = r;
    }
    
    fn getArea() -> double {
        return 3.14159 * radius * radius;
    }
}

let circle = new Circle(5.0);
```

### Inheritance
```java
class Animal {
    string name;
    
    fn speak() {
        io.out.println("Some sound");
    }
}

class Dog extends Animal {
    string breed;
    
    fn speak() {
        io.out.println("Woof!");
    }
}

let dog = new Dog();
```

### Access Modifiers
```java
class BankAccount {
    public string accountNumber;
    private double balance;
    protected string accountHolder;
    
    fn getBalance() -> double {
        return balance;
    }
}
```

### Static Members
```java
class Math {
    static double PI = 3.14159;
    
    static fn square(x: int) -> int {
        return x * x;
    }
}

let area = Math.PI * radius * radius;
let squared = Math.square(5);
```

---

## 6. Control Flow

### If-Else
```java
let age = 25;
if (age >= 18) {
    io.out.println("Adult");
} else {
    io.out.println("Minor");
}
```

### While Loop
```java
let i = 0;
while (i < 5) {
    io.out.println(i);
    i += 1;
}
```

### Do-While Loop
```java
let n = 0;
do {
    io.out.println(n);
    n += 1;
} while (n < 3);
```

### For-In Loop
```java
let numbers = [1, 2, 3, 4, 5];
for (n in numbers) {
    io.out.println(n);
}
```

### Break & Continue
```java
for (i in [1, 2, 3, 4, 5]) {
    if (i == 3) {
        break;  // Exit loop
    }
    if (i == 2) {
        continue;  // Skip to next iteration
    }
    io.out.println(i);
}
```

---

## 7. Operators & Expressions

### Arithmetic
```java
let a = 10;
let b = 3;
let sum = a + b;          // 13
let diff = a - b;         // 7
let product = a * b;      // 30
let quotient = a / b;     // 3
let remainder = a % b;    // 1
```

### Comparison
```java
5 == 5    // true
5 != 3    // true
5 < 10    // true
5 > 3     // true
5 <= 5    // true
5 >= 5    // true
```

### Logical
```java
true && true    // true (AND)
true || false   // true (OR)
!true           // false (NOT)
```

### Compound Assignment
```java
let x = 10;
x += 5;   // x = 15
x -= 3;   // x = 12
x *= 2;   // x = 24
x /= 4;   // x = 6
```

### Increment/Decrement
```java
let i = 5;
i++;      // i = 6
i--;      // i = 5
```

---

## 8. String Operations

### String Literals
```java
let str1 = "Hello";
let str2 = "World";
```

### String Concatenation
```java
let greeting = "Hello" + " " + "World";   // "Hello World"
let message = "Name: " + "Alice";          // "Name: Alice"
```

### String in Collections
```java
let words = ["apple", "banana", "cherry"];
let person = {"name": "Bob", "age": 25};
```

---

## 9. Complete Program Example

```java
import system.io as io;

class Student {
    string name;
    int id;
    double gpa;
    
    fn displayInfo() {
        io.out.println("Name:", name);
        io.out.println("ID:", id);
        io.out.println("GPA:", gpa);
    }
    
    fn isHonor() -> bool {
        return gpa >= 3.5;
    }
}

class School {
    string name;
    int studentCount;
    
    fn getStudentCount() -> int {
        return studentCount;
    }
}

let student = new Student();
let school = new School();

let scores = [95, 87, 92, 88, 90];
let data = {
    "subject": "Math",
    "difficulty": "Hard"
};

let sum = 0;
for (score in scores) {
    sum += score;
}

let average = sum / scores[4];

if (average >= 90) {
    io.out.println("Excellent!");
} else {
    io.out.println("Good work!");
}

io.out.println("Average score:", average);
```

---

## 10. Best Practices & Style

### Naming Conventions
```java
let myVariable = 42;          // camelCase for variables
let MY_CONSTANT = 3.14159;    // UPPER_CASE for constants
fn calculateTotal() { }       // camelCase for functions
class PersonData { }          // PascalCase for classes
```

### Organization
```java
import system.io as io;

class MyClass {
    int field1;
    string field2;
    
    fn method1() { }
    fn method2() { }
}

let instance = new MyClass();
```

### Readability
```java
// Good: Clear variable names
let userAge = 25;
let isActive = true;

// Better: Type annotations
let userAge: int = 25;
let isActive: bool = true;

// Best: Consistency
let userName: string = "Alice";
let userEmail: string = "alice@example.com";
```

---

## Syntax Summary

| Construct | Syntax | Example |
|-----------|--------|---------|
| Variable | `let x = value` | `let x = 42` |
| Type annot | `: type` | `let x: int = 42` |
| Array | `[val1, val2, ...]` | `[1, 2, 3]` |
| Dict | `{"key": val, ...}` | `{"name": "Bob"}` |
| Function | `fn name() { }` | `fn add() { }` |
| Param | `name: type` | `fn add(a: int)` |
| Return | `-> type` | `fn add() -> int` |
| Class | `class Name { }` | `class Person { }` |
| Object | `new ClassName()` | `new Person()` |
| Field | `type name;` | `string name;` |
| Method | `fn name() { }` | `fn greet() { }` |
| If | `if (cond) { }` | `if (x > 0) { }` |
| While | `while (cond) { }` | `while (i < 5) { }` |
| For-in | `for (v in arr)` | `for (n in nums)` |
| Break | `break;` | Inside loops |
| Continue | `continue;` | Inside loops |
| Return | `return val;` | `return 42;` |

---

This guide covers the complete Quartz syntax as of the latest implementation. The language prioritizes clarity and developer experience while maintaining the power of a modern programming language.
