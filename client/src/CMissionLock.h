#pragma once

// One player per mission id (docs/REQUISITOS.md 1): the server keeps who is playing which mission; a player that starts a
// mission somebody else is playing gets it ended right away with a message. Other missions are not affected.
class CMissionLock
{
public:
    static constexpr int MAX_MISSIONS = 201;
    static bool IsIndependentIntro(int missionId);
    static bool IsLocalIntroRunning();

    // a mission script was just launched by load_and_launch_mission; true = it was ended (somebody else has that mission)
    static bool OnMissionStarted(int missionId, CRunningScript* pScript);
    // the mission this game was playing is over (passed, failed or abandoned)
    static void OnMissionEnded();
    static void HandleState(int missionId, bool locked, playerid_t ownerId);
    // the launcher of the mission (its marker) is about to pass: true = somebody else plays that mission, so it must not
    // start (it would fade the screen and be ended right away). missionId -1: the one a blocked launch of this launcher
    // reported. Shows the message for 5 seconds.
    static bool IsLaunchBlocked(CRunningScript* pLauncher, int missionId);
    // a launch of this launcher was ended because of the lock: its marker keeps being blocked while the lock holds
    static void RememberBlockedLauncher(CRunningScript* pLauncher, int missionId);
    static void Reset();

private:
    static void AbortMission(CRunningScript* pScript);
    static void ShowBlocked(playerid_t ownerId);

    inline static playerid_t ms_owner[MAX_MISSIONS]{};
    inline static int ms_nMyMission = -1;
    inline static CRunningScript* ms_pMyScript = nullptr;
    inline static uint32_t ms_nLastMessageTick = 0;
    inline static char ms_szBlockedLauncher[8]{};
    inline static int ms_nBlockedMission = -1;
};
