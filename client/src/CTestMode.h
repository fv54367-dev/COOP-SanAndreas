#pragma once

#include "network/packets/players.h"

// Test mode: two game copies on the same PC against a local server (PROBAR_SOLO.bat).
// Switches, read before the game starts (the normal coopandreas.ini lives inside the user dir, too late for these):
//   <game dir>\CoopAndreas\coopandreas.ini, section [config]:
//     test-mode=true        own user files dir, input only in the focused window, test chat commands
//     user-dir=UserFiles    savegames, gta_sa.set and coopandreas.ini (relative to the game dir, or absolute)
//     windowed=true         windowed mode (default: same as test-mode)
//     window-pos=left       left | right | center | <x>,<y>
//   command line (overrides the file): -testmode  -userdir <path>  -windowed  -winpos <value>
// Without the file and the arguments nothing changes.
class CTestMode
{
public:
    static inline bool ms_bEnabled = false;
    static inline bool ms_bWindowed = false;

    // CCore::Init, before WinMain runs
    static void EarlyInit();
    // every frame while connected (main loop)
    static void Process();
    // returns true when `name` is a test command (also when test mode is off: it explains why it doesn't run)
    static bool HandleChatCommand(const std::wstring& name, const std::wstring& args);
    static void HandleTestCommand(Packets::Players::TestCommand* packet);
    static const char* GetHelpText();

    static bool IsGameWindowFocused();
};
