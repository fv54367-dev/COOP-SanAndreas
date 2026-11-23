#include "CHighStakesRace.h"
#include "stdafx.h"
#include "CCoopPool.h"
#include "CCoopLowrider.h"
#include "CCameraMirror.h"
#include <COpCodeSync.h>
#include <CCoopMission.h>
#include <CMissionMirror.h>
#include <CMissionMode.h>
#include <CEntryExitManager.h>
#include "CNetworkPlayerManager.h"

static constexpr uint32_t CAMERA_MIRROR_SEND_INTERVAL = 33;   // ~30 Hz
static constexpr uint32_t CAMERA_MIRROR_PACKET_TIMEOUT = 500;  // no packets for this long: give the camera back

// camera modes the SCM uses to direct a scene (MODE_FIXED is what SET_FIXED_CAMERA_POSITION / POINT_CAMERA_AT_POINT use)
static bool IsScriptControlledCamMode(eCamMode mode)
{
    switch (mode)
    {
    case MODE_FIXED:
    case MODE_FLYBY:
    case MODE_CAM_ON_A_STRING:
    case MODE_ATTACHCAM:
    case MODE_CAMERA:
    case MODE_SPECIAL_FIXED_FOR_SYPHON:
    case MODE_LIGHTHOUSE:
        return true;
    default:
        return false;
    }
}

// entering or leaving an interior (the enex fade) or standing inside one: the camera there is the player's own, a mission's
// leftover fixed camera / lost player control must not be taken for a scene (Home Invasion: the guests copied the host's
// camera when he went into the house). Real scenes keep the widescreen bars (or a cutscene) and are not affected.
static bool IsInteriorMoment()
{
    return CConfigLoader::ms_bCameraMirrorInteriorGuard && (CGame::currArea != 0 || CEntryExitManager::ms_exitEnterState != 0);
}

// the authority stands in another interior (or outside while this guest is in one): his camera looks at a place this
// guest does not have loaded. Home Invasion: the host broke into the house (bars on, fixed camera) while the guest was
// still outside and got the host's camera and bars (testeo 15-11); the guest walks in by the door with his own camera.
static bool IsAuthorityInOtherArea()
{
    if (!CConfigLoader::ms_bCameraMirrorInteriorGuard)
        return false;
    CPlayerPed* me = FindPlayerPed(0);
    if (!me)
        return false;
    CNetworkPlayer* authority = nullptr;
    if (CCoopMission::IsRunning())
        authority = CNetworkPlayerManager::GetPlayer(CCoopMission::GetOwnerId());
    else
        for (auto player : CNetworkPlayerManager::m_pPlayers)
            if (player && player->m_bIsHost) { authority = player; break; }
    return authority && authority->m_pPed && authority->m_pPed->m_nAreaCode != me->m_nAreaCode;
}

bool CCameraMirror::IsScriptCameraActive(bool bCountWide)
{
    // widescreen bars = the SCM is running a scene
    if (bCountWide && TheCamera.m_bWideScreenOn)
        return true;

    // a script camera alone is not enough (missions also use fixed cameras / camera vectors during playable moments):
    // the host must also have lost control of his player (SET_PLAYER_CONTROL off, or any other DisablePlayerControls)
    CPad* pPad = CPad::GetPad(0);
    if (pPad == nullptr || pPad->DisablePlayerControls == 0)
        return false;

    if (IsScriptControlledCamMode(TheCamera.m_aCams[TheCamera.m_nActiveCam].m_nMode))
        return true;

    // camera_set_vector_move / camera_set_vector_track (the new scriptable cameras)
    return TheCamera.VectorMoveRunning() || TheCamera.VectorTrackRunning() || TheCamera.m_bCameraPersistPosition ||
           TheCamera.m_bCameraPersistTrack;
}

