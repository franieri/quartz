// ============================================================================
// Parser - Statements Implementation
// Handles statement parsing: control flow, declarations, imports
// ============================================================================

#include "parser.h"
#include "logger.h"

// ============================================================================
// Main Statement Parser
// ============================================================================

ASTNodePtr Parser::statement() {
    if (match(TokenType::BREAK)) {
        consume(TokenType::SEMICOLON, "Expect ';' after 'break'");
        return std::make_shared<ASTNode>(NodeType::Break);
    }
    if (match(TokenType::CONTINUE)) {
        consume(TokenType::SEMICOLON, "Expect ';' after 'continue'");
        return std::make_shared<ASTNode>(NodeType::Continue);
    }
    if (match(TokenType::RETURN)) {
        ASTNodePtr returnNode = std::make_shared<ASTNode>(NodeType::Return);
        if (!check(TokenType::SEMICOLON)) {
            // Parse return value if present
            returnNode->children.push_back(expression());
        }
        consume(TokenType::SEMICOLON, "Expect ';' after return statement");
        return returnNode;
    }
    // Interface declaration
    if (match(TokenType::INTERFACE)) {
        current--;  // back up to re-consume
        return interfaceDeclaration();
    }
    // Abstract class or regular class
    if (match(TokenType::ABSTRACT)) {
        // Abstract class
        return classDeclaration();
    }
    if (match(TokenType::CLASS)) {
        current--; // back up to re-consume
        return classDeclaration();
    }
    if (match(TokenType::FN)) {
        current--; // back up to re-consume
        return functionDef();
    }
    if (match(TokenType::LET) || match(TokenType::VAR)) {
        return varDeclaration();
    }
    if (match(TokenType::FOR)) {
        current--; // back up to re-consume
        return forStatement();
    }
    if (match(TokenType::WHILE)) {
        current--; // back up to re-consume
        return whileStatement();
    }
    if (match(TokenType::DO)) {
        current--; // back up to re-consume
        return doWhileStatement();
    }
    if (match(TokenType::IF)) {
        consume(TokenType::LEFT_PAREN, "Expect '(' after 'if'.");
        ASTNodePtr condition = expression();
        consume(TokenType::RIGHT_PAREN, "Expect ')' after if condition.");
        ASTNodePtr thenBranch = statement();
        std::optional<ASTNodePtr> elseBranch;
        if (match(TokenType::ELSE)) {
            elseBranch = statement();
        }
        ASTNodePtr ifNode = std::make_shared<ASTNode>(NodeType::If);
        ifNode->children = {condition, thenBranch};
        if (elseBranch) ifNode->children.push_back(*elseBranch);
        return ifNode;
    }
    // Exception handling: try-catch-finally
    if (match(TokenType::TRY)) {
        return tryStatement();
    }
    // Exception handling: throw
    if (match(TokenType::THROW)) {
        return throwStatement();
    }
    if (match(TokenType::IMPORT)) {
        return importStatement();
    }
    if (match(TokenType::LEFT_BRACE)) {
        ASTNodePtr block = std::make_shared<ASTNode>(NodeType::Block);
        while (!isAtEnd() && !check(TokenType::RIGHT_BRACE)) {
            block->children.push_back(statement());
        }
        consume(TokenType::RIGHT_BRACE, "Expect '}' after block");
        return block;
    }
    if (match(TokenType::INT) || match(TokenType::DOUBLE) || match(TokenType::STRING) || match(TokenType::BOOL)) {
        std::string type = previous().lexeme;
        Token id = consume(TokenType::IDENTIFIER, "Expect identifier after type.");
        consume(TokenType::ASSIGN, "Expect '=' after identifier.");
        ASTNodePtr value = expression();
        consume(TokenType::SEMICOLON, "Expect ';' after declaration.");
        ASTNodePtr decl = std::make_shared<ASTNode>(NodeType::Declare);
        decl->value = type;
        decl->children = {std::make_shared<ASTNode>(NodeType::Identifier, id.lexeme), value};
        return decl;
    }
    ASTNodePtr expr = expression();
    consume(TokenType::SEMICOLON, "Expect ';' after expression.");
    // If the expression is a bare string literal (hanging string), warn and drop it
    if (expr->type == NodeType::Literal) {
        // check if it's a string
        bool isString = false;
        try {
            std::visit([&](auto&& arg){ using T = std::decay_t<decltype(arg)>; if constexpr(std::is_same_v<T,std::string>) isString = true; }, expr->value);
        } catch(...) {}
        if (isString) {
            Logger::instance().log(LogLevel::WARNING, "Hanging string literal ignored.");
            return std::make_shared<ASTNode>(NodeType::NoOp);
        }
    }
    return expr;
}

// ============================================================================
// Import Statement
// ============================================================================

