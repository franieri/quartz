// ============================================================================
// Parser - Expressions Implementation
// Handles expression parsing: precedence climbing, operators, literals
// ============================================================================

#include "parser.h"
#include "logger.h"
#include "lexer.h"
#include "syntax.h"

// ============================================================================
// Expression Parsing (Precedence Climbing)
// ============================================================================

ASTNodePtr Parser::expression() {
    return assignment();
}

ASTNodePtr Parser::assignment() {
    ASTNodePtr expr = logicalOr();
    if (match(TokenType::ASSIGN)) {
        Token equals = previous();
        ASTNodePtr value = assignment();
        if (expr->type == NodeType::Identifier) {
            ASTNodePtr assign = std::make_shared<ASTNode>(NodeType::Assign);
            assign->children = {expr, value};
            return assign;
        } else {
            Logger::instance().log(LogLevel::ERROR, equals, "Invalid assignment target.");
            throw std::runtime_error("Invalid assignment target");
        }
    } else if (match(TokenType::PLUS_ASSIGN) || match(TokenType::MINUS_ASSIGN) || 
               match(TokenType::MULTIPLY_ASSIGN) || match(TokenType::DIVIDE_ASSIGN)) {
        Token op = previous();
        ASTNodePtr value = assignment();
        if (expr->type == NodeType::Identifier) {
            // Convert compound assignment to binary operation + assignment
            // x += 1 becomes x = x + 1
            std::string binOp;
            if (op.type == TokenType::PLUS_ASSIGN) binOp = "+";
            else if (op.type == TokenType::MINUS_ASSIGN) binOp = "-";
            else if (op.type == TokenType::MULTIPLY_ASSIGN) binOp = "*";
            else if (op.type == TokenType::DIVIDE_ASSIGN) binOp = "/";
            
            ASTNodePtr binary = std::make_shared<ASTNode>(NodeType::Binary);
            binary->children = {expr, value};
            binary->value = binOp;
            
            ASTNodePtr assign = std::make_shared<ASTNode>(NodeType::Assign);
            assign->children = {expr, binary};
            return assign;
        } else {
            Logger::instance().log(LogLevel::ERROR, op, "Invalid assignment target.");
            throw std::runtime_error("Invalid assignment target");
        }
    }
    return expr;
}

ASTNodePtr Parser::logicalOr() {
    ASTNodePtr expr = logicalAnd();
    while (match(TokenType::OR)) {
        Token op = previous();
        ASTNodePtr right = logicalAnd();
        ASTNodePtr binary = std::make_shared<ASTNode>(NodeType::Binary);
        binary->children = {expr, right};
        binary->value = op.lexeme;
        expr = binary;
    }
    return expr;
}

ASTNodePtr Parser::logicalAnd() {
    ASTNodePtr expr = equality();
    while (match(TokenType::AND)) {
        Token op = previous();
        ASTNodePtr right = equality();
        ASTNodePtr binary = std::make_shared<ASTNode>(NodeType::Binary);
        binary->children = {expr, right};
        binary->value = op.lexeme;
        expr = binary;
    }
    return expr;
}

ASTNodePtr Parser::equality() {
    ASTNodePtr expr = comparison();
    while (match(TokenType::EQUAL) || match(TokenType::NOT_EQUAL)) {
        Token op = previous();
        ASTNodePtr right = comparison();
        ASTNodePtr binary = std::make_shared<ASTNode>(NodeType::Binary);
        binary->children = {expr, right};
        binary->value = op.lexeme;
        expr = binary;
    }
    return expr;
}

ASTNodePtr Parser::comparison() {
    ASTNodePtr expr = term();
    while (match(TokenType::LESS) || match(TokenType::GREATER) || match(TokenType::LESS_EQUAL) || match(TokenType::GREATER_EQUAL)) {
        Token op = previous();
        ASTNodePtr right = term();
        ASTNodePtr binary = std::make_shared<ASTNode>(NodeType::Binary);
        binary->children = {expr, right};
        binary->value = op.lexeme;
        expr = binary;
    }
    return expr;
}