void CCameraMirror::OnCommandExecuted(CRunningScript* script, uint16_t opcode)
{
    // only the synced scripts (the coop mission and what it runs); a shop is another, unsynced script
    if (!COpCodeSync::IsScriptSynced(script))
        return;

    switch (opcode & 0x7FFF)
    {
    case COMMAND_SWITCH_WIDESCREEN:
        ms_bScriptWide = ScriptParams[0] != 0;
        break;
    case COMMAND_SET_FIXED_CAMERA_POSITION:
    case COMMAND_POINT_CAMERA_AT_POINT:
    case COMMAND_POINT_CAMERA_AT_CAR:
    case COMMAND_POINT_CAMERA_AT_CHAR:
    case COMMAND_CAMERA_SET_VECTOR_MOVE:
    case COMMAND_CAMERA_SET_VECTOR_TRACK:
    case COMMAND_CAMERA_PERSIST_TRACK:
    case COMMAND_CAMERA_PERSIST_POS:
    case COMMAND_ATTACH_CAMERA_TO_VEHICLE:
    case COMMAND_ATTACH_CAMERA_TO_VEHICLE_LOOK_AT_VEHICLE:
    case COMMAND_ATTACH_CAMERA_TO_VEHICLE_LOOK_AT_CHAR:
    case COMMAND_ATTACH_CAMERA_TO_CHAR:
    case COMMAND_ATTACH_CAMERA_TO_CHAR_LOOK_AT_VEHICLE:
    case COMMAND_ATTACH_CAMERA_TO_CHAR_LOOK_AT_CHAR:
    case COMMAND_ATTACH_CAMERA_TO_OBJECT:
    case COMMAND_ATTACH_CAMERA_TO_OBJECT_LOOK_AT_VEHICLE:
    case COMMAND_ATTACH_CAMERA_TO_OBJECT_LOOK_AT_CHAR:
    case COMMAND_ATTACH_CAMERA_TO_OBJECT_LOOK_AT_OBJECT:
    case COMMAND_ATTACH_CAMERA_TO_CHAR_LOOK_AT_OBJECT:
    case COMMAND_ATTACH_CAMERA_TO_VEHICLE_LOOK_AT_OBJECT:
        ms_bScriptCamera = true;
        break;
    case COMMAND_RESTORE_CAMERA:
    case COMMAND_RESTORE_CAMERA_JUMPCUT:
    case COMMAND_SET_CAMERA_BEHIND_PLAYER:
        ms_bScriptCamera = false;
        break;
    }
}

void CCameraMirror::OnMirroredFade()
{
    if (CConfigLoader::ms_bFadeGuard)
        ms_bFadeArmed = true;
}

void CCameraMirror::ArmFadeGuard()
{
    if (CConfigLoader::ms_bFadeGuard)
        ms_bFadeArmed = true;
}

// black and steady (CDraw::FadeValue is the overlay alpha, 255 = black; gta-reversed CCamera::ProcessFade) with nothing of
// ours that explains it: no cutscene, menu, own minigame, local mission or death/arrest screen
bool CCameraMirror::IsFadeStuck()
{
    if (CDraw::FadeValue != 255 || TheCamera.m_bFading)
        return false;
    if (CCutsceneMgr::ms_running || FrontEndMenuManager.m_bMenuActive || CCoopLowrider::OwnContest())
        return false;
    if (COpCodeSync::IsLocalMissionRunning() && !COpCodeSync::IsSyncedMissionRunning())
        return false;
    auto* player = FindPlayerPed(0);
    return player && player->m_fHealth > 0.0f && CWorld::Players[0].m_nPlayerState == PLAYERSTATE_PLAYING;
}

bool CCameraMirror::RestoreFade(const char* why)
{
    if (!ms_bFadeArmed || !IsFadeStuck())
        return false;
    // CCamera::Fade (gta-reversed 0x50AC20): direction 1 = FADE_OUT = the overlay goes away
    TheCamera.Fade(0.5f, 1);
    ms_bFadeArmed = false;
    ms_nBlackSince = 0;
    CMissionMirror::LogEventf("fade-stuck-restore why=%s", why);
    return true;
}

