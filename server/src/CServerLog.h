#pragma once

#include <cstdio>
#include <mutex>
#include <string>

// G1: everything the server prints on the console (printf and logger) is also appended to
// server\logs\server_AAAA-MM-DD.log with a timestamp. `server-log-file` (default 1) turns it on; with it off nothing is
// written to a file and the console behaves exactly as before.
class CServerLog
{
public:
    // opens logs\server_<today>.log when enabled; called from main.cpp once the config has been read
    static void Init(bool enabled);
    // console printf, kept, plus the same line in the file
    static void Printf(const char* format, ...);
    // logger sink (shared/logger.h): the console line is already printed by logger, only the file is written here
    static void LoggerSink(const char* level, const char* text);

private:
    // the mutex must be held by the caller
    static void WriteLineLocked(const char* level, const char* text);
    static std::string Timestamp();

    static bool ms_bEnabled;
    static std::FILE* ms_pFile;
    static std::mutex ms_mutex;
};
