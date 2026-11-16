#include "stdafx.h"
#include "CServerConsole.h"
#include "CInternet.h"
#include "build_label.h"

#if defined(_WIN32)
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")
#endif

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

#include <cstdlib>
#include <atomic>

// E bug 9: the console reader thread blocks on std::getline and cannot be interrupted portably. Exiting through
// quick_exit skips the static destructors it uses (std::cin, the console queue and its mutex) and, since the thread is
// not detached, also avoids the std::terminate a joinable std::thread would trigger at shutdown.
static void ExitWithoutDestructors()
{
    static std::atomic<bool> exiting{false};
    if (exiting.exchange(true)) return;
    CInternet::Stop();
    std::quick_exit(0);
}

#if defined(_WIN32)
static BOOL WINAPI ConsoleCtrlHandler(DWORD)
{
    ExitWithoutDestructors();
    return TRUE;
}
#endif

std::filesystem::path GetExecutableDir()
{
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
#elif defined(__linux__)
    char buffer[4096];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len != -1)
    {
        buffer[len] = '\0';
        return std::filesystem::path(buffer).parent_path();
    }
    return std::filesystem::current_path();
#endif
}

int main(int argc, char* argv[])
{
#if defined(_WIN32)
    SetConsoleTitleW(L"COOP San Andreas Server");
#endif

    std::filesystem::path exeDir = GetExecutableDir();
    std::error_code ec;
    std::filesystem::current_path(exeDir, ec);

    // G1: the config decides whether everything printed from here on also goes to logs\server_AAAA-MM-DD.log
    CConfigManager::Init();
    CServerLog::Init(CConfigManager::GetConfigServerLogFile());

    CServerLog::Printf("[!] : Based on CoopAndreas by Tornamic (GPL-3.0) - https://github.com/Tornamic/CoopAndreas\n\n");

    CServerLog::Printf("[!] : COOP San Andreas Server \n");
#ifdef _DEBUG
    char config[] = "Debug";
#else
    char config[] = "Release";
#endif

    CServerLog::Printf("[!] : Version : %s (build %s), %s %s %s %s\n", COOPSA_VERSION, COOPSA_BUILD, config, sizeof(void*) == 8 ? "x64" : "x86",
        __DATE__, __TIME__);
#if defined(_WIN32)
    CServerLog::Printf("[!] : Platform : Microsoft Windows \n");
#else
    CServerLog::Printf("[!] : Platform : GNU/Linux | BSD \n");
#endif

    CServerTime::Init();

#if defined(_WIN32)
    // Ctrl+C / console close would otherwise run the static destructors while the reader thread is blocked on std::cin
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#endif

    CServerConsole::Start();

    CNetwork::Init(CConfigManager::GetConfigPort());

    // E bug 9: the main loop never returns; if it ever does (or the server fails to start) leave without running the
    // static destructors the console thread still uses
    ExitWithoutDestructors();
}