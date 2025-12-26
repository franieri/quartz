# Quartz OOP Syntax - Language Comparison Guide

## Class Definition Comparison

### Simple Class

**Quartz** (Elegant & Concise)
```java
class Person {
    string name;
    int age;
    
    fn greet() {
        io.println("Hello");
    }
}
```

**Java** (Verbose)
```java
public class Person {
    private String name;
    private int age;
    
    public void greet() {
        System.out.println("Hello");
    }
}
```

**C++** (Complex)
```cpp
class Person {
private:
    std::string name;
    int age;
public:
    void greet() {
        std::cout << "Hello" << std::endl;
    }
};
```

**Python** (Simple)
```python
class Person:
    def __init__(self):
        self.name = ""
        self.age = 0
    
    def greet(self):
        print("Hello")
```

---

## Object Instantiation

### Creating Objects

**Quartz** ⭐ Intuitive
```java
let person = new Person();
```

**Java** - Same pattern
```java
Person person = new Person();
```

**C++** - Multiple options
```cpp
Person person;              // Stack
Person* person = new Person(); // Heap
auto person = std::make_unique<Person>();
```

**Python** - Function call
```python
person = Person()
```

---

## Method Definition

### With Parameters and Return Type

**Quartz** ⭐ Clear & Modern
```java
fn calculateArea(radius: double) -> double {
    return 3.14159 * radius * radius;
}
```

**Java** - Type-heavy
```java
public double calculateArea(double radius) {
    return 3.14159 * radius * radius;
}
```

**C++** - C-style
```cpp
double calculateArea(double radius) {
    return 3.14159 * radius * radius;
}
```

**Python** - Minimal syntax
```python
def calculate_area(radius):
    return 3.14159 * radius * radius
```

---

## Inheritance

### Parent and Child Classes

**Quartz** ⭐ Familiar & Clean
```java
class Animal {
    string name;
    
    fn speak() {
        io.println("Some sound");
    }
}

class Dog extends Animal {
    string breed;
    
    fn speak() {
        io.println("Woof!");
    }
}
```

**Java** - Similar pattern
```java
public class Animal {
    protected String name;
    
    public void speak() {
        System.out.println("Some sound");
    }
}

public class Dog extends Animal {
    private String breed;
    
    @Override
    public void speak() {
        System.out.println("Woof!");
    }
}
```

**C++** - Verbose syntax
```cpp
class Animal {
protected:
    std::string name;
public:
    virtual void speak() {
        std::cout << "Some sound" << std::endl;
    }
};

class Dog : public Animal {
private:
    std::string breed;
public:
    void speak() override {
        std::cout << "Woof!" << std::endl;
    }
};
```

**Python** - Clean
```python
class Animal:
    def __init__(self):
        self.name = ""
    
    def speak(self):
        print("Some sound")

class Dog(Animal):
    def __init__(self):
        super().__init__()
        self.breed = ""
    
    def speak(self):
        print("Woof!")
```

---

## Access Modifiers

### Public, Private, Protected

**Quartz** ⭐ Optional annotations
```java
class Account {
    public string accountNumber;
    private double balance;
    protected string holder;
    
    fn getBalance() -> double {
        return balance;
    }
}
```

**Java** - Explicit modifiers
```java
public class Account {
    public String accountNumber;
    private double balance;
    protected String holder;
    
    public double getBalance() {
        return balance;
    }
}
```

**C++** - Section-based
```cpp
class Account {
public:
    std::string accountNumber;
    double getBalance();
protected:
    std::string holder;
private:
    double balance;
};
```

**Python** - Convention-based
```python
class Account:
    def __init__(self):
        self.account_number = ""     # public
        self._holder = ""            # protected
        self.__balance = 0.0         # private
    
    def get_balance(self):
        return self.__balance
```

---

## Static Members

### Class-level Variables and Methods

**Quartz** ⭐ Simple & Clear
```java
class Math {
    static double PI = 3.14159;
    
    static fn square(x: int) -> int {
        return x * x;
    }
}

let area = Math.PI * radius * radius;
let result = Math.square(5);
```

**Java** - Explicit static keyword
```java
public class Math {
    public static final double PI = 3.14159;
    
    public static int square(int x) {
        return x * x;
    }
}

double area = Math.PI * radius * radius;
int result = Math.square(5);
```

**C++** - Verbose declaration
```cpp
class Math {
public:
    static constexpr double PI = 3.14159;
    static int square(int x) { return x * x; }
};

double area = Math::PI * radius * radius;
int result = Math::square(5);
```

