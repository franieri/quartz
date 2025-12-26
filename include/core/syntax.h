#ifndef SYNTAX_H
#define SYNTAX_H

#include <unordered_map>
#include <string>
#include "token.h"

// Syntax configuration for easy parametrization
class SyntaxConfig {
public:
    std::unordered_map<std::string, TokenType> keywords;
    std::unordered_map<std::string, TokenType> operators;
    std::unordered_map<std::string, TokenType> punctuation;

    SyntaxConfig() {
        // Initialize keywords
        keywords = {
            {"class", TokenType::CLASS},
            {"public", TokenType::PUBLIC},
            {"private", TokenType::PRIVATE},
            {"protected", TokenType::PROTECTED},
            {"static", TokenType::STATIC},
            {"final", TokenType::FINAL},
            {"void", TokenType::VOID},
            {"if", TokenType::IF},
            {"else", TokenType::ELSE},
            {"while", TokenType::WHILE},
            {"for", TokenType::FOR},
            {"do", TokenType::DO},
            {"return", TokenType::RETURN},
            {"new", TokenType::NEW},
            {"this", TokenType::THIS},
            {"super", TokenType::SUPER},
            {"import", TokenType::IMPORT},
            {"as", TokenType::AS},
            {"package", TokenType::PACKAGE},
            {"fn", TokenType::FN},
            {"let", TokenType::LET},
            {"var", TokenType::VAR},
            {"in", TokenType::IN},
            {"break", TokenType::BREAK},
            {"continue", TokenType::CONTINUE},
            {"Option", TokenType::OPTION},
            {"Result", TokenType::RESULT},
            {"extends", TokenType::EXTENDS},
            {"implements", TokenType::IMPLEMENTS},
            {"interface", TokenType::INTERFACE},
            {"abstract", TokenType::ABSTRACT},
            {"constructor", TokenType::CONSTRUCTOR},
            {"override", TokenType::OVERRIDE},
            {"virtual", TokenType::VIRTUAL},
            
            // Exception handling
            {"try", TokenType::TRY},
            {"catch", TokenType::CATCH},
            {"finally", TokenType::FINALLY},
            {"throw", TokenType::THROW},

            // Primitives
            {"int", TokenType::INT},
            {"double", TokenType::DOUBLE},
            {"string", TokenType::STRING},
            {"bool", TokenType::BOOL},
        };

        // Initialize operators
        operators = {
            {"+=", TokenType::PLUS_ASSIGN},      // Compound assignments (must come before single +)
            {"-=", TokenType::MINUS_ASSIGN},
            {"*=", TokenType::MULTIPLY_ASSIGN},
            {"/=", TokenType::DIVIDE_ASSIGN},
            {"++", TokenType::INCREMENT},
            {"--", TokenType::DECREMENT},
            {"==", TokenType::EQUAL},
            {"!=", TokenType::NOT_EQUAL},
            {"<=", TokenType::LESS_EQUAL},
            {">=", TokenType::GREATER_EQUAL},
            {"&&", TokenType::AND},
            {"||", TokenType::OR},
            {"->", TokenType::ARROW},
            {"=>", TokenType::FAT_ARROW},  // Lambda arrow
            {"::", TokenType::DOUBLE_COLON},  // Static access
            {"+", TokenType::PLUS},
            {"-", TokenType::MINUS},
            {"*", TokenType::MULTIPLY},
            {"/", TokenType::DIVIDE},
            {"%", TokenType::MODULO},
            {"=", TokenType::ASSIGN},
            {"<", TokenType::LESS},
            {">", TokenType::GREATER},
            {"!", TokenType::NOT},
            {"?", TokenType::QUESTION},
            {"|", TokenType::PIPE}  // Union types
        };

        // Initialize punctuation
        punctuation = {
            {";", TokenType::SEMICOLON},
            {",", TokenType::COMMA},
            {".", TokenType::DOT},
            {"(", TokenType::LEFT_PAREN},
            {")", TokenType::RIGHT_PAREN},
            {"{", TokenType::LEFT_BRACE},
            {"}", TokenType::RIGHT_BRACE},
            {"[", TokenType::LEFT_BRACKET},
            {"]", TokenType::RIGHT_BRACKET},
            {":", TokenType::COLON},
            {"::", TokenType::DOUBLE_COLON}
        };
    }

    // Methods to add or modify syntax elements
    void addKeyword(const std::string& word, TokenType type) {
        keywords[word] = type;
    }

    void addOperator(const std::string& op, TokenType type) {
        operators[op] = type;
    }

    void addPunctuation(const std::string& punct, TokenType type) {
        punctuation[punct] = type;
    }
};

#endif // SYNTAX_H