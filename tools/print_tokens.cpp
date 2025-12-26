#include <iostream>
#include <fstream>
#include <sstream>
#include "lexer.h"
#include "syntax.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <source_file>" << std::endl;
        return 1;
    }
    std::ifstream f(argv[1]);
    std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    SyntaxConfig config;
    Lexer lexer(src, config);
    auto tokens = lexer.tokenize();
    for (auto &t : tokens) {
        std::cout << "(" << t.line << ":" << t.column << ") " << int(t.type) << " '" << t.lexeme << "'" << std::endl;
    }
    return 0;
}
