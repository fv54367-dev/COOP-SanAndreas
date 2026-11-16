#pragma once

#include <string>

// How the local player plays the missions (F1 menu): together with the others (coop, default) or alone (individual).
// Individual: a story marker only asks for him, he is not invited to coop side missions and he ignores the host's launch.
// The others learn it through PLAYER_MISSION_MODE (CNetworkPlayer::m_bIndividualMode). docs/MISIONES_MODO.md
class CMissionMode
{
public:
    // the story chains of main.scm (Sweet first), see s_aChains in CMissionMode.cpp
    static constexpr int STORY_CHAIN_COUNT = Packets::Scripts::PlayerMissionMode::STORY_CHAIN_COUNT;
    static constexpr uint8_t CHAIN_UNKNOWN = Packets::Scripts::PlayerMissionMode::CHAIN_UNKNOWN;

    static inline bool ms_bIndividual = false;
    static inline int ms_nLocalSweetProgress = -1;
    static inline int ms_nLocalRunningMission = -1;
    static inline uint32_t ms_nLastMismatchTick = 0;
    static inline uint32_t ms_nLastSendTick = 0;
    static inline playerid_t ms_equalizeTarget{};
    static inline uint32_t ms_nEqualizeTick = 0;
    // the chain counters last sent to the others
    static inline uint8_t ms_anLocalChainProgress[STORY_CHAIN_COUNT]{};

    static bool IsIndividual() { return ms_bIndividual; }
    // true when the local player plays alone: he chose it, or every other player did
    static bool IsPlayingAlone();
    // somebody else plays the missions in coop mode
    static bool AnyOtherCoop();

    static void SetIndividual(bool bIndividual);
    // CCore::Init: reads the Sweet progress by itself and tells the others every few seconds (a packet sent before the
    // connection was ready, or lost, would leave the others without it until the next change)
    static void Init();
    // the story mission Sweet's marker starts for a $flag_sweet_mission_counter value ("otra mision" past Cesar Vialpando)
    static std::string GetSweetMissionName(int counter);
    // the local counter of every story chain (CHAIN_UNKNOWN when out of 0..254)
    static void ReadLocalChains(uint8_t* pOut);
    // the story launchers alive in this game, bit = chain (SWEET 0, RYDER 1, SMOKE 2, STRAP 3): a guest starts the ones
    // the host has (CMissionSyncRules), whatever the counters say (a save may have Ryder with Sweet at 2, 12.11)
    static uint32_t ReadLocalLaunchers();
    static inline uint32_t ms_nLocalLaunchers = 0;
    // short name of a chain ("Sweet", "Ryder", ...)
    static const char* GetChainName(int chain);
    // the mission a chain starts next for a counter value, "terminada" once over, "?" when unknown
    static std::string GetChainStateName(int chain, int value);
    static bool IsChainFinished(int chain, int value);
    // chat command /mision: the next mission of every story chain for the local player, and where the others differ
    static void ShowNextMissions();
    // chat command /igualar <name>: every story chain counter he told becomes the local one (asks to repeat it)
    static void EqualizeWith(const std::string& playerName);
    // the local $flag_sweet_mission_counter: the others learn it when it changes (read where Sweet's marker is asked)
    static void UpdateSweetProgress(int counter);
    // a coop player (not the local one) whose Sweet progress differs from `localCounter`: false when all agree or are unknown
    static bool FindSweetMismatch(int localCounter, std::string& outName, int& outTheirCounter);
    // 5 seconds message + chat line: the coop mission can't start because the progress is not the same (every 5 s)
    static void ShowProgressMismatch(const std::string& otherName, int theirCounter, int localCounter);
    // the story chain a launcher script starts the missions of (-1: none, like INT)
    static int LauncherChain(const char* szLauncher);
    // every story launcher: a coop player whose own progress in that chain is not the local player's
    static bool FindChainMismatch(int chain, std::string& outName, int& outTheirs, int& outMine);
    // in the help box (every 5 s while in the marker): the mission can't start, the progress differs
    static void ShowChainMismatch(int chain, const std::string& otherName, int theirs, int mine);
    // the marker's mission is only played alone (Cesar Vialpando) and he is in coop mode: tell him to switch with F1
    static void ShowIndividualOnlyHint();
    // after connecting and when somebody joins: the others (re)learn the local mode
    static void SendLocal();
};
