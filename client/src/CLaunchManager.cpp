#include "stdafx.h"
#include <CLaunchManager.h>
#include <CMenuManager.h>
#include <CGenericGameStorage.h>

static char ms_szConnectIp[129] = "";
static unsigned short ms_nConnectPort = 0;
static char ms_szNick[Config::MAX_NICKNAME_LENGTH + 1] = "";

void CLaunchManager::CollectCommandLineArgs()
{
    // strtok writes into the buffer: work on a copy, not on the process command line
    std::string commandLine = GetCommandLineA();
    std::vector<char*> args;
    for (char* token = strtok(commandLine.data(), " "); token != NULL && args.size() < 200; token = strtok(NULL, " "))
    {
        args.push_back(token);
    }
    int argc = (int)args.size();

    // the game must not see our arguments (it quits on them): cut the real command line down to the exe path,
    // as the original in-place strtok did. CTestMode::EarlyInit already read the full line.
    for (char* token = strtok(GetCommandLineA(), " "); token != NULL; token = strtok(NULL, " "))
    {
    }

    for (int i = 0; i < argc; ++i) {
        // -connect <ip>[:port]  -nick <name>  -autoload new|slot<1-8> (used by PROBAR_SOLO.bat)
        if (strcmp(args[i], "-connect") == 0 && (i + 1) < argc) {
            std::string ip = args[i + 1];
            size_t colon = ip.find(':');
            if (colon != std::string::npos)
            {
                ms_nConnectPort = (unsigned short)atoi(ip.c_str() + colon + 1);
                ip.resize(colon);
            }
            strncpy_s(ms_szConnectIp, ip.c_str(), sizeof(ms_szConnectIp) - 1);
        }
        if (strcmp(args[i], "-nick") == 0 && (i + 1) < argc) {
            strncpy_s(ms_szNick, args[i + 1], sizeof(ms_szNick) - 1);
        }
        if (strcmp(args[i], "-autoload") == 0 && (i + 1) < argc) {
            std::string value = args[i + 1];
            if (value == "new")
                ms_eAutoStart = eAutoStart::NEW_GAME;
            else if (value.rfind("slot", 0) == 0)
            {
                int slot = atoi(value.c_str() + 4);
                if (slot >= 1 && slot <= 8)
                {
                    ms_eAutoStart = eAutoStart::SLOT;
                    ms_nAutoStartSlot = slot - 1;
                }
            }
        }
    }

    // without a server there is nothing to start into
    if (!ms_szConnectIp[0])
        ms_eAutoStart = eAutoStart::NONE;

    // no serial key since 2026-11-11: the launcher just starts the game with --coop
    // (the server never checked it); unknown arguments like the old -id/-serial are ignored
}

void CLaunchManager::ApplyOverrides()
{
    if (!ms_szConnectIp[0])
        return;

    strncpy_s(CNetwork::m_IpAddress, ms_szConnectIp, sizeof(CNetwork::m_IpAddress) - 1);
    if (ms_nConnectPort)
        CNetwork::m_nPort = ms_nConnectPort;
    if (ms_szNick[0])
        strncpy_s(CLocalPlayer::m_Name, ms_szNick, sizeof(CLocalPlayer::m_Name) - 1);

    CConfigLoader::Save();
}

// what the load screen does after confirming a slot (see 0x578EEA)
static bool StartLoadingSlot(CMenuManager* pMenu, int slot)
{
    if (!CGenericGameStorage::CheckSlotDataValid(slot, false))
        return false;

    uint8_t* pMenuBytes = reinterpret_cast<uint8_t*>(pMenu);
    pMenuBytes[0x15F] = (uint8_t)slot;  // selected slot
    if (!pMenu->m_bGameNotLoaded)
        pMenu->DoSettingsBeforeStartingAGame();
    pMenuBytes[0x32] = 1;     // m_bShutDownFrontEndRequested
    *(uint8_t*)0xB72910 = 0;  // CGame::bMissionPackGame
    pMenu->m_bWantToLoad = true;
    return true;
}

static void StartNewGame(CMenuManager* pMenu)
{
    *(uint8_t*)0xB72910 = 0;  // CGame::bMissionPackGame
    pMenu->DoSettingsBeforeStartingAGame();
    reinterpret_cast<uint8_t*>(pMenu)[0x32] = 1;
}

void CLaunchManager::ProcessAutoStart(CMenuManager* pMenu)
{
    static int frames = 0;

    if (ms_eAutoStart == eAutoStart::NONE || !pMenu->m_bGameNotLoaded || pMenu->m_nCurrentMenuPage != 0x22)
        return;

    // let the main menu settle first
    if (++frames < 30)
        return;

    eAutoStart mode = ms_eAutoStart;
    ms_eAutoStart = eAutoStart::NONE;

    // if the slot can't be loaded the menu stays and the player picks a game by hand
    if (mode == eAutoStart::SLOT)
        StartLoadingSlot(pMenu, ms_nAutoStartSlot);
    else if (mode == eAutoStart::NEW_GAME)
        StartNewGame(pMenu);
}
