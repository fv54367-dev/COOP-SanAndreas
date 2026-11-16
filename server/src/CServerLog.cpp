#include "stdafx.h"
#include "CServerLog.h"

#include <cstdarg>
#include <ctime>

bool CServerLog::ms_bEnabled = false;
std::FILE* CServerLog::ms_pFile = nullptr;
std::mutex CServerLog::ms_mutex;

std::string CServerLog::Timestamp()
{
    std::time_t now = std::time(nullptr);
    std::tm tmNow{};
#if defined(_WIN32)
    localtime_s(&tmNow, &now);
#else
    localtime_r(&now, &tmNow);
#endif

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tmNow);
    return buffer;
}

void CServerLog::Init(bool enabled)
{
    std::lock_guard<std::mutex> lock(ms_mutex);

    if (ms_pFile != nullptr)
    {
        std::fclose(ms_pFile);
        ms_pFile = nullptr;
    }

    ms_bEnabled = enabled;
    logger::ms_pfnExtraSink = nullptr;

    if (!enabled)
    {
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories("logs", ec);

    std::time_t now = std::time(nullptr);
    std::tm tmNow{};
#if defined(_WIN32)
    localtime_s(&tmNow, &now);
#else
    localtime_r(&now, &tmNow);
#endif

    char date[16];
    std::strftime(date, sizeof(date), "%Y-%m-%d", &tmNow);

    const std::string path = std::string("logs/server_") + date + ".log";
    ms_pFile = std::fopen(path.c_str(), "a");
    if (ms_pFile == nullptr)
    {
        ms_bEnabled = false;
        return;
    }

    WriteLineLocked("info", "server log started");
    logger::ms_pfnExtraSink = &CServerLog::LoggerSink;
}

void CServerLog::WriteLineLocked(const char* level, const char* text)
{
    if (ms_pFile == nullptr)
    {
        return;
    }

    std::string line = "[" + Timestamp() + "] [" + level + "]: " + text;
    if (line.empty() || line.back() != '\n')
    {
        line += '\n';
    }

    std::fputs(line.c_str(), ms_pFile);
    std::fflush(ms_pFile);
}

void CServerLog::Printf(const char* format, ...)
{
    char buffer[2048];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    std::fputs(buffer, stdout);

    if (!ms_bEnabled)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(ms_mutex);
    WriteLineLocked("info", buffer);
}

void CServerLog::LoggerSink(const char* level, const char* text)
{
    if (!ms_bEnabled)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(ms_mutex);
    WriteLineLocked(level, text);
}
