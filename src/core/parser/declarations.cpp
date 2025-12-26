// ============================================================================
// Parser - Declarations Implementation
// Handles function, class, and variable declarations
// ============================================================================

#include "parser.h"
#include "logger.h"

// ============================================================================
// Variable Declaration
// ============================================================================

ASTNodePtr Parser::varDeclaration() {
    bool isMutable = previous().type == TokenType::VAR;
    Token nameToken = consume(TokenType::IDENTIFIER, "Expect variable name");
    
    ASTNodePtr decl = std::make_shared<ASTNode>(NodeType::VarDecl, nameToken.lexeme);
    decl->isMutable = isMutable;
    
    // Type annotation optional
    if (match(TokenType::COLON)) {
        decl->annotationType = parseTypeAnnotation();
    }
    
    // Value assignment required for now
    consume(TokenType::ASSIGN, "Expect '=' in variable declaration");
    ASTNodePtr value = expression();
    decl->children.push_back(value);

    // Minimal static validation for obvious type mismatches.
    // Quartz is still largely dynamic, but if the user explicitly annotates
    // a scalar type, reject array/dict literals assigned to it.
    if (!decl->annotationType.name.empty()) {
        const std::string& tname = decl->annotationType.name;
        const bool isAny = (tname == "dynamic" || tname == "any");

        if (!isAny) {
            if (value && value->type == NodeType::Array) {
                if (!decl->annotationType.is_array) {
                    Logger::instance().log(LogLevel::ERROR, nameToken,
                        "Type mismatch: cannot assign array literal to type '" + decl->annotationType.toString() + "'. "
                        "Use an array type like 'int[]', or omit the annotation.");
                    throw std::runtime_error("Type mismatch in variable declaration");
                }
            } else if (value && value->type == NodeType::Dict) {
                const bool isDictType = (tname == "dict");
                if (isDictType) {
                    // Quartz dict literals always have string keys.
                    if (!decl->annotationType.generic_params.empty()) {
                        if (decl->annotationType.generic_params.size() != 2 || decl->annotationType.generic_params[0] != "string") {
                            Logger::instance().log(LogLevel::ERROR, nameToken,
                                "Type mismatch: dict keys must be 'string' (e.g. dict<string, int>). Got '" + decl->annotationType.toString() + "'.");
                            throw std::runtime_error("Type mismatch in variable declaration");
                        }
                    }
                } else if (decl->annotationType.is_array || tname == "int" || tname == "double" || tname == "bool" || tname == "string") {
                    Logger::instance().log(LogLevel::ERROR, nameToken,
                        "Type mismatch: cannot assign dictionary literal to type '" + decl->annotationType.toString() + "'. "
                        "Omit the annotation, or use an appropriate mapping type.");
                    throw std::runtime_error("Type mismatch in variable declaration");
                }
            } else {
                // If user writes T[] = <non-array>, error.
                if (decl->annotationType.is_array) {
                    Logger::instance().log(LogLevel::ERROR, nameToken,
                        "Type mismatch: expected an array literal for type '" + decl->annotationType.toString() + "'.");
                    throw std::runtime_error("Type mismatch in variable declaration");
                }
            }
        }
    }
    
    consume(TokenType::SEMICOLON, "Expect ';' after declaration");
    
    // Track in symbol table
    definedVariables[nameToken.lexeme] = decl->annotationType;
    
    return decl;
}

// ============================================================================
// Function Definition
// ============================================================================

