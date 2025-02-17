#pragma once
#include <iostream>
#include <sstream>
#include <string>
#include <chrono>
#include <mutex>

class Logger {
public:
    enum class Level {
        DEBUG,
        INFO,
        WARNING,
        ERROR
    };

    static Logger& Get() {
        static Logger instance;
        return instance;
    }

    template<typename... Args>
    void Log(Level level, Args&&... args) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        std::stringstream ss;
        ss << "[" << GetTimestamp() << "] ";
        ss << GetLevelString(level) << ": ";
        (ss << ... << std::forward<Args>(args));
        
        std::cout << ss.str() << std::endl;
        
        if (level == Level::ERROR) {
            m_hasError = true;
            m_lastError = ss.str();
        }
    }

    bool HasError() const { return m_hasError; }
    std::string GetLastError() const { return m_lastError; }
    void ClearError() { m_hasError = false; m_lastError.clear(); }

private:
    Logger() = default;
    
    std::string GetTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        char buffer[26];
        ctime_r(&time, buffer);
        buffer[24] = '\0'; 
        return buffer;
    }

    const char* GetLevelString(Level level) {
        switch (level) {
            case Level::DEBUG: return "DEBUG";
            case Level::INFO: return "INFO";
            case Level::WARNING: return "WARNING";
            case Level::ERROR: return "ERROR";
            default: return "UNKNOWN";
        }
    }

    std::mutex m_mutex;
    bool m_hasError = false;
    std::string m_lastError;
};

#define LOG_DEBUG(...) Logger::Get().Log(Logger::Level::DEBUG, __VA_ARGS__)
#define LOG_INFO(...) Logger::Get().Log(Logger::Level::INFO, __VA_ARGS__)
#define LOG_WARN(...) Logger::Get().Log(Logger::Level::WARNING, __VA_ARGS__)
#define LOG_ERROR(...) Logger::Get().Log(Logger::Level::ERROR, __VA_ARGS__)