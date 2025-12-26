#ifndef PARSER_H
#define PARSER_H

#include <vector>
#include <optional>
#include <map>
#include "types.h"
#include "token.h"

enum class ParserState { IDLE, PARSING, ERROR, COMPLETED };

class Parser {
public:
    Parser(const std::vector<Token>& tokens, const std::string& source="");
    AST parse();
    const std::string& getSource() const { return source; }
    ParserState getState() const { return state; }
    
    // Public for sub-expression parsing (used in string interpolation)
    ASTNodePtr expression();

private:
    std::vector<Token> tokens;
    size_t current;
    std::string source;
    std::vector<std::string> imports;
    std::map<std::string, std::string> aliases;
    ParserState state;
    std::map<std::string, TypeAnnotation> definedVariables;  // Variable type tracking

    Token peek() const;
    Token previous() const;
    Token peekNext() const;  // Look ahead 2 tokens
    bool isAtEnd() const;
    Token advance();
    bool match(TokenType type);
    bool check(TokenType type) const;  // Check without consuming
    Token consume(TokenType type, const std::string& message);
    void error(const Token& token, const std::string& message) const;

    // Statement parsing
    ASTNodePtr statement();
    ASTNodePtr functionDef();
    ASTNodePtr classDeclaration();
    ASTNodePtr interfaceDeclaration();  // Interface definitions
    ASTNodePtr loopStatement();
    ASTNodePtr forStatement();
    ASTNodePtr whileStatement();
    ASTNodePtr doWhileStatement();
    ASTNodePtr varDeclaration();
    ASTNodePtr importStatement();
    ASTNodePtr tryStatement();
    ASTNodePtr throwStatement();
    
    // Class member parsing
    ASTNodePtr parseConstructor(const std::string& visibility);  // Constructor with params
    ASTNodePtr parseMethod(const std::string& visibility, bool isStatic, bool isVirtual, bool isOverride);
    ASTNodePtr parseField(const std::string& visibility, bool isStatic);
    std::vector<std::string> parseGenericParams();  // Parse <T, U, ...>
    
    // Type parsing
    TypeAnnotation parseTypeAnnotation();
    TypeAnnotation parseFunctionType();  // (int, string) -> bool
    
    // Literal parsing
    ASTNodePtr parseArrayLiteral();
    ASTNodePtr parseDictLiteral();
    
    // Expression parsing (precedence climbing)
    ASTNodePtr assignment();
    ASTNodePtr logicalOr();
    ASTNodePtr logicalAnd();
    ASTNodePtr equality();
    ASTNodePtr comparison();
    ASTNodePtr term();
    ASTNodePtr factor();
    ASTNodePtr unary();
    ASTNodePtr postfix();
    ASTNodePtr primary();
    
    // Lambda/Closure parsing
    ASTNodePtr parseLambda();  // (x, y) => x + y or (x, y) => { ... }
    bool isLambdaStart();  // Check if current position starts a lambda
};

// Quartz naming convention (Qz prefix)
using QzParser = Parser;

#endif // PARSER_H