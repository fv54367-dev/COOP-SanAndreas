#pragma once

#include <cstdint>
#include <CVector.h>

namespace Packets::Scripts
{
class CameraMirror;
}

// Mirrors the host's script camera (fixed camera / flyby / new scriptable cameras) on the clients while a synced
// mission scene runs, so everybody sees the same shot (position, direction and FOV).
class CCameraMirror
{
public:
    // called from `gameProcessEvent` every frame
    static void Process();
    static void HandlePacket(Packets::Scripts::CameraMirror* pPacket);
    // after every command of every script (COpCodeSync's BuildAndSendOpcode): notes the scene the synced mission script
    // itself directs, so a garage, a shop or an entry/exit that also moves the camera is not mirrored
    static void OnCommandExecuted(CRunningScript* script, uint16_t opcode);
    // fade guard (T89): a DO_FADE replayed from the host (or a guest minigame that ended) may leave this client black, because
    // the fade-in can come from a script that is not synced (the host's DANCE thread)
    static void OnMirroredFade();
    static void ArmFadeGuard();
    // true if it faded back in; `why` goes to the log
    static bool RestoreFade(const char* why);
    // the bars of a scene the synced mission script directs (authority) or the authority's bars mirrored here (guest);
    // not a real cutscene, a garage or a shop
    static bool IsMissionSceneWide();

private:
    static void ProcessFadeGuard();
    static bool IsFadeStuck();
    static bool IsScriptCameraActive(bool bCountWide);
    static void SendHost();
    static void UpdateClientWide();
    static void UpdateClient();
    static void ApplyCamera();
    static void RestoreCamera();
    static void ForgetCamera();

    static inline bool ms_bFadeArmed = false;
    static inline uint32_t ms_nBlackSince = 0;

    // host
    static inline bool ms_bScriptWide = false;    // the synced mission script switched the bars on
    static inline bool ms_bScriptCamera = false;  // the synced mission script took the camera (fixed / attached / vector)
    static inline bool ms_bSentActive = false;
    static inline bool ms_bSentWide = false;
    static inline uint32_t ms_nLastWideSendTick = 0;
    static inline uint32_t ms_nLastSendTick = 0;

    // client: last snapshot received from the host
    static inline bool ms_bActive = false;
    static inline bool ms_bHasSnapshot = false;
    static inline uint32_t ms_nLastPacketTick = 0;
    static inline CVector ms_vecTargetSource{};
    static inline CVector ms_vecTargetFront{};
    static inline CVector ms_vecTargetUp{};
    static inline float ms_fTargetFov = 70.0f;

    // client: the authority's widescreen bars (kept in sync even when the camera itself is not mirrored)
    static inline bool ms_bHostWide = false;
    static inline bool ms_bWideForced = false;
    static inline uint32_t ms_nLastWideTick = 0;

    // client: camera actually applied, smoothed towards the last snapshot
    static inline bool ms_bCameraTaken = false;
    static inline bool ms_bHasSmoothed = false;
    static inline uint32_t ms_nLastFrameTick = 0;
    static inline CVector ms_vecSource{};
    static inline CVector ms_vecFront{};
    static inline CVector ms_vecUp{};
    static inline float ms_fFov = 70.0f;
    static inline float ms_fLastAppliedFov = -1.0f;
};
