// ============================================================================
// Parser - Core Implementation
// Handles core parser utilities: token management, error reporting, parse entry
// ============================================================================

#include "parser.h"
#include "logger.h"

#include <iostream>

// ============================================================================
// Constructor and Main Parse Entry
// ============================================================================

Parser::Parser(const std::vector<Token>& tokens, const std::string& source)
    : tokens(tokens), current(0), source(source), state(ParserState::IDLE) {}

AST Parser::parse() {
    state = ParserState::PARSING;
    AST ast;
    while (!isAtEnd()) {
        try {
            ast.nodes.push_back(statement());
        } catch (const std::runtime_error& e) {
            state = ParserState::ERROR;
            // Skip to next statement on error
            while (!isAtEnd() && !match(TokenType::SEMICOLON)) advance();
        }
    }
    if (state != ParserState::ERROR) state = ParserState::COMPLETED;
    return ast;
}

// ============================================================================
// Token Utilities
// ============================================================================

Token Parser::peek() const {
    if (current >= tokens.size()) return Token(TokenType::EOF_TOKEN, "", 0, 0);
    return tokens[current];
}

Token Parser::previous() const {
    if (current == 0) return Token(TokenType::EOF_TOKEN, "", 0, 0);
    return tokens[current - 1];
}

Token Parser::peekNext() const {
    if (current + 1 >= tokens.size()) return Token(TokenType::EOF_TOKEN, "", 0, 0);
    return tokens[current + 1];
}

bool Parser::isAtEnd() const {
    return current >= tokens.size() || tokens[current].type == TokenType::EOF_TOKEN;
}

Token Parser::advance() {
    if (!isAtEnd()) current++;
    return previous();
}

bool Parser::match(TokenType type) {
    if (isAtEnd() || peek().type != type) return false;
    advance();
    return true;
}

bool Parser::check(TokenType type) const {
    if (isAtEnd()) return false;
    return peek().type == type;
}

Token Parser::consume(TokenType type, const std::string& message) {
    if (match(type)) return previous();
    try {
        if (!source.empty()) {
            Logger::instance().log(LogLevel::ERROR, peek(), message);
            // replicate the caret display used by error()
            size_t lineNo = peek().line;
            size_t startIdx = 0, endIdx = source.size();
            size_t ln = 1;
            for (size_t i = 0; i < source.size(); ++i) { if (ln == lineNo) { startIdx = i; break; } if (source[i] == '\n') ++ln; }
            for (size_t i = startIdx; i < source.size(); ++i) { if (source[i] == '\n') { endIdx = i; break; } }
            std::string line = source.substr(startIdx, endIdx - startIdx);
            std::cerr << line << std::endl;
            int col = peek().column > 0 ? peek().column : 1;
            for (int i = 1; i < col; ++i) std::cerr << (line.size()>=i && line[i-1]=='\t' ? '\t' : ' ');
            std::cerr << '^' << std::endl;
        } else {
            Logger::instance().log(LogLevel::ERROR, peek(), message);
        }
    } catch (const std::runtime_error&) {
        // rethrow to propagate
        throw;
    }
    return Token(TokenType::UNKNOWN, "", peek().line, peek().column);
}

// ============================================================================
// Error Reporting
// ============================================================================

void Parser::error(const Token& token, const std::string& message) const {
    // Non-fatal parse diagnostics use NOTICE by default but show source line + caret
    if (!source.empty()) {
        // print source line
        size_t lineNo = token.line;
        size_t cur = 1;
        size_t startIdx = 0;
        size_t endIdx = source.size();
        size_t ln = 1;
        for (size_t i = 0; i < source.size(); ++i) {
            if (ln == lineNo) { startIdx = i; break; }
            if (source[i] == '\n') ++ln;
        }
        for (size_t i = startIdx; i < source.size(); ++i) {
            if (source[i] == '\n') { endIdx = i; break; }
        }
        std::string line = source.substr(startIdx, endIdx - startIdx);
        Logger::instance().log(LogLevel::NOTICE, token, message);
        std::cerr << line << std::endl;
        // caret under column (columns are 1-based)
        int col = token.column > 0 ? token.column : 1;
        for (int i = 1; i < col; ++i) std::cerr << (line.size()>=i && line[i-1]=='\t' ? '\t' : ' ');
        std::cerr << '^' << std::endl;
    } else {
        Logger::instance().log(LogLevel::NOTICE, token, message);
    }
}