ASTNodePtr Parser::term() {
    ASTNodePtr expr = factor();
    while (match(TokenType::PLUS) || match(TokenType::MINUS)) {
        Token op = previous();
        ASTNodePtr right = factor();
        ASTNodePtr binary = std::make_shared<ASTNode>(NodeType::Binary);
        binary->children = {expr, right};
        binary->value = op.lexeme;
        expr = binary;
    }
    return expr;
}

ASTNodePtr Parser::factor() {
    ASTNodePtr expr = unary();
    while (match(TokenType::MULTIPLY) || match(TokenType::DIVIDE)) {
        Token op = previous();
        ASTNodePtr right = unary();
        ASTNodePtr binary = std::make_shared<ASTNode>(NodeType::Binary);
        binary->children = {expr, right};
        binary->value = op.lexeme;
        expr = binary;
    }
    return expr;
}

ASTNodePtr Parser::unary() {
    if (match(TokenType::NOT) || match(TokenType::MINUS)) {
        Token op = previous();
        ASTNodePtr right = unary();
        ASTNodePtr unaryNode = std::make_shared<ASTNode>(NodeType::Unary);
        unaryNode->children = {right};
        unaryNode->value = op.lexeme;
        return unaryNode;
    }
    return postfix();
}

ASTNodePtr Parser::postfix() {
    ASTNodePtr expr = primary();
    
    while (match(TokenType::LEFT_BRACKET)) {
        // Array/Dict indexing
        ASTNodePtr index = expression();
        consume(TokenType::RIGHT_BRACKET, "Expect ']' after index expression");
        
        ASTNodePtr indexNode = std::make_shared<ASTNode>(NodeType::Index);
        indexNode->children = {expr, index};
        expr = indexNode;
    }
    
    return expr;
}

// ============================================================================
// Primary Expressions (Literals, Identifiers, Calls)
// ============================================================================

