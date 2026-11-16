#pragma once

// /guardar: opens the game's own save menu (the one the save pickups of the safe houses open: Game.ActivateSaveMenu of PSAVE1),
// so the savegame can be written wherever the player is. And a watchdog for those pickups: PSAVE1 creates them once
// ($created_save_blips = 1) and only creates them again when it sees the flag at 0, so pickups destroyed behind its back (it
// happened to the guest after a mission launch was cancelled) were never created again.
// The offsets are the ones of the compiled main.scm (MAIN is frozen, tools\check_main_layout.py keeps it): $created_save_blips
// 3536, $number_of_save_icons 3540, $grove_save_pickup[0] 3460.
class CSaveMenu
{
public:
    static void Init();
    // the chat command
    static void Request();
    static inline bool ms_bActive = false;

private:
    static void Process();
    static void ProcessPickupWatchdog();
    static void ProcessStuckMissionFlag();
    static void Finish();

    static inline uint32_t ms_nStartTick = 0;
    static inline uint32_t ms_nLastWatchdogTick = 0;
    static inline int ms_nBadChecks = 0;
    static inline uint32_t ms_nStuckSince = 0;

public:
    // the host's $onmission as it was last synced (a guest has the flag on while the host plays)
    static inline bool ms_bHostOnMission = false;
};
