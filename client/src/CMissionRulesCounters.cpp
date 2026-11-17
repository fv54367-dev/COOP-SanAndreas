#include "stdafx.h"
#include "CMissionRulesCounters.h"
#include "CMissionSyncRules.h"
#include "CMissionMirror.h"
#include "CMissionMode.h"
#include "CMissionHints.h"
#include "CNetworkStaticBlip.h"
#include "COpCodeSync.h"
#include "CCoopMission.h"
#include "CConfigLoader.h"
#include "CCutsceneMgr.h"

namespace
{
struct SGuestLauncher
{
    const char* name;
    int chain;
    int counterOffset;
    int endCounter;
    int prerequisiteChain;
    int prerequisiteCounter;
    bool searched = false;
    DWORD lastStart = 0;
    int offset = -1;
    // what this game took from the host and never saves (each player keeps his own progress, the user 12.11)
    int ownCounter = -1;     // the counter before it was raised to the host's
    int copiedCounter = -1;  // the host's value it was raised to
    CRunningScript* started = nullptr;
};
SGuestLauncher s_aGuestLaunchers[] = {
    {"SWEET", 0, 1808, 9, 0, 0},
    {"RYDER", 1, 1812, 3, 0, 5},
    {"SMOKE", 2, 1816, 4, 0, 4},
    {"STRAP", 3, 1820, 5, 2, 1},
    // MOB_LA1 starts CRASH after OG Loc's second mission and CESAR after Sweet's seventh; FLOW starts LA1FIN with every
    // Los Santos chain done; LA1FIN2 (The Green Sabre) starts BCRASH. Their contact points come from INITIAL.
    {"CRASH", 4, 1824, 2, 3, 2},
    {"CESAR", 5, 1828, 1, 0, 7},
    {"LA1FIN", 6, 1832, 2, 0, 9},
    {"BCRASH", 9, 1972, 1, 6, 2},
    // MOB_CAT (started by LA1FIN2) phones CAT in once Badlands is passed, and TRU with Catalina's first date done.
    // BCESAR (Wu Zi Mu races) stays out: it also starts the race tournament (35), one copy per player.
    {"CAT", 7, 256, 4, 9, 1},
    {"TRU", 8, 1964, 2, 7, 1},
};

bool IsActiveScript(CRunningScript* script)
{
    for (auto active = CTheScripts::pActiveScripts; active; active = active->m_pNext)
        if (active == script)
            return true;
    return false;
}

}

// CGenericGameStorage::GenericSave (0x5D13E0), called by C_PcSave::SaveSlot at 0x619081: the save holds this player's own
// progress only. The launchers started from the host's and the counters raised to his are taken out while it writes.
bool __cdecl CMissionRulesCounters::SaveOwnProgress(int arg)
{
    CRunningScript* removed[std::size(s_aGuestLaunchers)]{};
    int raised[std::size(s_aGuestLaunchers)];
    for (size_t i = 0; i < std::size(s_aGuestLaunchers); ++i)
    {
        auto& launcher = s_aGuestLaunchers[i];
        int& counter = *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[launcher.counterOffset]);
        raised[i] = -1;
        if (launcher.ownCounter >= 0 && counter == launcher.copiedCounter)
        {
            raised[i] = counter;
            counter = launcher.ownCounter;
        }
        // own progress past the chain's first mission: the launcher is his too. Taken out, a loaded save had no Sweet launcher
        // at all and Cesar Vialpando never started for that host (testeo 14-11)
        if (launcher.started && counter <= 0 && IsActiveScript(launcher.started) &&
            _strnicmp(launcher.started->m_szName, launcher.name, 8) == 0)
        {
            launcher.started->RemoveScriptFromList(&CTheScripts::pActiveScripts);
            removed[i] = launcher.started;
        }
        if (raised[i] >= 0 || removed[i])
            CMissionMirror::LogEventf("guest-launcher save-own name=%s counter=%d thread=%d", launcher.name, counter, removed[i] ? 0 : 1);
    }
    int& intro = *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[1792]);
    const int introRaised = CNetworkStaticBlip::ms_nIntroOwnCounter >= 0 && intro == CNetworkStaticBlip::ms_nIntroCopiedCounter
        ? intro : -1;
    if (introRaised >= 0)
        intro = CNetworkStaticBlip::ms_nIntroOwnCounter;
    CNetworkStaticBlip::BeforeSave();
    const bool result = plugin::CallAndReturn<bool, 0x5D13E0, int>(arg);
    CNetworkStaticBlip::AfterSave();
    if (introRaised >= 0)
        intro = introRaised;
    for (size_t i = 0; i < std::size(s_aGuestLaunchers); ++i)
    {
        if (raised[i] >= 0)
            *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[s_aGuestLaunchers[i].counterOffset]) = raised[i];
        if (removed[i])
            removed[i]->AddScriptToList(&CTheScripts::pActiveScripts);
    }
    return result;
}

