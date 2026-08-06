#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <mutex>
#include <ctime>
#include <iomanip>
#include <sstream>

enum class LogLevel { INFO, WARN, ERROR, DEBUG };

class Logger {
public:
    static Logger& instance() {
        static Logger instance;
        return instance;
    }

    void init(const std::string& filename) {
        std::lock_guard<std::mutex> lock(mtx);
        logFile.open(filename, std::ios::app);
    }

    void log(LogLevel level, const std::string& msg, const char* file, int line) {
        std::lock_guard<std::mutex> lock(mtx);
        
        // 1. Timestamp
        std::time_t now = std::time(nullptr);
        std::tm* tm_now = std::localtime(&now);
        std::stringstream ss_time;
        ss_time << "[" << std::put_time(tm_now, "%Y-%m-%d %H:%M:%S") << "] ";

        std::string timestamp = ss_time.str();
        std::string label;
        std::string colorCode;

        switch(level) {
            case LogLevel::INFO:
                label = "[INFO]  ";
                colorCode = "\033[32m"; // Green
                break;
            case LogLevel::WARN:
                label = "[WARN]  ";
                colorCode = "\033[33m"; // Yellow
                break;
            case LogLevel::ERROR:
                label = "[ERROR] ";
                colorCode = "\033[31m"; // Red
                break;
            case LogLevel::DEBUG:
                label = "[DEBUG] ";
                colorCode = "\033[36m"; // Cyan
                break;
        }

        std::stringstream ss_msg;
        ss_msg << label << msg << " (" << file << ":" << line << ")";
        std::string logContent = ss_msg.str();

        std::string resetCode = "\033[0m";
        std::ostream& target = (level == LogLevel::ERROR) ? std::cerr : std::cout;

        target << timestamp << colorCode << logContent << resetCode << std::endl;

        if (logFile.is_open()) {
            logFile << timestamp << logContent << std::endl;
        }
    }

    ~Logger() {
        if (logFile.is_open()) logFile.close();
    }

private:
    Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::mutex mtx;
    std::ofstream logFile;
};

#define LOG_INFO(msg)  Logger::instance().log(LogLevel::INFO, msg, __FILE__, __LINE__)
#define LOG_WARN(msg)  Logger::instance().log(LogLevel::WARN, msg, __FILE__, __LINE__)
#define LOG_ERROR(msg) Logger::instance().log(LogLevel::ERROR, msg, __FILE__, __LINE__)
#define LOG_DEBUG(msg) Logger::instance().log(LogLevel::DEBUG, msg, __FILE__, __LINE__)