#include "function_registry.h"
#include "types.h"

#include <chrono>
#include <ctime>
#include <thread>
#include <iomanip>
#include <sstream>

void register_time_functions(FunctionRegistry& reg) {
    
    // ========================================================================
    // system.time.now - Get current Unix timestamp in seconds
    // ========================================================================
    auto now_func = [](const std::vector<Value>& args) -> Value {
        auto now = std::chrono::system_clock::now();
        auto epoch = now.time_since_epoch();
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(epoch).count();
        return Value(static_cast<int>(seconds));
    };
    reg.registerFunction("system.time.now", now_func);
    
    // ========================================================================
    // system.time.nowMs - Get current timestamp in milliseconds
    // ========================================================================
    auto nowMs_func = [](const std::vector<Value>& args) -> Value {
        auto now = std::chrono::system_clock::now();
        auto epoch = now.time_since_epoch();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count();
        // Return as int (may overflow for very large values, but practical for durations)
        return Value(static_cast<int>(ms % 2147483647));
    };
    reg.registerFunction("system.time.nowMs", nowMs_func);
    
    // ========================================================================
    // system.time.format - Format timestamp using strftime pattern
    // ========================================================================
    auto format_func = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(std::string(""));
        
        int timestamp = 0;
        if (std::holds_alternative<int>(args[0])) {
            timestamp = std::get<int>(args[0]);
        } else {
            return Value(std::string(""));
        }
        
        std::string format = "";
        if (std::holds_alternative<std::string>(args[1])) {
            format = std::get<std::string>(args[1]);
        } else {
            return Value(std::string(""));
        }
        
        std::time_t time = static_cast<std::time_t>(timestamp);
        std::tm* tm_info = std::localtime(&time);
        if (!tm_info) {
            return Value(std::string(""));
        }
        
        char buffer[256];
        std::strftime(buffer, sizeof(buffer), format.c_str(), tm_info);
        return Value(std::string(buffer));
    };
    reg.registerFunction("system.time.format", format_func);
    
    // ========================================================================
    // system.time.parse - Parse date string to timestamp
    // ========================================================================
    auto parse_func = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(0);
        
        std::string dateStr = "";
        if (std::holds_alternative<std::string>(args[0])) {
            dateStr = std::get<std::string>(args[0]);
        } else {
            return Value(0);
        }
        
        std::string format = "";
        if (std::holds_alternative<std::string>(args[1])) {
            format = std::get<std::string>(args[1]);
        } else {
            return Value(0);
        }
        
        std::tm tm_info = {};
        std::istringstream ss(dateStr);
        ss >> std::get_time(&tm_info, format.c_str());
        
        if (ss.fail()) {
            return Value(0);
        }
        
        std::time_t time = std::mktime(&tm_info);
        return Value(static_cast<int>(time));
    };
    reg.registerFunction("system.time.parse", parse_func);
    
    // ========================================================================
    // system.time.year - Extract year from timestamp
    // ========================================================================
    auto year_func = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(0);
        
        int timestamp = 0;
        if (std::holds_alternative<int>(args[0])) {
            timestamp = std::get<int>(args[0]);
        } else {
            return Value(0);
        }
        
        std::time_t time = static_cast<std::time_t>(timestamp);
        std::tm* tm_info = std::localtime(&time);
        if (!tm_info) return Value(0);
        
        return Value(tm_info->tm_year + 1900);
    };
    reg.registerFunction("system.time.year", year_func);
    
    // ========================================================================
    // system.time.month - Extract month from timestamp (1-12)
    // ========================================================================
    auto month_func = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(0);
        
        int timestamp = 0;
        if (std::holds_alternative<int>(args[0])) {
            timestamp = std::get<int>(args[0]);
        } else {
            return Value(0);
        }
        
        std::time_t time = static_cast<std::time_t>(timestamp);
        std::tm* tm_info = std::localtime(&time);
        if (!tm_info) return Value(0);
        
        return Value(tm_info->tm_mon + 1);  // tm_mon is 0-11
    };
    reg.registerFunction("system.time.month", month_func);
    
    // ========================================================================
    // system.time.day - Extract day of month from timestamp (1-31)
    // ========================================================================
    auto day_func = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(0);
        
        int timestamp = 0;
        if (std::holds_alternative<int>(args[0])) {
            timestamp = std::get<int>(args[0]);
        } else {
            return Value(0);
        }
        
        std::time_t time = static_cast<std::time_t>(timestamp);
        std::tm* tm_info = std::localtime(&time);
        if (!tm_info) return Value(0);
        
        return Value(tm_info->tm_mday);
    };
    reg.registerFunction("system.time.day", day_func);
    
    // ========================================================================
    // system.time.hour - Extract hour from timestamp (0-23)
    // ========================================================================
    auto hour_func = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(0);
        
        int timestamp = 0;
        if (std::holds_alternative<int>(args[0])) {
            timestamp = std::get<int>(args[0]);
        } else {
            return Value(0);
        }
        
        std::time_t time = static_cast<std::time_t>(timestamp);
        std::tm* tm_info = std::localtime(&time);
        if (!tm_info) return Value(0);
        
        return Value(tm_info->tm_hour);
    };
    reg.registerFunction("system.time.hour", hour_func);
    
    // ========================================================================
    // system.time.minute - Extract minute from timestamp (0-59)
    // ========================================================================
    auto minute_func = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(0);
        
        int timestamp = 0;
        if (std::holds_alternative<int>(args[0])) {
            timestamp = std::get<int>(args[0]);
        } else {
            return Value(0);
        }
        
        std::time_t time = static_cast<std::time_t>(timestamp);
        std::tm* tm_info = std::localtime(&time);
        if (!tm_info) return Value(0);
        
        return Value(tm_info->tm_min);
    };
    reg.registerFunction("system.time.minute", minute_func);
    
    // ========================================================================
    // system.time.second - Extract second from timestamp (0-59)
    // ========================================================================
    auto second_func = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(0);
        
        int timestamp = 0;
        if (std::holds_alternative<int>(args[0])) {
            timestamp = std::get<int>(args[0]);
        } else {
            return Value(0);
        }
        
        std::time_t time = static_cast<std::time_t>(timestamp);
        std::tm* tm_info = std::localtime(&time);
        if (!tm_info) return Value(0);
        
        return Value(tm_info->tm_sec);
    };
    reg.registerFunction("system.time.second", second_func);
    
    // ========================================================================
    // system.time.weekday - Day of week (0=Sunday, 6=Saturday)
    // ========================================================================
    auto weekday_func = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(0);
        
        int timestamp = 0;
        if (std::holds_alternative<int>(args[0])) {
            timestamp = std::get<int>(args[0]);
        } else {
            return Value(0);
        }
        
        std::time_t time = static_cast<std::time_t>(timestamp);
        std::tm* tm_info = std::localtime(&time);
        if (!tm_info) return Value(0);
        
        return Value(tm_info->tm_wday);
    };
    reg.registerFunction("system.time.weekday", weekday_func);
    
    // ========================================================================
    // system.time.addSeconds - Add seconds to timestamp
    // ========================================================================
    auto addSeconds_func = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(0);
        
        int timestamp = 0;
        if (std::holds_alternative<int>(args[0])) {
            timestamp = std::get<int>(args[0]);
        } else {
            return Value(0);
        }
        
        int seconds = 0;
        if (std::holds_alternative<int>(args[1])) {
            seconds = std::get<int>(args[1]);
        } else {
            return Value(0);
        }
        
        return Value(timestamp + seconds);
    };
    reg.registerFunction("system.time.addSeconds", addSeconds_func);
    
    // ========================================================================
    // system.time.addDays - Add days to timestamp
    // ========================================================================
    auto addDays_func = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(0);
        
        int timestamp = 0;
        if (std::holds_alternative<int>(args[0])) {
            timestamp = std::get<int>(args[0]);
        } else {
            return Value(0);
        }
        
        int days = 0;
        if (std::holds_alternative<int>(args[1])) {
            days = std::get<int>(args[1]);
        } else {
            return Value(0);
        }
        
        return Value(timestamp + (days * 86400));  // 86400 seconds per day
    };
    reg.registerFunction("system.time.addDays", addDays_func);
    
    // ========================================================================
    // system.time.diff - Difference between two timestamps in seconds
    // ========================================================================
    auto diff_func = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(0);
        
        int t1 = 0, t2 = 0;
        if (std::holds_alternative<int>(args[0])) {
            t1 = std::get<int>(args[0]);
        }
        if (std::holds_alternative<int>(args[1])) {
            t2 = std::get<int>(args[1]);
        }
        
        return Value(t1 - t2);
    };
    reg.registerFunction("system.time.diff", diff_func);
    
    // ========================================================================
    // system.time.sleep - Pause execution for milliseconds
    // ========================================================================
    auto sleep_func = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value();
        
        int ms = 0;
        if (std::holds_alternative<int>(args[0])) {
            ms = std::get<int>(args[0]);
        } else {
            return Value();
        }
        
        if (ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        }
        
        return Value();
    };
    reg.registerFunction("system.time.sleep", sleep_func);
    
    // ========================================================================
    // system.time.toLocal - Convert UTC timestamp to local timezone
    // ========================================================================
    auto toLocal_func = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(0);
        
        int timestamp = 0;
        if (std::holds_alternative<int>(args[0])) {
            timestamp = std::get<int>(args[0]);
        } else {
            return Value(0);
        }
        
        // Get local time offset
        std::time_t now = std::time(nullptr);
        std::tm* local_tm = std::localtime(&now);
        std::time_t local_time = std::mktime(local_tm);
        std::tm* gm_tm = std::gmtime(&now);
        std::time_t gm_time = std::mktime(gm_tm);
        
        int offset = static_cast<int>(local_time - gm_time);
        return Value(timestamp + offset);
    };
    reg.registerFunction("system.time.toLocal", toLocal_func);
    
    // ========================================================================
    // system.time.toUtc - Convert local timestamp to UTC
    // ========================================================================
    auto toUtc_func = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(0);
        
        int timestamp = 0;
        if (std::holds_alternative<int>(args[0])) {
            timestamp = std::get<int>(args[0]);
        } else {
            return Value(0);
        }
        
        // Get local time offset
        std::time_t now = std::time(nullptr);
        std::tm* local_tm = std::localtime(&now);
        std::time_t local_time = std::mktime(local_tm);
        std::tm* gm_tm = std::gmtime(&now);
        std::time_t gm_time = std::mktime(gm_tm);
        
        int offset = static_cast<int>(local_time - gm_time);
        return Value(timestamp - offset);
    };
    reg.registerFunction("system.time.toUtc", toUtc_func);
}
