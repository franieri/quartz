#ifndef QZ_CORE_H
#define QZ_CORE_H

// Public Quartz (qz::) API layer.
// This header exposes the existing implementation types under a consistent namespace.

#include "core/types.h"
#include "core/token.h"
#include "core/syntax.h"
#include "core/logger.h"
#include "core/lexer.h"
#include "core/parser.h"
#include "core/function_registry.h"
#include "core/runtime.h"
#include "core/bytecode.h"
#include "core/bytecode/compiler.h"
#include "core/bytecode/vm.h"

namespace qz {
    using ::Value;
    using ::Token;
    using ::SyntaxConfig;

    using ::Logger;
    using ::Lexer;
    using ::Parser;
    using ::FunctionRegistry;
    using ::Runtime;

    using ::BytecodeCompiler;
    using ::BytecodeVM;

    namespace bc = ::bc;
}

#endif // QZ_CORE_H