ASTNodePtr Parser::primary() {
    if (match(TokenType::INTEGER_LITERAL)) {
        return std::make_shared<ASTNode>(NodeType::Literal, std::stoi(previous().lexeme));
    }
    if (match(TokenType::DOUBLE_LITERAL)) {
        return std::make_shared<ASTNode>(NodeType::Literal, std::stod(previous().lexeme));
    }
    if (match(TokenType::STRING_LITERAL)) {
        std::string str = previous().lexeme;
        
        // Check for string interpolation: \{expr}
        size_t pos = 0;
        bool hasInterpolation = false;
        while ((pos = str.find("\\{", pos)) != std::string::npos) {
            hasInterpolation = true;
            break;
        }
        
        if (hasInterpolation) {
            // Create an InterpolatedString node
            ASTNodePtr interpNode = std::make_shared<ASTNode>(NodeType::InterpolatedString);
            
            size_t start = 0;
            pos = 0;
            while ((pos = str.find("\\{", start)) != std::string::npos) {
                // Add the string part before the interpolation
                if (pos > start) {
                    std::string part = str.substr(start, pos - start);
                    ASTNodePtr partNode = std::make_shared<ASTNode>(NodeType::StringPart, part);
                    interpNode->children.push_back(partNode);
                }
                
                // Find the closing brace
                size_t end = str.find("}", pos + 2);
                if (end == std::string::npos) {
                    // Unclosed interpolation - treat rest as string
                    std::string rest = str.substr(pos);
                    ASTNodePtr partNode = std::make_shared<ASTNode>(NodeType::StringPart, rest);
                    interpNode->children.push_back(partNode);
                    start = str.length();
                    break;
                }
                
                // Extract the expression inside \{...}
                std::string exprStr = str.substr(pos + 2, end - pos - 2);
                
                // Parse the expression using a sub-parser
                // Tokenize the expression string
                Lexer subLexer(exprStr, SyntaxConfig{});
                std::vector<Token> subTokens = subLexer.tokenize();
                
                // Parse the expression
                Parser subParser(subTokens, exprStr);
                ASTNodePtr exprNode = subParser.expression();
                
                ASTNodePtr interpExpr = std::make_shared<ASTNode>(NodeType::InterpExpr);
                interpExpr->children.push_back(exprNode);
                interpNode->children.push_back(interpExpr);
                
                start = end + 1;
            }
            
            // Add any remaining string after the last interpolation
            if (start < str.length()) {
                std::string rest = str.substr(start);
                ASTNodePtr partNode = std::make_shared<ASTNode>(NodeType::StringPart, rest);
                interpNode->children.push_back(partNode);
            }
            
            return interpNode;
        }
        
        return std::make_shared<ASTNode>(NodeType::Literal, str);
    }
    if (match(TokenType::NEW)) {
        // new ClassName(args...) or new module.ClassName(args...)
        Token firstPart = consume(TokenType::IDENTIFIER, "Expect class name after 'new'");
        std::string className = firstPart.lexeme;
        
        // Check for qualified class name: module.ClassName
        while (match(TokenType::DOT)) {
            Token nextPart = consume(TokenType::IDENTIFIER, "Expect identifier after '.'");
            className += "." + nextPart.lexeme;
        }
        
        consume(TokenType::LEFT_PAREN, "Expect '(' after class name");
        
        ASTNodePtr newNode = std::make_shared<ASTNode>(NodeType::New, className);
        
        // Parse constructor arguments
        if (!match(TokenType::RIGHT_PAREN)) {
            do {
                newNode->children.push_back(expression());
            } while (match(TokenType::COMMA));
            consume(TokenType::RIGHT_PAREN, "Expect ')' after constructor arguments");
        }
        
        return newNode;
    }
    if (match(TokenType::LEFT_BRACKET)) {
        current--; // back up
        advance(); // consume again
        return parseArrayLiteral();
    }
    if (match(TokenType::LEFT_BRACE)) {
        current--; // back up
        advance(); // consume again
        return parseDictLiteral();
    }
    if (match(TokenType::IDENTIFIER)) {
        std::string name = previous().lexeme;
        while (match(TokenType::DOT)) {
            Token id = consume(TokenType::IDENTIFIER, "Expect identifier after '.'.");
            name += "." + id.lexeme;
        }
        // apply alias
        size_t dot_pos = name.find('.');
        std::string first = (dot_pos == std::string::npos) ? name : name.substr(0, dot_pos);
        if (aliases.count(first)) {
            std::string rest = (dot_pos == std::string::npos) ? "" : name.substr(dot_pos);
            name = aliases[first] + rest;
        }
        // function call if followed by '('
        if (match(TokenType::LEFT_PAREN)) {
            ASTNodePtr call = std::make_shared<ASTNode>(NodeType::Call, name);
            // parse arguments
            if (!match(TokenType::RIGHT_PAREN)) {
                do {
                    call->children.push_back(expression());
                } while (match(TokenType::COMMA));
                consume(TokenType::RIGHT_PAREN, "Expect ')' after arguments.");
            }
            // Don't validate function existence here - extensions load at runtime
            return call;
        }
        // if identifier is followed by a literal without operator, that's an error
        if (!isAtEnd() && (peek().type == TokenType::INTEGER_LITERAL || peek().type == TokenType::DOUBLE_LITERAL || peek().type == TokenType::STRING_LITERAL)) {
            Logger::instance().log(LogLevel::ERROR, peek(), "Unexpected token after identifier; did you mean a function call?");
            throw std::runtime_error("Unexpected token after identifier");
        }
        return std::make_shared<ASTNode>(NodeType::Identifier, name);
    }
    if (match(TokenType::LEFT_PAREN)) {
        // Could be a lambda: (x, y) => ... or (x: int) => ...
        // Or a grouped expression: (expr)
        if (isLambdaStart()) {
            current--;  // Back up to re-consume '('
            return parseLambda();
        }
        ASTNodePtr expr = expression();
        consume(TokenType::RIGHT_PAREN, "Expect ')' after expression.");
        return expr;
    }
    // Error
    error(peek(), "Unexpected token");
    advance();
    return std::make_shared<ASTNode>(NodeType::Error);
}