ASTNodePtr Parser::functionDef() {
    consume(TokenType::FN, "Expect 'fn' keyword");
    Token nameToken = consume(TokenType::IDENTIFIER, "Expect function name");
    consume(TokenType::LEFT_PAREN, "Expect '(' after function name");
    
    ASTNodePtr funcNode = std::make_shared<ASTNode>(NodeType::FunctionDef, nameToken.lexeme);
    
    // Parse parameters
    if (!match(TokenType::RIGHT_PAREN)) {
        do {
            Token paramName = consume(TokenType::IDENTIFIER, "Expect parameter name");
            std::string paramType = "dynamic"; // default type
            
            // Check for type annotation (e.g., ": int")
            if (match(TokenType::COLON)) {
                // Accept built-in type keywords or custom identifiers
                if (match(TokenType::INT)) {
                    paramType = "int";
                } else if (match(TokenType::DOUBLE)) {
                    paramType = "double";
                } else if (match(TokenType::STRING)) {
                    paramType = "string";
                } else if (match(TokenType::BOOL)) {
                    paramType = "bool";
                } else {
                    Token typeToken = consume(TokenType::IDENTIFIER, "Expect type name after ':'");
                    paramType = typeToken.lexeme;
                }
            }
            
            ASTNodePtr param = std::make_shared<ASTNode>(NodeType::Parameter, paramName.lexeme);
            param->annotationType = TypeAnnotation(paramType);
            funcNode->children.push_back(param);
        } while (match(TokenType::COMMA));
        consume(TokenType::RIGHT_PAREN, "Expect ')' after parameters");
    }
    
    // Parse return type annotation (optional)
    std::string returnType = "dynamic";
    if (match(TokenType::ARROW)) {
        // Accept built-in type keywords or custom identifiers
        if (match(TokenType::INT)) {
            returnType = "int";
        } else if (match(TokenType::DOUBLE)) {
            returnType = "double";
        } else if (match(TokenType::STRING)) {
            returnType = "string";
        } else if (match(TokenType::BOOL)) {
            returnType = "bool";
        } else {
            Token typeToken = consume(TokenType::IDENTIFIER, "Expect return type after '->'");
            returnType = typeToken.lexeme;
        }
    }
    funcNode->annotationType = TypeAnnotation(returnType);
    
    // Parse body
    consume(TokenType::LEFT_BRACE, "Expect '{' before function body");
    ASTNodePtr body = std::make_shared<ASTNode>(NodeType::Block);
    while (!isAtEnd() && !match(TokenType::RIGHT_BRACE)) {
        body->children.push_back(statement());
    }
    funcNode->children.push_back(body);
    
    return funcNode;
}

// ============================================================================
// Class Declaration
// ============================================================================

std::vector<std::string> Parser::parseGenericParams() {
    // Parse <T, U, V> generic type parameters
    std::vector<std::string> params;
    
    if (match(TokenType::LESS)) {  // <
        do {
            Token param = consume(TokenType::IDENTIFIER, "Expect type parameter name");
            params.push_back(param.lexeme);
        } while (match(TokenType::COMMA));
        consume(TokenType::GREATER, "Expect '>' after type parameters");
    }
    
    return params;
}

ASTNodePtr Parser::classDeclaration() {
    bool isAbstract = false;
    if (previous().type == TokenType::ABSTRACT) {
        isAbstract = true;
        consume(TokenType::CLASS, "Expect 'class' after 'abstract'");
    } else {
        consume(TokenType::CLASS, "Expect 'class' keyword");
    }
    
    Token nameToken = consume(TokenType::IDENTIFIER, "Expect class name");
    
    ASTNodePtr classNode = std::make_shared<ASTNode>(NodeType::ClassDef);
    classNode->name = nameToken.lexeme;
    classNode->value = nameToken.lexeme;
    
    // Parse generic type parameters: class Box<T> { ... }
    std::vector<std::string> genericParams = parseGenericParams();
    if (!genericParams.empty()) {
        // Store generic params as comma-separated in a special child
        ASTNodePtr genericNode = std::make_shared<ASTNode>(NodeType::GenericType);
        for (const auto& param : genericParams) {
            ASTNodePtr typeParam = std::make_shared<ASTNode>(NodeType::TypeParam, param);
            genericNode->children.push_back(typeParam);
        }
        classNode->children.push_back(genericNode);
    }
    
    // Check for inheritance: class Dog extends Animal
    if (match(TokenType::EXTENDS)) {
        Token parentName = consume(TokenType::IDENTIFIER, "Expect parent class name");
        classNode->annotationType.name = parentName.lexeme;  // Store parent class
    }
    
    // Check for interface implementation: class Dog implements Runnable, Comparable
    if (match(TokenType::IMPLEMENTS)) {
        std::string interfaces;
        do {
            Token ifaceName = consume(TokenType::IDENTIFIER, "Expect interface name");
            if (!interfaces.empty()) interfaces += ",";
            interfaces += ifaceName.lexeme;
        } while (match(TokenType::COMMA));
        classNode->annotationType.element_type = interfaces;  // Store interfaces
    }
    
    consume(TokenType::LEFT_BRACE, "Expect '{' before class body");
    
    // Parse class members
    while (!isAtEnd() && !check(TokenType::RIGHT_BRACE)) {
        // Check for visibility modifiers
        std::string visibility = "public";
        if (match(TokenType::PUBLIC)) {
            visibility = "public";
        } else if (match(TokenType::PRIVATE)) {
            visibility = "private";
        } else if (match(TokenType::PROTECTED)) {
            visibility = "protected";
        }
        
        // Check for static modifier
        bool isStatic = match(TokenType::STATIC);
        
        // Check for virtual/override modifiers (for methods)
        bool isVirtual = match(TokenType::VIRTUAL);
        bool isOverride = match(TokenType::OVERRIDE);
        
        // Check for abstract (method without body)
        bool isAbstractMember = match(TokenType::ABSTRACT);
        
        // Determine member type
        if (match(TokenType::CONSTRUCTOR)) {
            // Constructor: constructor(params) { ... }
            ASTNodePtr constructor = parseConstructor(visibility);
            classNode->children.push_back(constructor);
        } else if (check(TokenType::FN)) {
            // Method: fn name(params) -> type { ... }
            ASTNodePtr method = parseMethod(visibility, isStatic, isVirtual, isOverride);
            if (isAbstractMember) {
                method->annotationType.is_optional = true;  // Mark as abstract
            }
            classNode->children.push_back(method);
        } else {
            // Field: type name; or type name = value;
            ASTNodePtr field = parseField(visibility, isStatic);
            classNode->children.push_back(field);
        }
    }
    
    consume(TokenType::RIGHT_BRACE, "Expect '}' after class body");
    
    return classNode;
}