**Python** - Class attributes
```python
class Math:
    PI = 3.14159
    
    @staticmethod
    def square(x):
        return x * x

area = Math.PI * radius * radius
result = Math.square(5)
```

---

## Constructors

### Initialization Logic

**Quartz** (To Be Implemented)
```java
class Circle {
    double radius;
    
    constructor(r: double) {
        radius = r;
    }
}

let circle = new Circle(5.0);
```

**Java** - Method-style constructors
```java
public class Circle {
    private double radius;
    
    public Circle(double r) {
        radius = r;
    }
}

Circle circle = new Circle(5.0);
```

**C++** - Similar to Java
```cpp
class Circle {
private:
    double radius;
public:
    Circle(double r) : radius(r) {}
};

Circle circle(5.0);
```

**Python** - Special __init__ method
```python
class Circle:
    def __init__(self, r):
        self.radius = r

circle = Circle(5.0)
```

---

## Complete Example: Bank Account

### Quartz ⭐ Most Pleasant
```java
class BankAccount {
    private string accountNumber;
    private double balance;
    
    constructor(number: string, initialBalance: double) {
        accountNumber = number;
        balance = initialBalance;
    }
    
    fn deposit(amount: double) {
        balance += amount;
    }
    
    fn withdraw(amount: double) -> bool {
        if (amount <= balance) {
            balance -= amount;
            return true;
        }
        return false;
    }
    
    fn getBalance() -> double {
        return balance;
    }
}

let account = new BankAccount("12345", 1000.0);
account.deposit(500.0);
let success = account.withdraw(200.0);
io.println("Balance:", account.getBalance());
```

### Java
```java
public class BankAccount {
    private String accountNumber;
    private double balance;
    
    public BankAccount(String number, double initialBalance) {
        this.accountNumber = number;
        this.balance = initialBalance;
    }
    
    public void deposit(double amount) {
        balance += amount;
    }
    
    public boolean withdraw(double amount) {
        if (amount <= balance) {
            balance -= amount;
            return true;
        }
        return false;
    }
    
    public double getBalance() {
        return balance;
    }
}

BankAccount account = new BankAccount("12345", 1000.0);
account.deposit(500.0);
boolean success = account.withdraw(200.0);
System.out.println("Balance: " + account.getBalance());
```

### Python
```python
class BankAccount:
    def __init__(self, number, initial_balance):
        self.__account_number = number
        self.__balance = initial_balance
    
    def deposit(self, amount):
        self.__balance += amount
    
    def withdraw(self, amount):
        if amount <= self.__balance:
            self.__balance -= amount
            return True
        return False
    
    def get_balance(self):
        return self.__balance

account = BankAccount("12345", 1000.0)
account.deposit(500.0)
success = account.withdraw(200.0)
print("Balance:", account.get_balance())
```

---

## Feature Comparison Matrix

| Feature | Quartz | Java | C++ | Python |
|---------|----------|------|-----|--------|
| **Syntax Simplicity** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐ | ⭐⭐⭐⭐⭐ |
| **Type Safety** | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐ |
| **Performance** | ⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐ |
| **Learning Curve** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐ | ⭐⭐⭐⭐ |
| **Readability** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| **Conciseness** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐ | ⭐⭐⭐⭐⭐ |
| **Flexibility** | ⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| **Standard Library** | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |

---

## Why Quartz OOP is Pleasant

### 1. **Best of All Worlds**
- Java's familiar patterns
- C++'s performance potential
- Python's readability
- Modern language conveniences

### 2. **Minimal Boilerplate**
- No `public class` everywhere
- No `@Override` annotations needed
- No `this.` prefix required
- Optional type annotations

### 3. **Clear & Consistent**
- `fn` for all functions
- `:` for all type annotations
- `->` for return types
- `new` for instantiation
- `extends` for inheritance

### 4. **Developer Friendly**
- Error messages with line/column
- Predictable behavior
- Logical organization
- Familiar to most programmers

### 5. **Modern Features**
- Optional type annotations
- Compound assignments
- Loop control (break/continue)
- Arrays and dictionaries
- Import system

---

## Philosophy: "Elegance Without Sacrifice"

Quartz OOP demonstrates that you can have:
- ✓ Simplicity (like Python)
- ✓ Type safety (like Java)
- ✓ Performance potential (like C++)
- ✓ Expressiveness (like all modern languages)

All in one coherent, pleasant syntax.