// ============================================================================
// Type Annotations
// ============================================================================

TypeAnnotation Parser::parseTypeAnnotation() {
    std::string typeName;
    
    // Accept built-in type keywords or custom identifiers
    if (match(TokenType::INT)) {
        typeName = "int";
    } else if (match(TokenType::DOUBLE)) {
        typeName = "double";
    } else if (match(TokenType::STRING)) {
        typeName = "string";
    } else if (match(TokenType::BOOL)) {
        typeName = "bool";
    } else {
        Token type = consume(TokenType::IDENTIFIER, "Expect type name");
        typeName = type.lexeme;
    }
    
    TypeAnnotation annot(typeName);

    // Generic type parameters: e.g. dict<string, int>
    if (match(TokenType::LESS)) {
        // At least one type parameter
        TypeAnnotation first = parseTypeAnnotation();
        annot.generic_params.push_back(first.toString());
        while (match(TokenType::COMMA)) {
            TypeAnnotation next = parseTypeAnnotation();
            annot.generic_params.push_back(next.toString());
        }
        consume(TokenType::GREATER, "Expect '>' after type parameters");
    }
    
    // Check for optional marker
    if (match(TokenType::QUESTION)) {
        annot.is_optional = true;
    }
    
    // Check for array marker
    if (match(TokenType::LEFT_BRACKET)) {
        annot.is_array = true;
        consume(TokenType::RIGHT_BRACKET, "Expect ']' after '['");
    }
    
    return annot;
}

// ============================================================================
// Array and Dictionary Literals
// ============================================================================

ASTNodePtr Parser::parseArrayLiteral() {
    ASTNodePtr array = std::make_shared<ASTNode>(NodeType::Array);
    
    if (!match(TokenType::RIGHT_BRACKET)) {
        do {
            array->children.push_back(expression());
        } while (match(TokenType::COMMA));
        consume(TokenType::RIGHT_BRACKET, "Expect ']' after array elements");
    }
    
    return array;
}

ASTNodePtr Parser::parseDictLiteral() {
    ASTNodePtr dict = std::make_shared<ASTNode>(NodeType::Dict);
    
    if (!match(TokenType::RIGHT_BRACE)) {
        do {
            // Parse key (must be string)
            Token key = consume(TokenType::STRING_LITERAL, "Expect string key in dictionary");
            consume(TokenType::COLON, "Expect ':' after dictionary key");
            ASTNodePtr value = expression();
            
            // Remove quotes from string literal
            std::string keyStr = key.lexeme;
            if (keyStr.size() >= 2 && keyStr.front() == '"' && keyStr.back() == '"') {
                keyStr = keyStr.substr(1, keyStr.size() - 2);
            }
            
            ASTNodePtr pair = std::make_shared<ASTNode>(NodeType::Pair);
            pair->name = keyStr;  // Store key in name field
            pair->children = {value};
            dict->children.push_back(pair);
        } while (match(TokenType::COMMA));
        consume(TokenType::RIGHT_BRACE, "Expect '}' after dictionary");
    }
    
    return dict;
}

// ============================================================================
// Lambda/Closure Parsing
// ============================================================================