// ============================================================================
// Constructor Parsing
// ============================================================================

ASTNodePtr Parser::parseConstructor(const std::string& visibility) {
    // constructor(params) { body }
    // Also supports: constructor(params) : field1(value1), field2(value2) { body }
    
    ASTNodePtr constructorNode = std::make_shared<ASTNode>(NodeType::Constructor);
    constructorNode->name = visibility;
    
    consume(TokenType::LEFT_PAREN, "Expect '(' after 'constructor'");
    
    // Parse parameters
    if (!check(TokenType::RIGHT_PAREN)) {
        do {
            Token paramName = consume(TokenType::IDENTIFIER, "Expect parameter name");
            std::string paramType = "dynamic";
            
            if (match(TokenType::COLON)) {
                TypeAnnotation typeAnnot = parseTypeAnnotation();
                paramType = typeAnnot.name;
            }
            
            ASTNodePtr param = std::make_shared<ASTNode>(NodeType::Parameter, paramName.lexeme);
            param->annotationType = TypeAnnotation(paramType);
            constructorNode->children.push_back(param);
        } while (match(TokenType::COMMA));
    }
    
    consume(TokenType::RIGHT_PAREN, "Expect ')' after constructor parameters");
    
    // Check for field initializer list: constructor() : field1(val1), field2(val2)
    if (match(TokenType::COLON)) {
        // Parse initializer list
        ASTNodePtr initList = std::make_shared<ASTNode>(NodeType::Block);  // Use Block to hold initializers
        initList->name = "initializers";
        
        do {
            if (match(TokenType::SUPER)) {
                // super(args) call
                consume(TokenType::LEFT_PAREN, "Expect '(' after 'super'");
                ASTNodePtr superCall = std::make_shared<ASTNode>(NodeType::Super, "super");
                if (!check(TokenType::RIGHT_PAREN)) {
                    do {
                        superCall->children.push_back(expression());
                    } while (match(TokenType::COMMA));
                }
                consume(TokenType::RIGHT_PAREN, "Expect ')' after super arguments");
                initList->children.push_back(superCall);
            } else {
                // field(value) initializer
                Token fieldName = consume(TokenType::IDENTIFIER, "Expect field name in initializer");
                consume(TokenType::LEFT_PAREN, "Expect '(' after field name");
                ASTNodePtr value = expression();
                consume(TokenType::RIGHT_PAREN, "Expect ')' after initializer value");
                
                ASTNodePtr init = std::make_shared<ASTNode>(NodeType::Assign);
                init->name = fieldName.lexeme;
                init->children.push_back(value);
                initList->children.push_back(init);
            }
        } while (match(TokenType::COMMA));
        
        constructorNode->children.push_back(initList);
    }
    
    // Parse body
    consume(TokenType::LEFT_BRACE, "Expect '{' before constructor body");
    ASTNodePtr body = std::make_shared<ASTNode>(NodeType::Block);
    while (!isAtEnd() && !check(TokenType::RIGHT_BRACE)) {
        body->children.push_back(statement());
    }
    consume(TokenType::RIGHT_BRACE, "Expect '}' after constructor body");
    constructorNode->children.push_back(body);
    
    return constructorNode;
}

// ============================================================================
// Method Parsing
// ============================================================================