void CCameraMirror::ProcessFadeGuard()
{
    if (!ms_bFadeArmed)
        return;
    if (!CConfigLoader::ms_bFadeGuard || CCoopMission::IsScriptAuthority())
    {
        ms_bFadeArmed = false;
        ms_nBlackSince = 0;
        return;
    }
    // the screen is clear again: nothing left to watch
    if (CDraw::FadeValue == 0 && !TheCamera.m_bFading)
    {
        ms_bFadeArmed = false;
        ms_nBlackSince = 0;
        return;
    }
    if (!IsFadeStuck())
    {
        ms_nBlackSince = 0;
        return;
    }
    const uint32_t now = GetTickCount();
    if (!ms_nBlackSince)
        ms_nBlackSince = now;
    else if (now - ms_nBlackSince >= 4000)
        RestoreFade("watchdog");
}

void CCameraMirror::Process()
{
    ProcessFadeGuard();
    ProcessStuckScriptCamera();
    if (CHighStakesRace::OwnMarkers()) {
        if (ms_bCameraTaken) RestoreCamera();
        ms_bActive = ms_bHostWide = ms_bWideForced = false;
        return;
    }
    if (!CConfigLoader::ms_bCameraMirror)
    {
        if (ms_bCameraTaken)
            RestoreCamera();
        return;
    }

    if (CCoopMission::IsScriptAuthority())
    {
        // becoming host in the middle of a scene: stop mirroring somebody else's camera
        if (ms_bCameraTaken)
            RestoreCamera();
        ms_bWideForced = false;
        ms_bHostWide = false;

        SendHost();
        return;
    }

    UpdateClientWide();
    UpdateClient();
}

bool CCameraMirror::IsMissionSceneWide()
{
    if (!TheCamera.m_bWideScreenOn || CCutsceneMgr::ms_running)
        return false;
    if (CCoopMission::IsScriptAuthority())
        return ms_bScriptWide && COpCodeSync::IsSyncedMissionRunning() && CMissionMirror::IsHostMissionMirrored();
    return ms_bWideForced && ms_bHostWide && GetTickCount() - ms_nLastWideTick <= 3000;
}

void CCameraMirror::SendHost()
{
    // a scene = a mirrored mission (not a shop or a gym that also moves the camera) + (widescreen, or no player control +
    // script camera); see IsScriptCameraActive
    const bool bMissionScene = COpCodeSync::IsSyncedMissionRunning() && CMissionMirror::IsHostMissionMirrored();
    if (!bMissionScene)
    {
        ms_bScriptWide = false;
        ms_bScriptCamera = false;
    }
    // only what the mission script set: a garage, a shop script or an entry/exit also puts bars or a fixed camera on the
    // host, and that is his own moment (the guests kept getting his camera there)
    const bool bWide = bMissionScene && ms_bScriptWide && TheCamera.m_bWideScreenOn;
    bool active = bMissionScene && !CCutsceneMgr::ms_running && (bWide || (ms_bScriptCamera && !IsInteriorMoment() && IsScriptCameraActive(false)));

    if (!active)
    {
        const uint32_t now = CConfigLoader::GetSyncTime();

        // one last packet so the clients release the camera; it carries the bars too
        const bool bSendRelease = ms_bSentActive;
        // the bars alone (a real cutscene has them while the camera is not mirrored): on every change and once a second
        const bool bSendWide = bWide != ms_bSentWide || (bWide && now - ms_nLastWideSendTick > 1000);

        if (!bSendRelease && !bSendWide)
            return;

        Packets::Scripts::CameraMirror packet{};
        packet.active = false;
        packet.wide = bWide;
        GetPacketFactory().Send(packet);
        ms_bSentActive = false;
        ms_bSentWide = bWide;
        ms_nLastWideSendTick = now;
        return;
    }

    uint32_t tickCount = CConfigLoader::GetSyncTime();
    if (tickCount - ms_nLastSendTick < CAMERA_MIRROR_SEND_INTERVAL)
        return;

    ms_nLastSendTick = tickCount;

    CCam& cam = TheCamera.m_aCams[TheCamera.m_nActiveCam];

    Packets::Scripts::CameraMirror packet{};
    packet.active = true;
    packet.source = cam.m_vecSource;
    packet.front = cam.m_vecFront;
    packet.up = cam.m_vecUp;
    packet.fov = cam.m_fFOV;
    packet.wide = bWide;
    GetPacketFactory().Send(packet);

    ms_bSentActive = true;
    ms_bSentWide = bWide;
    ms_nLastWideSendTick = tickCount;
}

