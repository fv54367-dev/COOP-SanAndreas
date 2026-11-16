#pragma once

#include <vector>

namespace Packets::Scripts
{
class MinigameVsScore;
}

// VS race (docs/MISIONES_MODO.md): two or more players run the race mission (CPRACE, id 35) at the same time, each in his own
// game. While that lasts the world is empty of NPCs for the local player: no pedestrians, no traffic, no police, and the ones
// that were around are removed. Only the players in the race count, the others (in other missions or free roaming) are not
// asked anything. The mission ids travel in PLAYER_MISSION_MODE (CNetworkPlayer::m_nRunningMission).
class CVsRace
{
public:
    static void Init();
    // the local player runs the race and so does another one
    static bool IsActive();

    // the position HUD of a VS race: the game's one counts the NPC racers, this one only the players. Progress comes from the
    // race script's own variables (lap, checkpoint, distance to the next one) and travels in MINIGAME_VS_SCORE
    static constexpr int PROGRESS_GAME_ID = 200;
    // VS with NPCs: the owner sends the progress of each NPC racer of his race script as game id NPC_GAME_ID + slot
    static constexpr int NPC_GAME_ID = 201, NPC_SLOTS = 16;
    static void OnProgress(const Packets::Scripts::MinigameVsScore& packet);
    static void OnNpcProgress(const Packets::Scripts::MinigameVsScore& packet);
    // drops the race script's own position/time panel (RACETOUR main loop) from this frame's script draws. The game draws
    // every non-empty entry of IntroRectangles/IntroTextLines whatever the counters say (gta-reversed CHud::DrawScriptText
    // 0x58C080, CTheScripts::DrawScriptSpritesAndRectangles): the entries themselves are emptied
    static int DropScriptRacePanel();
    static bool ShouldDrawHud();
    static void DrawHud();
    // vs-race-board: the live standings box of the players (same look as High Stakes Lowrider's), game render
    static void DrawBoard();
    // the race script's own position/time panel goes while the board is on screen (after CTheScripts::Process)
    static void HideNativeHud();

private:
    static void Process();
    static void Enter();
    static void Exit();
    static void Reassert();
    static void HideNpcRacersAndSeparate();
    static void UpdateProgress();
    static void DrawResults(struct ImDrawList* dl, struct ImFont* font, const struct ImVec2& size);

    struct SRacer
    {
        uint32_t id = 0;
        int score = 0;
        uint8_t state = 0;  // 0 racing, 1 finished, 2 left
        uint32_t lastTick = 0;
        uint32_t finishTick = 0;
        int finishSeconds = 0;
    };
    static inline std::vector<SRacer> ms_vRacers;
    struct SNpc
    {
        uint32_t owner = 0;
        int slot = 0;
        int score = 0;
        uint8_t state = 0;
        uint32_t lastTick = 0;
        uint32_t finishTick = 0;
    };
    static inline std::vector<SNpc> ms_vNpcs;
    static void NoteNpc(uint32_t owner, int slot, int score, uint8_t state);
    static bool FindGuestGridSpot(class CVehicle* pMine, int nBehind, class CVector& out);
    static inline bool ms_bHudActive = false;
    static inline int ms_nHudPosition = 1;
    static inline int ms_nHudPlayers = 1;
    static inline int ms_nHudLap = 1;
    static inline int ms_nHudLaps = 1;
    static inline int ms_nHudSeconds = 0;
    static inline int ms_nLocalScore = 0;
    static inline uint8_t ms_nLocalState = 0;
    static inline uint32_t ms_nLocalFinishTick = 0;
    static inline int ms_nLocalFinishSeconds = 0;
    static inline uint32_t ms_nLastSendTick = 0;

    static inline bool ms_bActive = false;
    static inline uint32_t ms_nLastClearTick = 0;
    static inline float ms_fPedDensity = 1.0f;
    static inline float ms_fCarDensity = 1.0f;
    static inline unsigned char ms_bEmergency = 1;
    static inline unsigned int ms_nMaxWanted = 6;
};
