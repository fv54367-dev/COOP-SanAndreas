#pragma once

#include <mutex>
#include <string>
#include <thread>
#include <vector>

// E: the server's console. A thread reads the standard input and hands the commands to the network loop through a
// mutex-protected queue, so the loop never blocks on the keyboard.
class CServerConsole
{
public:
    // spawns the reader thread; if stdin is not a console the thread just ends
    static void Start();
    // called every iteration of the network loop: runs the queued commands
    static void Process();

private:
    static void ReaderThread();
    static void Execute(const std::string& line);
    static void PrintHelp();
    static void PrintStatus();
    static void PrintPlayers();
    static void Kick(const std::string& args);
    static void Say(const std::string& args);

    static std::mutex ms_mutex;
    static std::vector<std::string> ms_queue;
    static std::thread ms_thread;
    static uint32_t ms_nStartTime;
};