void CCameraMirror::HandlePacket(Packets::Scripts::CameraMirror* pPacket)
{
    if (CHighStakesRace::OwnMarkers()) return;
    if (CCoopMission::IsScriptAuthority())
        return;

    ms_bHostWide = pPacket->wide;
    ms_nLastWideTick = GetTickCount();

    if (!pPacket->active)
    {
        ms_bActive = false;
        return;
    }

    ms_vecTargetSource = pPacket->source;
    ms_vecTargetFront = pPacket->front;
    ms_vecTargetUp = pPacket->up;
    ms_fTargetFov = std::clamp(pPacket->fov, 1.0f, 180.0f);
    ms_bActive = true;
    ms_bHasSnapshot = true;
    ms_nLastPacketTick = GetTickCount();
}

// the bars of the authority's mission scene are on here too, whatever turned them off (a scene that was not replayed, a
// cutscene of this game that ended first...): Drive-Thru / Nines And AK's showed the guest the models Rockstar hides
void CCameraMirror::UpdateClientWide()
{
    const uint32_t now = GetTickCount();
    // an individual mission of this game decides its own bars; the authority repeats its state every second while on
    const bool bLocalOnly = COpCodeSync::IsLocalMissionRunning() && !COpCodeSync::IsSyncedMissionRunning();
    const bool bHostWide = ms_bHostWide && now - ms_nLastWideTick <= 3000 && !bLocalOnly && !IsAuthorityInOtherArea();

    if (bHostWide)
    {
        if (!TheCamera.m_bWideScreenOn)
            TheCamera.SetWideScreenOn();
        ms_bWideForced = true;
    }
    else if (ms_bWideForced)
    {
        ms_bWideForced = false;
        ms_bHostWide = false;
        if (!CCutsceneMgr::ms_running)
            TheCamera.SetWideScreenOff();
    }
}

