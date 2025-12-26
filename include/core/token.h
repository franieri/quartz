#ifndef TOKEN_H
#define TOKEN_H

#include <string>

// Enum for token types
enum class TokenType {
    // Keywords
    CLASS, PUBLIC, PRIVATE, PROTECTED, STATIC, FINAL, VOID, INT, DOUBLE, STRING, BOOL,
    IF, ELSE, WHILE, FOR, RETURN, NEW, THIS, SUPER, IMPORT, AS, PACKAGE,
    FN,              // Function definition
    LET, VAR,        // Let (immutable) and Var (mutable)
    DO,              // Do-while
    OPTION, RESULT,  // Option<T> and Result<T,E>
    BREAK, CONTINUE, // Loop control
    IN,              // For-in iteration
    ARROW,           // Function return type arrow (->)
    FAT_ARROW,       // Lambda arrow (=>)
    EXTENDS, IMPLEMENTS, INTERFACE, ABSTRACT, CONSTRUCTOR,  // OOP keywords
    OVERRIDE, VIRTUAL,  // Method modifiers
    TRY, CATCH, FINALLY, THROW,  // Exception handling
    // Literals
    IDENTIFIER, INTEGER_LITERAL, DOUBLE_LITERAL, STRING_LITERAL, BOOLEAN_LITERAL,
    // Operators
    PLUS, MINUS, MULTIPLY, DIVIDE, MODULO, ASSIGN, EQUAL, NOT_EQUAL, LESS, GREATER, LESS_EQUAL, GREATER_EQUAL,
    AND, OR, NOT, INCREMENT, DECREMENT,
    PLUS_ASSIGN, MINUS_ASSIGN, MULTIPLY_ASSIGN, DIVIDE_ASSIGN,  // Compound assignments
    QUESTION,        // For optional types (?)
    // Punctuation
    SEMICOLON, COMMA, DOT, LEFT_PAREN, RIGHT_PAREN, LEFT_BRACE, RIGHT_BRACE, LEFT_BRACKET, RIGHT_BRACKET,
    COLON,           // For type annotations
    DOUBLE_COLON,    // For static access (::)
    PIPE,            // For union types (|)
    // Special
    EOF_TOKEN, UNKNOWN
};

// Token structure
struct Token {
    TokenType type;
    std::string lexeme;
    int line;
    int column;

    Token(TokenType type, const std::string& lexeme, int line, int column)
        : type(type), lexeme(lexeme), line(line), column(column) {}
};

#endif // TOKEN_H