ASTNodePtr Parser::parseMethod(const std::string& visibility, bool isStatic, bool isVirtual, bool isOverride) {
    consume(TokenType::FN, "Expect 'fn' keyword");
    Token methodName = consume(TokenType::IDENTIFIER, "Expect method name");
    
    ASTNodePtr methodNode = std::make_shared<ASTNode>(
        isStatic ? NodeType::StaticMethod : NodeType::Method,
        methodName.lexeme
    );
    methodNode->name = visibility;
    
    // Store virtual/override flags
    if (isVirtual) methodNode->annotationType.is_optional = false;  // Will use a different flag
    if (isOverride) methodNode->annotationType.is_result = true;  // Reuse for override flag
    
    // Parse generic type parameters for method: fn map<T>(...)
    std::vector<std::string> genericParams = parseGenericParams();
    if (!genericParams.empty()) {
        ASTNodePtr genericNode = std::make_shared<ASTNode>(NodeType::GenericType);
        for (const auto& param : genericParams) {
            ASTNodePtr typeParam = std::make_shared<ASTNode>(NodeType::TypeParam, param);
            genericNode->children.push_back(typeParam);
        }
        methodNode->children.push_back(genericNode);
    }
    
    consume(TokenType::LEFT_PAREN, "Expect '(' after method name");
    
    // Parse parameters
    if (!check(TokenType::RIGHT_PAREN)) {
        do {
            Token paramName = consume(TokenType::IDENTIFIER, "Expect parameter name");
            std::string paramType = "dynamic";
            
            if (match(TokenType::COLON)) {
                TypeAnnotation typeAnnot = parseTypeAnnotation();
                paramType = typeAnnot.name;
            }
            
            ASTNodePtr param = std::make_shared<ASTNode>(NodeType::Parameter, paramName.lexeme);
            param->annotationType = TypeAnnotation(paramType);
            methodNode->children.push_back(param);
        } while (match(TokenType::COMMA));
    }
    
    consume(TokenType::RIGHT_PAREN, "Expect ')' after method parameters");
    
    // Parse return type
    std::string returnType = "void";
    if (match(TokenType::ARROW)) {
        TypeAnnotation typeAnnot = parseTypeAnnotation();
        returnType = typeAnnot.name;
    }
    methodNode->annotationType.name = returnType;
    
    // Parse body (or semicolon for abstract methods)
    if (match(TokenType::SEMICOLON)) {
        // Abstract method - no body
        methodNode->isMutable = true;  // Mark as abstract
    } else {
        consume(TokenType::LEFT_BRACE, "Expect '{' before method body");
        ASTNodePtr body = std::make_shared<ASTNode>(NodeType::Block);
        while (!isAtEnd() && !check(TokenType::RIGHT_BRACE)) {
            body->children.push_back(statement());
        }
        consume(TokenType::RIGHT_BRACE, "Expect '}' after method body");
        methodNode->children.push_back(body);
    }
    
    return methodNode;
}

// ============================================================================
// Field Parsing
// ============================================================================

ASTNodePtr Parser::parseField(const std::string& visibility, bool isStatic) {
    std::string fieldTypeName;
    
    // Accept built-in type keywords or custom identifiers for field type
    if (match(TokenType::INT)) {
        fieldTypeName = "int";
    } else if (match(TokenType::DOUBLE)) {
        fieldTypeName = "double";
    } else if (match(TokenType::STRING)) {
        fieldTypeName = "string";
    } else if (match(TokenType::BOOL)) {
        fieldTypeName = "bool";
    } else {
        Token fieldType = consume(TokenType::IDENTIFIER, "Expect field type");
        fieldTypeName = fieldType.lexeme;
        
        // Check for generic type arguments: List<T>
        if (match(TokenType::LESS)) {
            fieldTypeName += "<";
            do {
                if (match(TokenType::INT)) fieldTypeName += "int";
                else if (match(TokenType::DOUBLE)) fieldTypeName += "double";
                else if (match(TokenType::STRING)) fieldTypeName += "string";
                else if (match(TokenType::BOOL)) fieldTypeName += "bool";
                else {
                    Token genericArg = consume(TokenType::IDENTIFIER, "Expect type argument");
                    fieldTypeName += genericArg.lexeme;
                }
                if (check(TokenType::COMMA)) {
                    fieldTypeName += ", ";
                }
            } while (match(TokenType::COMMA));
            consume(TokenType::GREATER, "Expect '>' after type arguments");
            fieldTypeName += ">";
        }
    }
    
    Token fieldName = consume(TokenType::IDENTIFIER, "Expect field name");
    
    ASTNodePtr fieldNode = std::make_shared<ASTNode>(
        isStatic ? NodeType::StaticField : NodeType::Field,
        fieldName.lexeme
    );
    fieldNode->annotationType = TypeAnnotation(fieldTypeName);
    fieldNode->name = visibility;
    
    // Optional initialization
    if (match(TokenType::ASSIGN)) {
        ASTNodePtr initValue = expression();
        fieldNode->children.push_back(initValue);
    }
    
    consume(TokenType::SEMICOLON, "Expect ';' after field");
    
    return fieldNode;
}

