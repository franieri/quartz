#include "lexer.h"
#include <cctype>
#include <iostream>

Lexer::Lexer(const std::string& source, const SyntaxConfig& config)
    : source(source), current(0), line(1), column(1), config(config) {}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    tokens.reserve(source.size() / 4);  // Estimate: ~4 chars per token on average
    while (!isAtEnd()) {
        skipWhitespace();
        if (isAtEnd()) break;
        char c = advance();
        int tokenColumn = column - 1; // column was incremented by advance(), so token starts at column-1
        switch (c) {
            case '+':
                if (peek() == '+') {
                    advance();
                    tokens.emplace_back(TokenType::INCREMENT, "++", line, tokenColumn);
                } else if (peek() == '=') {
                    advance();
                    tokens.emplace_back(TokenType::PLUS_ASSIGN, "+=", line, tokenColumn);
                } else {
                    tokens.emplace_back(TokenType::PLUS, "+", line, tokenColumn);
                }
                break;
            case '-':
                if (peek() == '-') {
                    advance();
                    tokens.emplace_back(TokenType::DECREMENT, "--", line, tokenColumn);
                } else if (peek() == '>') {
                    advance();
                    tokens.emplace_back(TokenType::ARROW, "->", line, tokenColumn);
                } else if (peek() == '=') {
                    advance();
                    tokens.emplace_back(TokenType::MINUS_ASSIGN, "-=", line, tokenColumn);
                } else {
                    tokens.emplace_back(TokenType::MINUS, "-", line, tokenColumn);
                }
                break;
            case '*':
                if (peek() == '=') {
                    advance();
                    tokens.emplace_back(TokenType::MULTIPLY_ASSIGN, "*=", line, tokenColumn);
                } else {
                    tokens.emplace_back(TokenType::MULTIPLY, "*", line, tokenColumn);
                }
                break;
            case '/':
                if (peek() == '/') {
                    // Single-line comment: skip until end of line
                    while (!isAtEnd() && peek() != '\n') {
                        advance();
                    }
                } else if (peek() == '*') {
                    // Multi-line comment: skip until */
                    advance(); // consume *
                    while (!isAtEnd()) {
                        if (peek() == '*' && peekNext() == '/') {
                            advance(); // consume *
                            advance(); // consume /
                            break;
                        }
                        if (peek() == '\n') {
                            line++;
                            column = 1;
                        }
                        advance();
                    }
                } else if (peek() == '=') {
                    advance();
                    tokens.emplace_back(TokenType::DIVIDE_ASSIGN, "/=", line, tokenColumn);
                } else {
                    tokens.emplace_back(TokenType::DIVIDE, "/", line, tokenColumn);
                }
                break;
            case '%': tokens.emplace_back(TokenType::MODULO, "%", line, tokenColumn); break;
            case '=':
                if (peek() == '=') {
                    advance();
                    tokens.emplace_back(TokenType::EQUAL, "==", line, tokenColumn);
                } else if (peek() == '>') {
                    advance();
                    tokens.emplace_back(TokenType::FAT_ARROW, "=>", line, tokenColumn);
                } else {
                    tokens.emplace_back(TokenType::ASSIGN, "=", line, tokenColumn);
                }
                break;
            case '!':
                if (peek() == '=') {
                    advance();
                    tokens.emplace_back(TokenType::NOT_EQUAL, "!=", line, tokenColumn);
                } else {
                    tokens.emplace_back(TokenType::NOT, "!", line, tokenColumn);
                }
                break;
            case '<':
                if (peek() == '=') {
                    advance();
                    tokens.emplace_back(TokenType::LESS_EQUAL, "<=", line, tokenColumn);
                } else {
                    tokens.emplace_back(TokenType::LESS, "<", line, tokenColumn);
                }
                break;
            case '>':
                if (peek() == '=') {
                    advance();
                    tokens.emplace_back(TokenType::GREATER_EQUAL, ">=", line, tokenColumn);
                } else {
                    tokens.emplace_back(TokenType::GREATER, ">", line, tokenColumn);
                }
                break;
            case '&':
                if (peek() == '&') {
                    advance();
                    tokens.emplace_back(TokenType::AND, "&&", line, tokenColumn);
                }
                break;
            case ';': tokens.emplace_back(TokenType::SEMICOLON, ";", line, tokenColumn); break;
            case ',': tokens.emplace_back(TokenType::COMMA, ",", line, tokenColumn); break;
            case '.': tokens.emplace_back(TokenType::DOT, ".", line, tokenColumn); break;
            case '(': tokens.emplace_back(TokenType::LEFT_PAREN, "(", line, tokenColumn); break;
            case ')': tokens.emplace_back(TokenType::RIGHT_PAREN, ")", line, tokenColumn); break;
            case '{': tokens.emplace_back(TokenType::LEFT_BRACE, "{", line, tokenColumn); break;
            case '}': tokens.emplace_back(TokenType::RIGHT_BRACE, "}", line, tokenColumn); break;
            case '[': tokens.emplace_back(TokenType::LEFT_BRACKET, "[", line, tokenColumn); break;
            case ']': tokens.emplace_back(TokenType::RIGHT_BRACKET, "]", line, tokenColumn); break;
            case '?': tokens.emplace_back(TokenType::QUESTION, "?", line, tokenColumn); break;
            case ':':
                if (peek() == ':') {
                    advance();
                    tokens.emplace_back(TokenType::DOUBLE_COLON, "::", line, tokenColumn);
                } else {
                    tokens.emplace_back(TokenType::COLON, ":", line, tokenColumn);
                }
                break;
            case '|':
                if (peek() == '|') {
                    advance();
                    tokens.emplace_back(TokenType::OR, "||", line, tokenColumn);
                } else {
                    tokens.emplace_back(TokenType::PIPE, "|", line, tokenColumn);
                }
                break;
            case '"': tokens.push_back(string()); break;
            default:
                if (std::isdigit(c)) {
                    tokens.push_back(number());
                } else if (std::isalpha(c) || c == '_') {
                    tokens.push_back(identifier());
                } else {
                    tokens.emplace_back(TokenType::UNKNOWN, std::string(1, c), line, tokenColumn);
                }
                break;
        }
    }
    tokens.emplace_back(TokenType::EOF_TOKEN, "", line, column);
    return tokens;
}

