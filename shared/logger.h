#pragma once

class logger
{
private:
    static void log(const char* text, const char* level, const char* color)
    {
        std::cout << color << "[" << level << "]" << "\033[0m" << ": " << text << std::endl;

        // G1: the server sets this to also write the line to its log file (the client leaves it null)
        if (ms_pfnExtraSink != nullptr)
        {
            ms_pfnExtraSink(level, text);
        }
    }

public:
    // G1: optional extra sink (server/src/CServerLog.cpp), gets the level name and the already formatted text
    inline static void (*ms_pfnExtraSink)(const char* level, const char* text) = nullptr;

    static void info(const char* format, ...)
    {
        char buffer[512];
        va_list args;
        va_start(args, format);
        std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        log(buffer, "info", "\033[34m");
    }

    static void warn(const char* format, ...)
    {
        char buffer[512];
        va_list args;
        va_start(args, format);
        std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        log(buffer, "warn", "\033[33m");
    }

    static void error(const char* format, ...)
    {
        char buffer[512];
        va_list args;
        va_start(args, format);
        std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        log(buffer, "error", "\033[31m");
    }
};
