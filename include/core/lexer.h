#ifndef LEXER_H
#define LEXER_H

#include <vector>
#include <string>
#include "token.h"
#include "syntax.h"

class Lexer {
public:
    Lexer(const std::string& source, const SyntaxConfig& config);
    std::vector<Token> tokenize();

private:
    std::string source;
    size_t current;
    int line;
    int column;
    SyntaxConfig config;

    char advance();
    char peek() const;
    char peekNext() const;
    bool isAtEnd() const;
    void skipWhitespace();
    Token identifier();
    Token number();
    Token string();
    TokenType checkKeyword(const std::string& word) const;
};

// Quartz naming convention (Qz prefix)
using QzLexer = Lexer;

#endif // LEXER_H