char Lexer::advance() {
    char c = source[current++];
    column++;
    return c;
}

char Lexer::peek() const {
    if (isAtEnd()) return '\0';
    return source[current];
}

char Lexer::peekNext() const {
    if (current + 1 >= source.size()) return '\0';
    return source[current + 1];
}

bool Lexer::isAtEnd() const {
    return current >= source.size();
}

void Lexer::skipWhitespace() {
    while (!isAtEnd()) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '\n':
                advance();
                line++;
                column = 1;
                break;
            default:
                return;
        }
    }
}

Token Lexer::identifier() {
    size_t start = current - 1;
    while (!isAtEnd() && (std::isalnum(peek()) || peek() == '_')) {
        advance();
    }
    int startCol = column - static_cast<int>(current - start);
    std::string text = source.substr(start, current - start);
    TokenType type = checkKeyword(text);
    return Token(type, text, line, startCol);
}

Token Lexer::number() {
    size_t start = current - 1;
    bool isDouble = false;
    while (!isAtEnd() && std::isdigit(peek())) {
        advance();
    }
    if (!isAtEnd() && peek() == '.') {
        isDouble = true;
        advance();
        while (!isAtEnd() && std::isdigit(peek())) {
            advance();
        }
    }
    int startCol = column - static_cast<int>(current - start);
    std::string text = source.substr(start, current - start);
    return Token(isDouble ? TokenType::DOUBLE_LITERAL : TokenType::INTEGER_LITERAL, text, line, startCol);
}

Token Lexer::string() {
    size_t start = current;
    while (!isAtEnd() && peek() != '"') {
        if (peek() == '\n') line++;
        advance();
    }
    if (isAtEnd()) {
        // Error: unterminated string
        int startCol = column - static_cast<int>(current - start) - 1;
        return Token(TokenType::UNKNOWN, source.substr(start - 1, current - start + 1), line, startCol);
    }
    advance(); // consume closing "
    int startCol = column - static_cast<int>(current - start) - 1;
    std::string value = source.substr(start, current - start - 1);
    return Token(TokenType::STRING_LITERAL, value, line, startCol);
}

TokenType Lexer::checkKeyword(const std::string& word) const {
    auto it = config.keywords.find(word);
    if (it != config.keywords.end()) {
        return it->second;
    }
    return TokenType::IDENTIFIER;
}