int CMissionSyncRules::OwnChainCounter(int chain, int current)
{
    for (const auto& launcher : s_aGuestLaunchers)
        if (launcher.chain == chain && launcher.ownCounter >= 0 && current == launcher.copiedCounter)
            return launcher.ownCounter;
    return current;
}

void CMissionSyncRules::ForgetOwnCounters()
{
    for (auto& launcher : s_aGuestLaunchers)
        launcher.ownCounter = launcher.copiedCounter = -1;
    CNetworkStaticBlip::ms_nIntroOwnCounter = CNetworkStaticBlip::ms_nIntroCopiedCounter = -1;
}

void CMissionRulesCounters::Process()
{
    auto& launchers = s_aGuestLaunchers;
    static unsigned int lastFrame = 0;
    static unsigned int session = 1;  // a new one at every game started or loaded
    const unsigned int frame = CTimer::m_FrameCounter;
    if (frame < lastFrame)
    {
        ++session;
        for (auto& launcher : launchers)
        {
            launcher.searched = false;
            launcher.offset = -1;
            launcher.lastStart = 0;
            launcher.ownCounter = launcher.copiedCounter = -1;
            launcher.started = nullptr;
        }
        CNetworkStaticBlip::ms_nIntroOwnCounter = CNetworkStaticBlip::ms_nIntroCopiedCounter = -1;
    }
    lastFrame = frame;
    CPlayerPed* player = FindPlayerPed(0);
    if (!CMissionSyncRules::IsEnabled() || !CConfigLoader::ms_bGuestStoryLaunchers ||
        !CConfigLoader::ms_bVanillaScripts || !CNetwork::m_bConnected || !CNetwork::m_bAuthenticated ||
        COpCodeSync::bProcessingNetworkOpcode || !player ||
        player->m_fHealth <= 0.0f || CWorld::Players[0].m_nPlayerState != PLAYERSTATE_PLAYING ||
        CCutsceneMgr::ms_running || TheCamera.m_bWideScreenOn || !CTheScripts::pActiveScripts ||
        *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[1636]) != 0)
        return;

    // the host follows no one: only his own launchers missing from a save are started again (below)
    const bool authority = CCoopMission::IsScriptAuthority();

    // intro-follow-host: a save of this game went on with Sweet but its intro counter stayed behind (the guest played
    // Big Smoke/Ryder on the host's game, testeo 13-11): the old CJ/Ryder blip showed again after loading it
    if (!authority && CConfigLoader::ms_bIntroFollowHost &&
        CMissionSyncRules::OwnChainCounter(0, *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[1808])) > 0 &&
        CNetworkStaticBlip::ms_nIntroOwnCounter < 0)
    {
        const int intro = *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[1792]);
        if (intro == 0 || intro == 1)
            CNetworkStaticBlip::SetIntroCounter(2);
    }

    // INT (Big Smoke / Ryder) gone while the intro is still pending: it terminated at a counter of 2 that came back down
    // (testeo 14-11: no "waiting" at Ryder's marker for the guest). Started again like the other guest launchers.
    if (!authority)
    {
        static int intOffset = -2;
        static DWORD intLastStart = 0;
        // a new game: MAIN starts INT a moment after the connection, and ours made a second one (PROLOG1 loaded twice,
        // its baggage belt undrawn, testeo 17-11). Only an INT seen and gone, or never there after a minute, is started
        static bool intSeen = false;
        static DWORD sessionStart = 0;
        static unsigned int intSession = 0;
        if (intSession != session)
        {
            intSession = session;
            intOffset = -2;
            intSeen = false;
            sessionStart = GetTickCount();
        }
        const int intro = *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[1792]);
        bool intRuns = false;
        for (auto script = CTheScripts::pActiveScripts; script && !intRuns; script = script->m_pNext)
            intRuns = _strnicmp(script->m_szName, "INT", 8) == 0;
        intSeen |= intRuns;
        if ((intro == 0 || intro == 1) && !intRuns && (intSeen || GetTickCount() - sessionStart >= 60000) &&
            CTheScripts::pIdleScripts && GetTickCount() - intLastStart >= 5000)
        {
            if (intOffset == -2)
            {
                intOffset = -1;
                static const unsigned char signature[] = {0xA4, 0x03, 0x09};
                const unsigned int size = CTheScripts::MainScriptSize;
                int matches = 0;
                if (size >= sizeof(signature) + 8 && size <= 200000)
                    for (unsigned int i = 0; i <= size - sizeof(signature) - 8; ++i)
                        if (memcmp(&CTheScripts::ScriptSpace[i], signature, sizeof(signature)) == 0 &&
                            _strnicmp(reinterpret_cast<const char*>(&CTheScripts::ScriptSpace[i + sizeof(signature)]), "INT", 8) == 0)
                        {
                            intOffset = static_cast<int>(i);
                            ++matches;
                        }
                if (matches != 1)
                    intOffset = -1;
            }
            intLastStart = GetTickCount();
            if (intOffset >= 0)
            {
                CTheScripts::StartNewScript(reinterpret_cast<unsigned char*>(&CTheScripts::ScriptSpace[intOffset]));
                CMissionMirror::LogEventf("guest-launcher start name=INT offset=%d counter=%d", intOffset, intro);
            }
        }
    }

    CNetworkPlayer* host = nullptr;
    for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (networkPlayer && networkPlayer->m_bIsHost)
        {
            host = networkPlayer;
            break;
        }
    }
    if (!host && !authority)
        return;
    auto hostProgress = [&](int chain) {
        return chain == 0 ? host->m_nSweetProgress : static_cast<int>(host->m_anChainProgress[chain]);
    };
    for (auto& launcher : launchers)
    {
        int& counter = *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[launcher.counterOffset]);
        if (authority)
        {
            // Sweet, Ryder, Smoke and Strap run from the intro to their chain's end: one gone mid-chain was lost by a save
            // made before the 14-11 fix (see GenericSave_OwnProgress). Never raised: his progress is his own.
            if (launcher.chain > 3 || counter <= 0 || counter >= launcher.endCounter)
                continue;
        }
        else
        {
        const int hostCounter = hostProgress(launcher.chain);
        const int prerequisite = hostProgress(launcher.prerequisiteChain);
        // the host's own launcher alive says it all; the counters only guess (a save had Ryder with Sweet at 2, 12.11)
        const bool hostRuns = (host->m_nStoryLaunchers & (1u << launcher.chain)) != 0;
        if (hostCounter < 0 || hostCounter == CMissionMode::CHAIN_UNKNOWN || hostCounter > launcher.endCounter ||
            (!hostRuns && (prerequisite < 0 || prerequisite == CMissionMode::CHAIN_UNKNOWN ||
                prerequisite < launcher.prerequisiteCounter)))
            continue;
        // Preserve SWEET's existing completed-chain behavior.
        if (launcher.chain == 0 && hostCounter >= launcher.endCounter)
            continue;
        if (counter < 0 || counter > launcher.endCounter)
            continue;
        if (counter < hostCounter)
        {
            CMissionMirror::LogEventf("guest-launcher counter name=%s from=%d to=%d", launcher.name, counter, hostCounter);
            if (launcher.ownCounter < 0 || counter != launcher.copiedCounter)
                launcher.ownCounter = counter;
            counter = launcher.copiedCounter = hostCounter;
            if (launcher.chain == 0)
                CMissionMode::UpdateSweetProgress(counter);
        }
        // CAT offers Catalina's dates again (mission 40) only once King in Exile (CATCUT) set $flag_trailor_cutscene
        // (8652): past her second mission the host has seen it. Only on a counter taken from the host.
        int& trailerCut = *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[8652]);
        if (launcher.chain == 7 && counter >= 2 && counter == launcher.copiedCounter && trailerCut == 0)
        {
            trailerCut = 1;
            CMissionMirror::LogEventf("guest-launcher trailer-cut name=%s counter=%d", launcher.name, counter);
        }
        if (counter >= launcher.endCounter)
            continue;
        }
        bool exists = false;
        for (auto script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
        {
            if (_strnicmp(script->m_szName, launcher.name, 8) == 0)
            {
                exists = true;
                break;
            }
        }
        if (exists)
            continue;
        if (!launcher.searched)
        {
            launcher.searched = true;
            // Original main.scm: 03A4, short string (09), 8 bytes.
            static const unsigned char signature[] = {0xA4, 0x03, 0x09};
            const unsigned int size = CTheScripts::MainScriptSize;
            int matches = 0;
            // ScriptSpace is a pointer in the SDK; its backing array has 200000 bytes.
            if (size >= sizeof(signature) + 8 && size <= 200000)
            {
                for (unsigned int i = 0; i <= size - sizeof(signature) - 8; ++i)
                {
                    if (memcmp(&CTheScripts::ScriptSpace[i], signature, sizeof(signature)) == 0 &&
                        _strnicmp(reinterpret_cast<const char*>(&CTheScripts::ScriptSpace[i + sizeof(signature)]), launcher.name, 8) == 0)
                    {
                        launcher.offset = static_cast<int>(i);
                        ++matches;
                    }
                }
            }
            if (matches != 1)
            {
                launcher.offset = -1;
                CMissionMirror::LogEventf("guest-launcher label-unavailable name=%s matches=%d", launcher.name, matches);
            }
            // CAT's label is 7 bytes before its SCRIPT_NAME: $load_and_launch_catalina_mission (2872) = -1. Skipping it
            // the switch sees 0 and launches Local Liquor Store at once.
            static const unsigned char catPrelude[] = {0x04, 0x00, 0x02, 0x38, 0x0B, 0x04, 0xFF};
            if (launcher.offset >= 7 && !_strnicmp(launcher.name, "CAT", 8) &&
                memcmp(&CTheScripts::ScriptSpace[launcher.offset - 7], catPrelude, sizeof(catPrelude)) == 0)
                launcher.offset -= 7;
        }
        // Loaded saves may lack the thread; new threads get their name on their first command.
        if (launcher.offset < 0 || !CTheScripts::pIdleScripts || GetTickCount() - launcher.lastStart < 5000)
            continue;
        // StartNewScript dereferences pIdleScripts (gta-reversed TheScripts.cpp, 0x464C20).
        launcher.started = CTheScripts::StartNewScript(reinterpret_cast<unsigned char*>(&CTheScripts::ScriptSpace[launcher.offset]));
        launcher.lastStart = GetTickCount();
        CMissionMirror::LogEventf("guest-launcher start name=%s offset=%d counter=%d authority=%d", launcher.name, launcher.offset, counter,
            authority ? 1 : 0);
    }
}

