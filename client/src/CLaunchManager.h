#pragma once

class CMenuManager;

// command line given by the launcher (--coop, no serial key since 2026-11-11) and, optionally, by PROBAR_SOLO.bat:
//   -connect <ip>[:port]  -nick <name>  -autoload new|slot<1-8>
// with -connect the ip/port/nick go to coopandreas.ini; -autoload skips the menus (only together with -connect)
class CLaunchManager
{
public:
    enum class eAutoStart
    {
        NONE,
        NEW_GAME,
        SLOT
    };

    static void CollectCommandLineArgs();
    static void ApplyOverrides();  // after the config file was read
    static void ProcessAutoStart(CMenuManager* pMenu);

    static inline eAutoStart ms_eAutoStart = eAutoStart::NONE;
    static inline int ms_nAutoStartSlot = 0;
};