void CCameraMirror::UpdateClient()
{
    if (CCoopLowrider::OwnContest() || CCoopPool::OwnView())
    {
        ms_bCameraTaken = false;
        ms_bHasSmoothed = false;
        return;
    }
    uint32_t now = GetTickCount();

    bool fresh = ms_bActive && ms_bHasSnapshot && ms_nLastPacketTick != 0 &&
                 now - ms_nLastPacketTick <= CAMERA_MIRROR_PACKET_TIMEOUT;
    // every client runs its own copy of the synced mission script, so `IsLocalMissionRunning` alone would always be
    // true here: only an individual mission (a mission script that is not the synced one) must keep the camera
    bool localOnlyMission = COpCodeSync::IsLocalMissionRunning() && !COpCodeSync::IsSyncedMissionRunning();
    // a guest in an interior (or crossing a door) keeps his own camera unless the host's scene has the bars on
    // and never while the authority is in another area: not even his bars (his interior is not loaded here)
    bool blocked = localOnlyMission || CCutsceneMgr::ms_running || (!ms_bHostWide && IsInteriorMoment()) ||
                   IsAuthorityInOtherArea();

    if (!fresh || blocked)
    {
        if (ms_bCameraTaken)
        {
            // a real cutscene took the camera over (spline): restoring it here would throw the scene's cameras away and the
            // guest would watch the actors from behind his own player with the bars on (Drive-Thru, 2026-10-20)
            if (CCutsceneMgr::ms_running)
                ForgetCamera();
            else
                RestoreCamera();
        }

        if (!fresh)
        {
            ms_bActive = false;
            ms_bHasSnapshot = false;
        }
        return;
    }

    uint32_t delta = ms_nLastFrameTick != 0 ? now - ms_nLastFrameTick : 16;
    ms_nLastFrameTick = now;
    if (delta > 100)
        delta = 100;

    if (!ms_bHasSmoothed)
    {
        // first snapshot: start exactly there instead of flying in from the gameplay camera
        ms_vecSource = ms_vecTargetSource;
        ms_vecFront = ms_vecTargetFront;
        ms_vecUp = ms_vecTargetUp;
        ms_fFov = ms_fTargetFov;
        ms_bHasSmoothed = true;
        ms_fLastAppliedFov = -1.0f;
    }
    else
    {
        float distance = (ms_vecTargetSource - ms_vecSource).Magnitude();
        float dot = ms_vecFront.x * ms_vecTargetFront.x + ms_vecFront.y * ms_vecTargetFront.y +
                    ms_vecFront.z * ms_vecTargetFront.z;

        // a long jump or a big direction change is a camera cut, not a movement to smooth
        bool cut = distance > 10.0f || dot < 0.5f;  // 0.5 = 60 degrees

        float alpha = cut ? 1.0f : (1.0f - expf(-(float)delta / 70.0f));

        ms_vecSource += (ms_vecTargetSource - ms_vecSource) * alpha;
        ms_vecFront += (ms_vecTargetFront - ms_vecFront) * alpha;
        ms_vecUp += (ms_vecTargetUp - ms_vecUp) * alpha;
        ms_fFov += (ms_fTargetFov - ms_fFov) * alpha;

        float length = ms_vecFront.Magnitude();
        if (length > 0.0001f)
            ms_vecFront *= 1.0f / length;

        length = ms_vecUp.Magnitude();
        if (length > 0.0001f)
            ms_vecUp *= 1.0f / length;
    }

    ApplyCamera();
    ms_bCameraTaken = true;
}

void CCameraMirror::ApplyCamera()
{
    Command<Commands::SET_FIXED_CAMERA_POSITION>(ms_vecSource.x, ms_vecSource.y, ms_vecSource.z, 0.0f, 0.0f, 0.0f);

    CVector target = ms_vecSource + ms_vecFront;
    Command<Commands::POINT_CAMERA_AT_POINT>(target.x, target.y, target.z, 2);  // 2 = jump cut

    // the active cam is the one being rendered, keep its FOV equal to the host's (zoom in / zoom out)
    TheCamera.m_aCams[TheCamera.m_nActiveCam].m_fFOV = ms_fFov;

    if (ms_fLastAppliedFov < 0.0f || fabsf(ms_fFov - ms_fLastAppliedFov) > 0.05f)
    {
        Command<Commands::CAMERA_SET_LERP_FOV>(ms_fFov, ms_fFov, 0, 0);
        ms_fLastAppliedFov = ms_fFov;
    }
}

void CCameraMirror::ForgetCamera()
{
    ms_bCameraTaken = false;
    ms_bHasSmoothed = false;
    ms_bActive = false;
    ms_bHasSnapshot = false;
    ms_nLastFrameTick = 0;
    ms_fLastAppliedFov = -1.0f;
}

void CCameraMirror::RestoreCamera()
{
    Command<Commands::RESTORE_CAMERA_JUMPCUT>();
    Command<Commands::SET_CAMERA_BEHIND_PLAYER>();

    ms_bCameraTaken = false;
    ms_bHasSmoothed = false;
    ms_bActive = false;
    ms_bHasSnapshot = false;
    ms_nLastFrameTick = 0;
    ms_fLastAppliedFov = -1.0f;
}

