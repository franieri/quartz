#include "logger.h"
#include <iostream>

Logger& Logger::instance() {
    static Logger lg;
    return lg;
}

// Default: only show WARNING and ERROR (suppress DEBUG/INFO/NOTICE)
Logger::Logger() : minLevel(LogLevel::WARNING) {}

const char* Logger::levelName(LogLevel l) const {
    switch (l) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::NOTICE: return "NOTICE";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR: return "ERROR";
    }
    return "UNKNOWN";
}

void Logger::log(LogLevel level, const std::string& message) {
    // Skip if below minimum level
    if (level < minLevel) return;
    
    std::lock_guard<std::mutex> lk(mtx);
    std::cerr << "[" << levelName(level) << "] " << message << std::endl;
    std::cerr << std::flush;
}

void Logger::log(LogLevel level, const Token& token, const std::string& message) {
    // Skip if below minimum level
    if (level < minLevel) return;
    
    std::lock_guard<std::mutex> lk(mtx);
    std::cerr << "[" << levelName(level) << "] ";
    if (!token.lexeme.empty()) {
        std::cerr << "[" << token.line << ":" << token.column << "] ";
    }
    std::cerr << message;
    if (!token.lexeme.empty()) std::cerr << " (near '" << token.lexeme << "')";
    std::cerr << std::endl;
    std::cerr << std::flush;
}