// ============================================================================
// Interface Declaration
// ============================================================================

ASTNodePtr Parser::interfaceDeclaration() {
    // interface Name<T> extends Other { ... }
    consume(TokenType::INTERFACE, "Expect 'interface' keyword");
    Token nameToken = consume(TokenType::IDENTIFIER, "Expect interface name");
    
    ASTNodePtr interfaceNode = std::make_shared<ASTNode>(NodeType::InterfaceDef);
    interfaceNode->name = nameToken.lexeme;
    interfaceNode->value = nameToken.lexeme;
    
    // Parse generic type parameters
    std::vector<std::string> genericParams = parseGenericParams();
    if (!genericParams.empty()) {
        ASTNodePtr genericNode = std::make_shared<ASTNode>(NodeType::GenericType);
        for (const auto& param : genericParams) {
            ASTNodePtr typeParam = std::make_shared<ASTNode>(NodeType::TypeParam, param);
            genericNode->children.push_back(typeParam);
        }
        interfaceNode->children.push_back(genericNode);
    }
    
    // Check for interface extension
    if (match(TokenType::EXTENDS)) {
        std::string extended;
        do {
            Token extName = consume(TokenType::IDENTIFIER, "Expect interface name");
            if (!extended.empty()) extended += ",";
            extended += extName.lexeme;
        } while (match(TokenType::COMMA));
        interfaceNode->annotationType.name = extended;
    }
    
    consume(TokenType::LEFT_BRACE, "Expect '{' before interface body");
    
    // Parse interface members (abstract method signatures only)
    while (!isAtEnd() && !check(TokenType::RIGHT_BRACE)) {
        // Methods in interfaces are implicitly public and abstract
        if (match(TokenType::FN)) {
            Token methodName = consume(TokenType::IDENTIFIER, "Expect method name");
            
            ASTNodePtr methodNode = std::make_shared<ASTNode>(NodeType::Method, methodName.lexeme);
            methodNode->name = "public";
            methodNode->isMutable = true;  // Mark as abstract
            
            // Parse generic params for method
            std::vector<std::string> methodGenerics = parseGenericParams();
            if (!methodGenerics.empty()) {
                ASTNodePtr genericNode = std::make_shared<ASTNode>(NodeType::GenericType);
                for (const auto& param : methodGenerics) {
                    ASTNodePtr typeParam = std::make_shared<ASTNode>(NodeType::TypeParam, param);
                    genericNode->children.push_back(typeParam);
                }
                methodNode->children.push_back(genericNode);
            }
            
            consume(TokenType::LEFT_PAREN, "Expect '(' after method name");
            
            // Parse parameters
            if (!check(TokenType::RIGHT_PAREN)) {
                do {
                    Token paramName = consume(TokenType::IDENTIFIER, "Expect parameter name");
                    std::string paramType = "dynamic";
                    
                    if (match(TokenType::COLON)) {
                        TypeAnnotation typeAnnot = parseTypeAnnotation();
                        paramType = typeAnnot.name;
                    }
                    
                    ASTNodePtr param = std::make_shared<ASTNode>(NodeType::Parameter, paramName.lexeme);
                    param->annotationType = TypeAnnotation(paramType);
                    methodNode->children.push_back(param);
                } while (match(TokenType::COMMA));
            }
            
            consume(TokenType::RIGHT_PAREN, "Expect ')' after parameters");
            
            // Return type
            if (match(TokenType::ARROW)) {
                TypeAnnotation typeAnnot = parseTypeAnnotation();
                methodNode->annotationType.name = typeAnnot.name;
            } else {
                methodNode->annotationType.name = "void";
            }
            
            consume(TokenType::SEMICOLON, "Expect ';' after interface method");
            
            interfaceNode->children.push_back(methodNode);
        } else {
            error(peek(), "Interfaces can only contain method signatures");
            advance();
        }
    }
    
    consume(TokenType::RIGHT_BRACE, "Expect '}' after interface body");
    
    return interfaceNode;
}