// camera-guest-driver (Catalyst, 20-11): this game replays the authority's camera opcodes (0x015F / 0x0160 / 0x0373 /
// 0x02EB are in the synced table, COpCodeSync.cpp), so a scene camera of the host can stay latched on a guest that is
// playing. The owner's report: driving Ryder's Picador he kept the host's camera until he got into a car by hand. Two
// guards: SkipRemoteCamera drops those opcodes while this client drives with control, and ProcessStuckScriptCamera
// gives back a script camera that is still there with nobody owning the moment.
bool CCameraMirror::SkipRemoteCamera(uint16_t opcode)
{
    if (!CConfigLoader::ms_bCameraGuestDriver || CCoopMission::IsScriptAuthority())
        return false;

    switch (opcode & 0x7FFF)
    {
    case 0x0158:  // point camera at char / at car
    case 0x0159:
    case 0x015F:
    case 0x0160:
    case COMMAND_SWITCH_WIDESCREEN:
    case 0x02EB:  // restore_camera_jumpcut
    case 0x0373:  // set_camera_behind_player
        break;
    default:
        return false;
    }

    CPlayerPed* me = FindPlayerPed(0);
    if (me == nullptr || !me->m_nPedFlags.bInVehicle || me->m_pVehicle == nullptr || me->m_pVehicle->m_pDriver != me)
        return false;
    CPad* pad = CPad::GetPad(0);
    if (pad == nullptr || pad->DisablePlayerControls != 0)
        return false;
    return !CCutsceneMgr::ms_running && !ms_bActive && !ms_bHostWide;
}

void CCameraMirror::ProcessStuckScriptCamera()
{
    if (!CConfigLoader::ms_bCameraGuestDriver || CCoopMission::IsScriptAuthority())
    {
        ms_nStuckCamSince = 0;
        return;
    }

    CPlayerPed* me = FindPlayerPed(0);
    if (me == nullptr || !me->m_nPedFlags.bInVehicle || me->m_pVehicle == nullptr || me->m_pVehicle->m_pDriver != me ||
        ms_bActive || ms_bHostWide || TheCamera.m_bWideScreenOn || CCutsceneMgr::ms_running || CCoopPool::OwnView() ||
        CCoopLowrider::OwnContest() || CHighStakesRace::OwnMarkers())
    {
        ms_nStuckCamSince = 0;
        return;
    }

    CPad* pad = CPad::GetPad(0);
    if (pad == nullptr || pad->DisablePlayerControls != 0)
    {
        ms_nStuckCamSince = 0;
        return;
    }

    const eCamMode mode = TheCamera.m_aCams[TheCamera.m_nActiveCam].m_nMode;
    // ONLY MODE_FIXED is a stuck script camera (it is what SET_FIXED_CAMERA_POSITION / POINT_CAMERA_AT_POINT set).
    // MODE_CAM_ON_A_STRING (0x12) is the NORMAL car camera, in IsScriptControlledCamMode only for scripted shots, and the
    // persist / vector flags stay set after any mission camera: with any of those this watchdog restored the camera every
    // 3 s while the guest drove (owner's report 21-11, "la cámara se me resetea cada unos segundos", log mode=18 car=600).
    if (mode != MODE_FIXED)
    {
        ms_nStuckCamSince = 0;
        return;
    }

    // dwell first (a real scene sets its camera for more than one frame), then one restore per episode
    const uint32_t now = GetTickCount();
    if (ms_nStuckCamSince == 0)
    {
        ms_nStuckCamSince = now;
        return;
    }
    if (now - ms_nStuckCamSince < 500 || now - ms_nLastStuckRestore < 10000)
        return;

    ms_nLastStuckRestore = now;
    ms_nStuckCamSince = 0;
    RestoreCamera();
    CMissionMirror::LogEventf("catalyst-camera restuck mode=%d car=%d", (int)mode, me->m_pVehicle->m_nModelIndex);
}

PACKET_HANDLER(ePacketType::CAMERA_MIRROR, Packets::Scripts::CameraMirror* pCameraMirror)
{
    // F1 individual: the authority's scenes don't reach this game (the server doesn't relay them either)
    if (CMissionMode::IsIndividual() && !CCoopMission::IsScriptAuthority())
        return;
    CCameraMirror::HandlePacket(pCameraMirror);
}