ASTNodePtr Parser::importStatement() {
    // New import syntax:
    // import system.io.*;                    - Import all from namespace
    // import system.io.* as io;              - Import all with alias
    // import system.io.println;              - Import specific function
    // import system.io.{println, readln};    - Import multiple specific items
    // import mylib.*;                        - Load all .qz files from mylib/
    
    Token id = consume(TokenType::IDENTIFIER, "Expect identifier after 'import'.");
    std::string basePath = id.lexeme;
    
    // Parse the module path: system.io, mylib.utils, etc.
    while (match(TokenType::DOT)) {
        // Check for wildcard: .*
        if (match(TokenType::MULTIPLY)) {
            // This is a wildcard import
            break;
        }
        
        // Check for specific imports with braces: .{item1, item2}
        if (match(TokenType::LEFT_BRACE)) {
            // Parse multiple specific items
            std::vector<std::string> specificItems;
            do {
                Token item = consume(TokenType::IDENTIFIER, "Expect identifier in import list.");
                specificItems.push_back(item.lexeme);
            } while (match(TokenType::COMMA));
            consume(TokenType::RIGHT_BRACE, "Expect '}' after import list.");
            
            // Check for alias
            std::string alias;
            if (match(TokenType::AS)) {
                Token aliasToken = consume(TokenType::IDENTIFIER, "Expect identifier after 'as'.");
                alias = aliasToken.lexeme;
            }
            consume(TokenType::SEMICOLON, "Expect ';' after import.");
            
            // Create import node with specific items
            // Format: "basePath:{item1,item2}" or "alias:basePath:{item1,item2}"
            std::string itemList;
            for (size_t i = 0; i < specificItems.size(); ++i) {
                if (i > 0) itemList += ",";
                itemList += specificItems[i];
            }
            std::string importValue = basePath + ":{" + itemList + "}";
            if (!alias.empty()) {
                importValue = alias + ":" + importValue;
            }
            ASTNodePtr imp = std::make_shared<ASTNode>(NodeType::Import, importValue);
            return imp;
        }
        
        // Regular path component
        std::string part_name;
        if (peek().type == TokenType::IDENTIFIER) {
            part_name = advance().lexeme;
        } else if (peek().type == TokenType::STRING) {
            advance();
            part_name = "string";
        } else if (peek().type == TokenType::INT) {
            advance();
            part_name = "int";
        } else if (peek().type == TokenType::DOUBLE) {
            advance();
            part_name = "double";
        } else if (peek().type == TokenType::BOOL) {
            advance();
            part_name = "bool";
        } else {
            error(peek(), "Expect identifier after '.'");
            throw std::runtime_error("Expect identifier after '.'");
        }
        basePath += "." + part_name;
    }
    
    // Check if last token was wildcard (already consumed)
    bool isWildcard = (previous().type == TokenType::MULTIPLY);
    
    // Check for alias
    std::string alias;
    if (match(TokenType::AS)) {
        Token aliasToken = consume(TokenType::IDENTIFIER, "Expect identifier after 'as'.");
        alias = aliasToken.lexeme;
    }
    consume(TokenType::SEMICOLON, "Expect ';' after import.");
    
    // Build the import value string
    // Format: "path.*" for wildcard, "path.specific" for specific, "alias:path.*" with alias
    std::string importValue;
    if (isWildcard) {
        importValue = basePath + ".*";
    } else {
        // Check if this is a specific item import (no wildcard, has a final component)
        // e.g., import system.io.println -> basePath = "system.io.println"
        importValue = basePath;
    }
    
    if (!alias.empty()) {
        aliases[alias] = basePath;
        importValue = alias + ":" + importValue;
    } else if (!isWildcard) {
        // For non-wildcard, non-aliased imports, add to imports list
        imports.push_back(basePath);
    }
    
    ASTNodePtr imp = std::make_shared<ASTNode>(NodeType::Import, importValue);
    return imp;
}

// ============================================================================
// Loop Statements
// ============================================================================

ASTNodePtr Parser::forStatement() {
    consume(TokenType::FOR, "Expect 'for' keyword");
    consume(TokenType::LEFT_PAREN, "Expect '(' after 'for'");
    
    Token var = consume(TokenType::IDENTIFIER, "Expect variable name");
    consume(TokenType::IN, "Expect 'in' in for loop");
    ASTNodePtr iterable = expression();
    consume(TokenType::RIGHT_PAREN, "Expect ')' after for clause");
    
    ASTNodePtr body = statement();
    
    ASTNodePtr forNode = std::make_shared<ASTNode>(NodeType::For, var.lexeme);
    forNode->children = {iterable, body};
    return forNode;
}

ASTNodePtr Parser::whileStatement() {
    consume(TokenType::WHILE, "Expect 'while' keyword");
    consume(TokenType::LEFT_PAREN, "Expect '(' after 'while'");
    ASTNodePtr condition = expression();
    consume(TokenType::RIGHT_PAREN, "Expect ')' after condition");
    ASTNodePtr body = statement();
    
    ASTNodePtr whileNode = std::make_shared<ASTNode>(NodeType::While);
    whileNode->children = {condition, body};
    return whileNode;
}

