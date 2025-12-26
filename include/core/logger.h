#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <mutex>
#include "token.h"

enum class LogLevel { DEBUG, INFO, NOTICE, WARNING, ERROR };

// Simple thread-safe logger with Java-like levels and optional abort on ERROR
class Logger {
public:
    static Logger& instance();

    void log(LogLevel level, const std::string& message);
    void log(LogLevel level, const Token& token, const std::string& message);

    void setAbortOnError(bool v) { abortOnError = v; }
    bool getAbortOnError() const { return abortOnError; }
    
    void setMinLevel(LogLevel level) { minLevel = level; }
    LogLevel getMinLevel() const { return minLevel; }

private:
    Logger();
    std::mutex mtx;
    bool abortOnError;
    LogLevel minLevel;
    const char* levelName(LogLevel l) const;
};

// Quartz naming convention (Qz prefix)
using QzLogger = Logger;

#endif // LOGGER_H