bool Parser::isLambdaStart() {
    // Check if current position (after '(') looks like a lambda
    // Lambdas: () => ..., (x) => ..., (x, y) => ..., (x: int) => ...
    
    size_t saved = current;
    int parenDepth = 1;  // We already consumed the opening '('
    
    // Scan to find matching ')' and check what follows
    while (!isAtEnd() && parenDepth > 0) {
        if (peek().type == TokenType::LEFT_PAREN) parenDepth++;
        else if (peek().type == TokenType::RIGHT_PAREN) parenDepth--;
        advance();
    }
    
    // If we found ')' and '=>' follows, it's a lambda
    bool isLambda = !isAtEnd() && peek().type == TokenType::FAT_ARROW;
    
    // Restore position
    current = saved;
    return isLambda;
}

ASTNodePtr Parser::parseLambda() {
    // Lambda syntax: (params) => expr or (params) => { block }
    // Also supports: x => expr for single parameter
    
    ASTNodePtr lambda = std::make_shared<ASTNode>(NodeType::Lambda);
    
    // Check if single identifier (no parens): x => ...
    if (peek().type == TokenType::IDENTIFIER && peekNext().type == TokenType::FAT_ARROW) {
        Token param = advance();  // consume identifier
        ASTNodePtr paramNode = std::make_shared<ASTNode>(NodeType::Parameter, param.lexeme);
        paramNode->annotationType = TypeAnnotation("dynamic");
        lambda->children.push_back(paramNode);
    } else {
        // Parenthesized parameters: (x, y) => ... or (x: int, y: string) => ...
        consume(TokenType::LEFT_PAREN, "Expect '(' before lambda parameters");
        
        if (!check(TokenType::RIGHT_PAREN)) {
            do {
                Token paramName = consume(TokenType::IDENTIFIER, "Expect parameter name");
                std::string paramType = "dynamic";
                
                // Optional type annotation
                if (match(TokenType::COLON)) {
                    TypeAnnotation typeAnnot = parseTypeAnnotation();
                    paramType = typeAnnot.name;
                }
                
                ASTNodePtr paramNode = std::make_shared<ASTNode>(NodeType::Parameter, paramName.lexeme);
                paramNode->annotationType = TypeAnnotation(paramType);
                lambda->children.push_back(paramNode);
            } while (match(TokenType::COMMA));
        }
        
        consume(TokenType::RIGHT_PAREN, "Expect ')' after lambda parameters");
    }
    
    consume(TokenType::FAT_ARROW, "Expect '=>' in lambda expression");
    
    // Check for optional return type annotation: (x) -> int => ...
    // Actually, we'll use a simpler syntax: the return type is inferred
    
    // Parse body: either a single expression or a block
    ASTNodePtr body;
    if (match(TokenType::LEFT_BRACE)) {
        // Block body: { statements }
        body = std::make_shared<ASTNode>(NodeType::Block);
        while (!isAtEnd() && !check(TokenType::RIGHT_BRACE)) {
            body->children.push_back(statement());
        }
        consume(TokenType::RIGHT_BRACE, "Expect '}' after lambda body");
        lambda->isMutable = false;  // Block body (not expression)
    } else {
        // Expression body: single expression
        body = expression();
        lambda->isMutable = true;  // Expression body
    }
    
    lambda->children.push_back(body);  // Body is last child
    
    return lambda;
}

// ============================================================================
// Function Type Annotation
// ============================================================================

TypeAnnotation Parser::parseFunctionType() {
    // Function type: (int, string) -> bool
    TypeAnnotation funcType;
    funcType.is_function = true;
    
    consume(TokenType::LEFT_PAREN, "Expect '(' in function type");
    
    if (!check(TokenType::RIGHT_PAREN)) {
        do {
            TypeAnnotation paramType = parseTypeAnnotation();
            funcType.param_types.push_back(paramType);
        } while (match(TokenType::COMMA));
    }
    
    consume(TokenType::RIGHT_PAREN, "Expect ')' after function parameter types");
    consume(TokenType::ARROW, "Expect '->' in function type");
    
    TypeAnnotation retType = parseTypeAnnotation();
    funcType.return_type = std::make_shared<TypeAnnotation>(retType);
    
    return funcType;
}