ASTNodePtr Parser::doWhileStatement() {
    consume(TokenType::DO, "Expect 'do' keyword");
    ASTNodePtr body = statement();
    consume(TokenType::WHILE, "Expect 'while' after do body");
    consume(TokenType::LEFT_PAREN, "Expect '(' after 'while'");
    ASTNodePtr condition = expression();
    consume(TokenType::RIGHT_PAREN, "Expect ')' after condition");
    consume(TokenType::SEMICOLON, "Expect ';' after do-while");
    
    ASTNodePtr doNode = std::make_shared<ASTNode>(NodeType::DoWhile);
    doNode->children = {body, condition};
    return doNode;
}

ASTNodePtr Parser::loopStatement() {
    // 'loop' is an infinite loop, needs break to exit
    consume(TokenType::DO, "Expect 'loop' or 'do' for infinite loop");
    ASTNodePtr body = statement();
    
    ASTNodePtr loopNode = std::make_shared<ASTNode>(NodeType::Loop);
    loopNode->children = {body};
    return loopNode;
}

// ============================================================================
// Exception Handling
// ============================================================================

ASTNodePtr Parser::tryStatement() {
    // try { ... } catch (ExceptionType e) { ... } finally { ... }
    // children[0] = try body
    // children[1] = catch variable name (Identifier node with exception type in annotationType)
    // children[2] = catch body
    // children[3] = finally body (optional)
    
    ASTNodePtr tryNode = std::make_shared<ASTNode>(NodeType::Try);
    
    // Parse try block
    consume(TokenType::LEFT_BRACE, "Expect '{' after 'try'");
    ASTNodePtr tryBody = std::make_shared<ASTNode>(NodeType::Block);
    while (!isAtEnd() && peek().type != TokenType::RIGHT_BRACE) {
        tryBody->children.push_back(statement());
    }
    consume(TokenType::RIGHT_BRACE, "Expect '}' after try block");
    tryNode->children.push_back(tryBody);
    
    // Parse catch clause (required)
    consume(TokenType::CATCH, "Expect 'catch' after try block");
    consume(TokenType::LEFT_PAREN, "Expect '(' after 'catch'");
    
    // Parse exception type and variable name: catch (Exception e) or catch (e)
    std::string exceptionType = "Exception";  // Default type
    std::string varName;
    
    Token first = consume(TokenType::IDENTIFIER, "Expect exception type or variable name");
    if (peek().type == TokenType::IDENTIFIER) {
        // Two identifiers: type and name
        exceptionType = first.lexeme;
        Token second = consume(TokenType::IDENTIFIER, "Expect variable name after exception type");
        varName = second.lexeme;
    } else {
        // Single identifier: just the variable name
        varName = first.lexeme;
    }
    
    consume(TokenType::RIGHT_PAREN, "Expect ')' after catch parameters");
    
    // Create catch variable node with type annotation
    ASTNodePtr catchVar = std::make_shared<ASTNode>(NodeType::Identifier, varName);
    catchVar->annotationType = TypeAnnotation(exceptionType);
    tryNode->children.push_back(catchVar);
    
    // Parse catch block
    consume(TokenType::LEFT_BRACE, "Expect '{' after catch parameters");
    ASTNodePtr catchBody = std::make_shared<ASTNode>(NodeType::Block);
    while (!isAtEnd() && peek().type != TokenType::RIGHT_BRACE) {
        catchBody->children.push_back(statement());
    }
    consume(TokenType::RIGHT_BRACE, "Expect '}' after catch block");
    tryNode->children.push_back(catchBody);
    
    // Parse optional finally clause
    if (match(TokenType::FINALLY)) {
        consume(TokenType::LEFT_BRACE, "Expect '{' after 'finally'");
        ASTNodePtr finallyBody = std::make_shared<ASTNode>(NodeType::Block);
        while (!isAtEnd() && peek().type != TokenType::RIGHT_BRACE) {
            finallyBody->children.push_back(statement());
        }
        consume(TokenType::RIGHT_BRACE, "Expect '}' after finally block");
        tryNode->children.push_back(finallyBody);
    }
    
    return tryNode;
}

ASTNodePtr Parser::throwStatement() {
    // throw new Exception("message");
    // throw exceptionVariable;
    // throw "simple message";  (creates Exception with message)
    
    ASTNodePtr throwNode = std::make_shared<ASTNode>(NodeType::Throw);
    throwNode->line = previous().line;
    throwNode->column = previous().column;
    
    // Parse the exception expression
    ASTNodePtr expr = expression();
    throwNode->children.push_back(expr);
    
    consume(TokenType::SEMICOLON, "Expect ';' after throw statement");
    return throwNode;
}
