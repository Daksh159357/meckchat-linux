#include "meckchat/core/logger.h"

namespace MeckChat::Core {

std::mutex Logger::s_mutex;

void Logger::init() {
    // Setup logging format or file handlers if needed
}

QString Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "UNKNOWN";
}

void Logger::log(LogLevel level, const QString &tag, const QString &message) {
    std::lock_guard<std::mutex> lock(s_mutex);
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    QString logLine = QString("[%1] [%2] [%3] %4")
                          .arg(timestamp, levelToString(level), tag, message);
    std::cout << logLine.toStdString() << std::endl;
}

void Logger::debug(const QString &tag, const QString &message) {
    log(LogLevel::Debug, tag, message);
}

void Logger::info(const QString &tag, const QString &message) {
    log(LogLevel::Info, tag, message);
}

void Logger::warning(const QString &tag, const QString &message) {
    log(LogLevel::Warning, tag, message);
}

void Logger::error(const QString &tag, const QString &message) {
    log(LogLevel::Error, tag, message);
}

} // namespace MeckChat::Core
