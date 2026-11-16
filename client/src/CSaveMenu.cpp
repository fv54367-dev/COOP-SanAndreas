#include "stdafx.h"
#include "CSaveMenu.h"
#include "COpCodeSync.h"
#include "CCoopMission.h"
#include <CPickups.h>

static constexpr int OFFSET_CREATED_SAVE_BLIPS = 3536;
static constexpr int OFFSET_NUMBER_OF_SAVE_ICONS = 3540;
static constexpr int OFFSET_SAVE_PICKUP_0 = 3460;

static int& ScriptVar(int offset)
{
    return *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[offset]);
}

void CSaveMenu::Request()
{
    if (ms_bActive)
        return;

    CPlayerPed* pPlayer = FindPlayerPed(0);
    const bool bPlaying = pPlayer && pPlayer->m_fHealth > 0.0f && CWorld::Players[0].m_nPlayerState == PLAYERSTATE_PLAYING;
    if (!bPlaying)
    {
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} No se puede guardar ahora.");
        return;
    }

    // the same conditions as the pickup: not in a mission, a cutscene or a menu
    if (CTheScripts::OnAMissionFlag != 0 && ScriptVar(CTheScripts::OnAMissionFlag) != 0)
    {
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} No se puede guardar durante una mision.");
        return;
    }

    if (COpCodeSync::IsLocalMissionRunning() || CCutsceneMgr::ms_running || FrontEndMenuManager.m_bMenuActive)
    {
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} No se puede guardar durante una mision, una cinematica o un menu.");
        return;
    }

    // PSAVE1_923 also sets $onmission = 1 here, but the savegame then holds that script waiting in its save loop, which puts the
    // flag back to 0 when the game is loaded. This command has no such script: the flag would be saved as 1 and stay (a
    // savegame made this way left the player "on a mission" for good), so only the control is taken
    Command<Commands::SET_PLAYER_CONTROL>(0, false);
    Command<Commands::ACTIVATE_SAVE_MENU>();

    ms_bActive = true;
    ms_nStartTick = GetTickCount();
}

void CSaveMenu::Finish()
{
    ms_bActive = false;

    // PSAVE1_1003: back to the game
    Command<Commands::DO_FADE>(500, 1);
    Command<Commands::RESTORE_CAMERA_JUMPCUT>();
    Command<Commands::SET_CAMERA_BEHIND_PLAYER>();
    Command<Commands::SET_PLAYER_CONTROL>(0, true);
}

void CSaveMenu::ProcessPickupWatchdog()
{
    const uint32_t now = GetTickCount();
    if (now - ms_nLastWatchdogTick < 1000)
        return;
    ms_nLastWatchdogTick = now;

    // only when the script believes the pickups exist and the game is not in a mission
    const int nIcons = ScriptVar(OFFSET_NUMBER_OF_SAVE_ICONS);
    if (ScriptVar(OFFSET_CREATED_SAVE_BLIPS) != 1 || nIcons < 13 || nIcons > 18 || CTheScripts::OnAMissionFlag == 0 ||
        ScriptVar(CTheScripts::OnAMissionFlag) != 0 || ms_bActive)
    {
        ms_nBadChecks = 0;
        return;
    }

    const int index = CPickups::GetActualPickupIndex(ScriptVar(OFFSET_SAVE_PICKUP_0));
    const bool bAlive = index != -1 && CPickups::aPickUps[index].m_nPickupType != PICKUP_NONE;
    if (bAlive)
    {
        ms_nBadChecks = 0;
        return;
    }

    // a few seconds in a row (a collected pickup is back in a moment): let PSAVE1 create them again
    if (++ms_nBadChecks >= 3)
    {
        ms_nBadChecks = 0;
        ScriptVar(OFFSET_CREATED_SAVE_BLIPS) = 0;
    }
}

// A savegame made by the old /guardar has $onmission = 1 with no mission: the player can't start missions and the save pickups are
// gone for good. $onmission = 1 with no mission script, cutscene, coop mission or host mission behind it for 20 seconds in a row is
// not a real mission: put it back to 0.
void CSaveMenu::ProcessStuckMissionFlag()
{
    const bool bFlagOn = CTheScripts::OnAMissionFlag != 0 && ScriptVar(CTheScripts::OnAMissionFlag) != 0;
    const bool bSomethingRuns = COpCodeSync::IsLocalMissionRunning() || CTheScripts::bAlreadyRunningAMissionScript ||
                                CCutsceneMgr::ms_running || CCoopMission::IsActive() || ms_bHostOnMission || ms_bActive ||
                                FrontEndMenuManager.m_bMenuActive;

    if (!bFlagOn || bSomethingRuns)
    {
        ms_nStuckSince = 0;
        return;
    }

    const uint32_t now = GetTickCount();
    if (ms_nStuckSince == 0)
    {
        ms_nStuckSince = now;
        return;
    }

    if (now - ms_nStuckSince > 20000)
    {
        ms_nStuckSince = 0;
        ScriptVar(CTheScripts::OnAMissionFlag) = 0;
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Estabas marcado 'en mision' sin estar en una (el guardado venia asi): corregido. Guarda de nuevo con /guardar.");
    }
}

void CSaveMenu::Process()
{
    ProcessPickupWatchdog();
    ProcessStuckMissionFlag();

    if (!ms_bActive)
        return;

    // Game.HasSaveGameFinished; a minute of margin for the player to choose the slot, then give up
    if (Command<Commands::HAS_SAVE_GAME_FINISHED>() || GetTickCount() - ms_nStartTick > 10 * 60 * 1000)
        Finish();
}

void CSaveMenu::Init()
{
    Events::gameProcessEvent.after += []
    {
        Process();
    };
}
