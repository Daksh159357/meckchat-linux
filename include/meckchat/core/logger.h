#pragma once

#include <QString>
#include <QDateTime>
#include <iostream>
#include <mutex>

namespace MeckChat::Core {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

class Logger {
public:
    static void init();
    static void log(LogLevel level, const QString &tag, const QString &message);
    static void debug(const QString &tag, const QString &message);
    static void info(const QString &tag, const QString &message);
    static void warning(const QString &tag, const QString &message);
    static void error(const QString &tag, const QString &message);

private:
    static std::mutex s_mutex;
    static QString levelToString(LogLevel level);
};

} // namespace MeckChat::Core
