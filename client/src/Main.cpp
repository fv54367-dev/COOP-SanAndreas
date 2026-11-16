#include "CSocialActions.h"
#include "CGpsRoute.h"
#include "CGarageSync.h"
#include "CMissionMachines.h"
#include "CMissionObjectMoves.h"
#include "CMissionWarTable.h"
#include "CHighStakesRace.h"
#include "CCutsceneKeepSeat.h"
#include "CVsRace.h"
#include "CVsLobby.h"
#include "CCoopLowrider.h"
#include "CLocalPlayer.h"
#include "CNetworkPlayerManager.h"
#include "CPacketBuffer.h"
#include "CServerTime.h"
#include "stdafx.h"
#include "CActivityChallenge.h"
#include "CCutscenePartners.h"
#include "CCoopPool.h"
#include "CSpawnWatch.h"
#include "CCutsceneMgr.h"
#include "UI/CDXFont.h"
#include "UI/CHudHideAll.h"

#include "network/packets/players.h"
#include "network/packet.h"
#include <CCarEnterExit.h>
#include <CDiscordRPCMgr.h>
#include <CEntryExitManager.h>
#include <CEntryExitMarkerSync.h>
#include <Hooks/MissionAudioHooks.h>
#include <CEntryExitTransitionSync.h>
#include <CFireManager.h>
#include <CGeneral.h>
#include <CNetworkAnimQueue.h>
#include <CNetworkCheckpoint.h>
#include <CTrainSync.h>
#include "CVoice.h"
#include <UI/CNetworkPlayerList.h>
#include <CNetworkStaticBlip.h>
#include <COpCodeSync.h>
#include <CCollectibleSync.h>
#include <CHangWatchdog.h>
#include <CMissionAreaMarkers.h>
#include <CMissionHunters.h>
#include <CCameraMirror.h>
#include <CPatientHandover.h>
#include <CPedPlacement.h>
#include <CTaskSimpleCarSetPedInAsPassenger.h>
#include <CTaskSimpleCarSetPedOut.h>
#include <Hooks/WorldHooks.h>
#include <Hooks/CrashfixHooks.h>
#include <game_sa/CTagManager.h>
#include <semver.h>
#include <CAimSync.h>
#include <CKeySync.h>
#include <CCompatibilityChecker.h>
#include "CTestMode.h"
#include <CWeatherSync.h>
#include <network/packets/scripts.h>
#include <CNetworkEntityBlip.h>
#include <CCutsceneSkipVote.h>
#include <CObjectSync.h>
#include <CMinigameVs.h>
#include <CCoopMission.h>
#include <CCheatSync.h>
#include <CParachuteSync.h>
#include <CPlayerAnimMirror.h>
#include <CPlayerMoney.h>
#include <CVehicleSurf.h>
#include <CSniperLaser.h>
#include <CJackSync.h>
#include "CMissionMirror.h"
#include "CPerfWatch.h"

unsigned int lastOnFootSyncTickRate = 0;
unsigned int lastDriverSyncTickRate = 0;
unsigned int lastIdleVehicleSyncTickRate = 0;
unsigned int lastVehicleDoorsSyncTickRate = 0;
unsigned int lastPassengerSyncTickRate = 0;
unsigned int lastPedSyncTickRate = 0;
unsigned int lastWeatherTimeSyncTickRate = 0;
unsigned int lastClothesSyncTickRate = 0;
unsigned int lastOnscreenTimerSyncTickRate = 0;
unsigned int lastPickupSyncTickRate = 0;
unsigned int lastCollectibleSyncTickRate = 0;
unsigned int lastStreamingSyncTickRate = 0;
unsigned int lastFireSyncTickRate = 0;
unsigned int lastTrainSyncTickRate = 0;
unsigned int lastPlayerAimSyncTickRate = 0;
bool bBeenConnected;
bool lastOnMissionFlag;
uint32_t startTime;

// Open Limit Adjuster moves ScriptSpace and MissionBlock unless MAINsegmentSize/MissionSize are 0 (its default is
// unlimited): every main.scm global read from C++ and the mission detection (m_pBaseIP == MissionBlock) break silently
// (testeo 14-11: no guests in cutscenes, the guest's own markers during the host's mission). The ini is set here, at
// load: if the adjuster already read it, the game start below asks for one restart. Returns false if no ini was found.
static bool ms_bLimitIniChangedAtLoad = false;
static bool FixLimitAdjusterScriptSpace(bool& changed)
{
    changed = false;
    char gameDir[MAX_PATH];
    if (!GetModuleFileNameA(NULL, gameDir, MAX_PATH))
        return false;
    char* lastSlash = strrchr(gameDir, '\\');
    if (!lastSlash)
        return false;
    *lastSlash = '\0';
    bool found = false;
    for (const char* sub : {"", "\\scripts"})
    {
        char ini[MAX_PATH];
        snprintf(ini, MAX_PATH, "%s%s\\III.VC.SA.LimitAdjuster.ini", gameDir, sub);
        if (GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES)
            continue;
        found = true;
        for (const char* key : {"MAINsegmentSize", "MissionSize"})
        {
            char value[32]{};
            GetPrivateProfileStringA("SALIMITS", key, "", value, sizeof(value), ini);
            // "0 ; comment" already counts as 0; a failed write (read-only ini) must not ask for a restart forever
            if ((value[0] != '0' || atoi(value) != 0) && WritePrivateProfileStringA("SALIMITS", key, "0", ini))
                changed = true;
        }
    }
    return found;
}

class CCoopSanAndreas
{
public:
    CCoopSanAndreas()
    {
        FixLimitAdjusterScriptSpace(ms_bLimitIniChangedAtLoad);

        // a game command wrote past plugin-sdk's fake script (it zeroed CNetwork::m_bAuthenticated, 2026-10-25)
        plugin::scripting::ms_pfnGuardBroken = [](unsigned int commandId, unsigned int offset, int where)
        {
            CMissionMirror::LogEventf("script-guard-broken cmd=%04X offset=0x%X when=%s", commandId,
                0xE0 + offset, where == 0 ? "during" : "after");
        };

        CCutscenePartners::Init();
        gameShutdownEvent.before += [] { CCutscenePartners::Reset(); };
        Events::shutdownRwEvent += []
        {
            CVoice::Shutdown();
            CNetworkPlayerList::Shutdown();
        };
        Events::gameProcessEvent.before += []
        {
            CHangWatchdog::Start();

            if (CNetwork::m_bSessionResetPending.exchange(false))
            {
                CNetwork::ResetSessionState();
            }

            if (CNetwork::m_bConnected)
            {
                bBeenConnected = true;

                // the flag only drops together with m_bConnected (or on a new session): alone it was overwritten, and
                // the client stopped sending everything while still receiving (frozen for the others, 2026-10-25)
                if (!CNetwork::m_bAuthenticated && !CNetwork::IsHandshakePending() &&
                    CNetwork::m_nAuthenticatedSessionId != 0 &&
                    CNetwork::m_nAuthenticatedSessionId == CNetwork::m_nSessionId)
                {
                    CNetwork::m_bAuthenticated = true;
                    CMissionMirror::LogEventf("net-auth-restored session=%u", CNetwork::m_nSessionId.load());
                }

                CServerTime::Process();
                CNetwork::ProcessReceive();
                GetPacketBuffer().Process();

                uint32_t currentTime = GetTickCount();
                if (currentTime - startTime >= 1000)
                {
                    CNetwork::ms_nBytesReceivedThisSecond = CNetwork::ms_nBytesReceivedThisSecondCounter.exchange(0);
                    CNetwork::ms_nBytesSentThisSecond = CNetwork::ms_nBytesSentThisSecondCounter.exchange(0);
                    startTime = currentTime;
                }
            }
            else if (bBeenConnected && !CNetwork::m_bConnected)
            {
                bBeenConnected = false;
                CInteriorOwnership::Reset();
                CEntryExitMarkerSync::ms_bDeferredAccess = false;

                // auto-reconnect: the network thread already told the player it is retrying
                if (!CNetwork::m_bAutoReconnecting)
                {
                    CChat::AddMessage("{d6dce8}[Network] Te desconectaste del servidor.");
                }
            }
        };
        Events::gameProcessEvent += []
        {
            CVoice::Process();
            CNetworkAnimQueue::Process();
            CEntryExitTransitionSync::Process();
            CEntryExitMarkerSync::Process();
            CDiscordRPCMgr::Update();
            CDebugVehicleSpawner::Process();

            CCutscenePartners::Process();
            CCameraMirror::Process();
            CCoopLowrider::Process();
            CCoopPool::Process();
            CActivityChallenge::Process();
            CSocialActions::Process();
            CGpsRoute::Process();
            CGarageSync::Process();
            CMissionMachines::Process();
            CMissionObjectMoves::Process();
            CMissionWarTable::Process();
            CrashfixHooks::ProcessPoolWatch();
            CPerfWatch::Process();

            // These check authentication themselves and reset their state outside a session.
            CParachuteSync::Process();
            CPlayerAnimMirror::Process();
            CPlayerMoney::Process();
            CVehicleSurf::Process();
            CSniperLaser::Process();
            CCheatSync::Process();

            if (/*CNetwork::m_bConnected*/ CNetwork::m_bAuthenticated)
            {
                // TODO: refactor
                if (COpCodeSync::ms_bLoadingCutscene && !CCoopMission::IsScriptAuthority() && CCutsceneMgr::ms_cutsceneName[0] &&
                    CCutsceneMgr::ms_cutsceneLoadStatus == 2)
                {
                    COpCodeSync::ms_bLoadingCutscene = false;
                    CMissionMirror::EnsureCutsceneTable();
                    Command<Commands::START_CUTSCENE>();
                }

                // the dialogue lines of the missions ("~z~" texts) show only with the Subtitles option on: force it once per
                // session when the player connects (a copy of the game with it off saw no dialogue at all, 2026-10-20)
                static bool bSubtitlesForced = false;
                if (!bSubtitlesForced)
                {
                    bSubtitlesForced = true;
                    FrontEndMenuManager.m_bPrefsShowSubtitles = true;
                }

                CCutsceneSkipVote::Process();

                if (!CCoopMission::IsScriptAuthority())
                {
                    MissionAudioHooks::ProcessNetwork();
                }

                if (CCoopMission::IsScriptAuthority() && CTheScripts::OnAMissionFlag)
                {
                    bool bOnMission = CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag] &&
                        COpCodeSync::IsSyncedMissionRunning();

                    if (bOnMission != lastOnMissionFlag)
                    {
                        lastOnMissionFlag = bOnMission;

                        Packets::Scripts::OnMissionFlagSync packet{};
                        packet.bOnMission = bOnMission;
                        GetPacketFactory().Send(packet);
                    }
                }

                unsigned int tickCount = CConfigLoader::GetSyncTime();

                CPassengerEnter::Process();
                CJackSync::Process();
                CSpawnWatch::Process();

                CPlayerPed* localPlayer = FindPlayerPed(0);

                CDriveBy::Process(localPlayer);

                int syncRate = 50;
                CVector velocity{};

                bool isDriver = localPlayer->m_nPedFlags.bInVehicle && localPlayer->m_pVehicle &&
                                localPlayer->m_pVehicle->m_pDriver == localPlayer;

                bool isPassenger = localPlayer->m_nPedFlags.bInVehicle && localPlayer->m_pVehicle &&
                                   localPlayer->m_pVehicle->m_pDriver != localPlayer;

                velocity = isDriver ? localPlayer->m_pVehicle->m_vecMoveSpeed : localPlayer->m_vecMoveSpeed;

                if (velocity.x == 0 && velocity.y == 0 && velocity.z == 0)
                {
                    syncRate = 100;
                }
                else if (isDriver && CConfigLoader::ms_bHighSyncRate)
                {
                    syncRate = 28;  // ~35 Hz, vehicles move fast
                }

                if (!isPassenger && tickCount > (isDriver ? lastDriverSyncTickRate : lastOnFootSyncTickRate) + syncRate)
                {
                    if (isDriver)
                    {
                        CNetworkVehicleManager::UpdateDriver(localPlayer->m_pVehicle);
                        lastDriverSyncTickRate = tickCount;
                    }
                    else
                    {
                        Packets::Players::OnFootUpdate onFootUpdate{};
                        onFootUpdate.vecPos = localPlayer->GetPosition();
                        onFootUpdate.vecMoveSpeed = localPlayer->m_vecMoveSpeed;
                        onFootUpdate.currentRotation = localPlayer->m_fCurrentRotation;
                        onFootUpdate.aimingRotation = localPlayer->m_fAimingRotation;

                        onFootUpdate.healthSnapshot.iHealth =
                            static_cast<uint8_t>(std::clamp(localPlayer->m_fHealth, 0.0f, 255.0f));
                        onFootUpdate.healthSnapshot.iArmour =
                            static_cast<uint8_t>(std::clamp(localPlayer->m_fArmour, 0.0f, 255.0f));

                        CWeapon& weapon = localPlayer->m_aWeapons[localPlayer->m_nActiveWeaponSlot];
                        onFootUpdate.weaponSnapshot.iWeaponType = weapon.m_eWeaponType;
                        onFootUpdate.weaponSnapshot.iWeaponState = weapon.m_nState;
                        onFootUpdate.weaponSnapshot.nAmmo = weapon.m_nAmmoInClip;

                        onFootUpdate.bDucking = CUtil::IsDucked(localPlayer);
                        onFootUpdate.bHasJetpack = CUtil::IsPedHasJetpack(localPlayer);

                        onFootUpdate.iFightingStyle = localPlayer->m_nFightingStyle;
                        onFootUpdate.areaCode = localPlayer->m_nAreaCode;
                        CKeySync::CollectState(onFootUpdate.keySnapshot);

                        if (localPlayer->m_pPlayerData)
                        {
                            float maxBreath = CStats::GetFatAndMuscleModifier(STAT_MOD_AIR_IN_LUNG);
                            if (maxBreath > 0.0f)
                                onFootUpdate.breathPercent = (uint8_t)std::clamp(
                                    localPlayer->m_pPlayerData->m_fBreath * 100.0f / maxBreath, 0.0f, 100.0f);
                        }

                        GetPacketFactory().Send(onFootUpdate);

                        lastOnFootSyncTickRate = tickCount;
                    }
                }

                if (isPassenger && tickCount > lastPassengerSyncTickRate + 333)
                {
                    CNetworkVehicleManager::UpdatePassenger(localPlayer->m_pVehicle, localPlayer);
                    lastPassengerSyncTickRate = tickCount;
                }

                if (tickCount > lastIdleVehicleSyncTickRate + 150)
                {
                    CNetworkVehicleManager::UpdateIdle();
                    lastIdleVehicleSyncTickRate = tickCount;
                }

                if (tickCount > lastVehicleDoorsSyncTickRate + 100)
                {
                    CNetworkVehicleManager::UpdateDoors();
                    lastVehicleDoorsSyncTickRate = tickCount;
                }

                if (tickCount > lastPedSyncTickRate + 50)
                {
                    CNetworkPedManager::Update();
                    lastPedSyncTickRate = tickCount;
                }

                CChatCommands::Process();
                CNetworkPlayerManager::ProcessAutoRefresh();
                CTestMode::Process();

                if (tickCount > lastFireSyncTickRate + 200)
                {
                    CFireSync::Process();
                    lastFireSyncTickRate = tickCount;
                }

                if (tickCount > lastTrainSyncTickRate + (CConfigLoader::ms_bTrainSyncRail ? 100 : 200))
                {
                    CTrainSync::Process();
                    lastTrainSyncTickRate = tickCount;
                }

                if (tickCount > lastStreamingSyncTickRate + 500)
                {
                    CStreamingSync::Process();
                    lastStreamingSyncTickRate = tickCount;
                }

                if (tickCount > lastPickupSyncTickRate + 100)
                {
                    CObjectSync::Process();
                    CPickupSync::Process();
                    lastPickupSyncTickRate = tickCount;
                }

                if (tickCount > lastCollectibleSyncTickRate + 250)
                {
                    CCollectibleSync::Process();
                    CPatientHandover::Process();
                    if ((tickCount / 250) % 2 == 0)
                        CMissionHunters::Process();
                    lastCollectibleSyncTickRate = tickCount;
                }

                if (tickCount > lastOnscreenTimerSyncTickRate + 500)
                {
                    COpCodeSync::ProcessOnscreenTimerSync();
                    lastOnscreenTimerSyncTickRate = tickCount;
                }

                if (tickCount > lastClothesSyncTickRate + 1000)
                {
                    CPersistenceSync::Process(tickCount);
                    CStatsSync::ProcessClothes();
                    lastClothesSyncTickRate = tickCount;
                }

                if (CLocalPlayer::m_bIsHost && tickCount > lastWeatherTimeSyncTickRate + 2000)
                {
                    CWeatherSync::SyncCurrentState();
                    lastWeatherTimeSyncTickRate = tickCount;
                }
                CNetworkVehicleManager::UpdateDamageSync();
                CNetworkVehicleManager::ProcessPendingComponents();

                CAimSync::ProcessSyncing();

                CNetworkPedManager::Process();

                CMinigameVs::Process();
                CCoopMission::Process();

                CNetworkEntityBlip::Update();

                static uint32_t crashHotkeyPressedAt = 0;

                if (GetAsyncKeyState(VK_F7) && GetAsyncKeyState(VK_F10) && GetAsyncKeyState(VK_NUMPAD1))
                {
                    if (crashHotkeyPressedAt == 0)
                    {
                        crashHotkeyPressedAt = tickCount;
                    }
                    if (tickCount - crashHotkeyPressedAt >= 5000)
                    {
                        *(uint*)0x0 = 0xDEADC0DE;
                    }
                }
                else
                {
                    crashHotkeyPressedAt = 0;
                }
            }
        };
        // the guest's checkpoint marker is placed where the scripts place theirs (a locate's red cylinder,
        // CTheScripts::HighlightImportantArea 0x485E00 -> C3dMarkers), before CSpecialFX::Update turns the frame's
        // markers on; placed while drawing, after C3dMarkers::Render, the friend's game didn't show it
        Events::processScriptsEvent.after += []
        {
            CMissionAreaMarkers::Process();
            CNetworkCheckpoint::Process();
            CNetworkStaticBlip::Process();
            CHighStakesRace::HideNativeHud();
            CVsRace::HideNativeHud();
            CCutsceneKeepSeat::Process();
        };
        Events::drawBlipsEvent.before += [] { CGpsRoute::Draw(); };  // the route lies under the blips
        Events::drawBlipsEvent += []
        {
            CNetworkPlayerMapPin::Process();
            CNetworkPlayerWaypoint::Process();
        };
        Events::drawingEvent += []
        {
            const bool hudHidden = CHudHideAll::IsActive();  // F2: nothing of the mod is drawn
            if (!hudHidden)
            {
                CHighStakesRace::Draw();
                CVsRace::DrawBoard();
                CVsLobby::DrawHelpBox();
                CCoopLowrider::DrawDanceTeam();
            }
            if (CEntryExitMarkerSync::ms_bUpdateAfterProcessingThisFrame && CLocalPlayer::m_bIsHost &&
                CEntryExitMarkerSync::ms_nLastUpdate + 5000 < GetTickCount())
            {
                CEntryExitMarkerSync::Send();
                CEntryExitMarkerSync::ms_bUpdateAfterProcessingThisFrame = false;
                CEntryExitMarkerSync::ms_nLastUpdate = GetTickCount();
            }

            if (CNetworkStaticBlip::ms_bNeedToSendAfterThisFrame && CLocalPlayer::m_bIsHost)
            {
                CNetworkStaticBlip::Send();
                CNetworkStaticBlip::ms_bNeedToSendAfterThisFrame = false;
            }

            /*if (GetAsyncKeyState(VK_F11) && CLocalPlayer::m_bIsHost)
            {
                CEntryExitMarkerSync::Send();
            }*/

            if (!hudHidden)
                CNetworkPlayerNameTag::Process();
            if (!CHudHideAll::IsChatHidden())
            {
                CChat::Draw();
                CChat::DrawInput();
            }

            if (FrontEndMenuManager.m_bPrefsShowHud && !hudHidden)
            {
                CNetworkPlayerList::Draw();
            }

            if (FrontEndMenuManager.m_bPrefsShowHud && !hudHidden && CCore::Version.stage != SEMVER_STAGE_RELEASE)
            {
                CDXFont::Draw(0, RsGlobal.maximumHeight - CDXFont::m_fFontSize,
                    std::string("COOP San Andreas " + std::string(COOPSA_VERSION)).c_str(),
                    D3DCOLOR_ARGB(255, 160, 160, 160));
            }

            if (!hudHidden && /*CNetwork::m_bConnected && */ GetAsyncKeyState(VK_F10))
            {
                CDebugPedTasks::Draw();
            }

            if (!hudHidden && /*CNetwork::m_bConnected && */ GetAsyncKeyState(VK_F9))
            {
                char buffer[270];
                sprintf(buffer,
                    "IsHost=%d | RTT %dms | Jitter %dms | Game/Network: Peds %d/%d | Cars %d/%d | Recv %d %.2f KB/S | "
                    "Sent %d %.2f KB/S | EnEx %d",
                    CLocalPlayer::m_bIsHost, CNetwork::GetRTT(), CNetwork::GetRTTVariance(),
                    CPools::ms_pPedPool->GetNoOfUsedSpaces(), CNetworkPedManager::m_pPeds.size(),
                    CPools::ms_pVehiclePool->GetNoOfUsedSpaces(), CNetworkVehicleManager::m_pVehicles.size(),
                    CNetwork::GetReceivedCount(), CNetwork::ms_nBytesReceivedThisSecond / 1024.0f,
                    CNetwork::GetSentCount(), CNetwork::ms_nBytesSentThisSecond / 1024.0f,
                    CEntryExitManager::mp_poolEntryExits->GetNoOfUsedSpaces());
                CDXFont::Draw(100, 10, buffer, D3DCOLOR_ARGB(255, 255, 255, 255));
                /*for (auto enex : CEntryExitManager::mp_poolEntryExits)
                {
                    CVector posn = CVector(enex->m_recEntrance.left, enex->m_recEntrance.bottom, enex->m_fEntranceZ);

                    RwV3d screenCoors; float w, h;
                    if (CSprite::CalcScreenCoors({ posn.x, posn.y, posn.z + 1.0f }, &screenCoors, &w, &h, true, true))
                    {
                        if (w >= 10.0f)
                        {
                            {
                                buffer[0] = 0;
                                if (enex->m_nFlags.bUnknownInterior)
                                    sprintf(buffer, "%sbUnknownInterior\n", buffer);
                                if (enex->m_nFlags.bUnknownPairing)
                                    sprintf(buffer, "%sbUnknownPairing\n", buffer);
                                if (enex->m_nFlags.bCreateLinkedPair)
                                    sprintf(buffer, "%sbCreateLinkedPair\n", buffer);
                                if (enex->m_nFlags.bRewardInterior)
                                    sprintf(buffer, "%sbRewardInterior\n", buffer);
                                if (enex->m_nFlags.bUsedRewardEntrance)
                                    sprintf(buffer, "%sbUsedRewardEntrance\n", buffer);
                                if (enex->m_nFlags.bCarsAndAircraft)
                                    sprintf(buffer, "%sbCarsAndAircraft\n", buffer);
                                if (enex->m_nFlags.bBikesAndMotorcycles)
                                    sprintf(buffer, "%sbBikesAndMotorcycles\n", buffer);
                                if (enex->m_nFlags.bDisableOnFoot)
                                    sprintf(buffer, "%sbDisableOnFoot\n", buffer);
                                if (enex->m_nFlags.bAcceptNpcGroup)
                                    sprintf(buffer, "%sbAcceptNpcGroup\n", buffer);
                                if (enex->m_nFlags.bFoodDateFlag)
                                    sprintf(buffer, "%sbFoodDateFlag\n", buffer);
                                if (enex->m_nFlags.bUnknownBurglary)
                                    sprintf(buffer, "%sbUnknownBurglary\n", buffer);
                                if (enex->m_nFlags.bDisableExit)
                                    sprintf(buffer, "%sbDisableExit\n", buffer);
                                if (enex->m_nFlags.bBurglaryAccess)
                                    sprintf(buffer, "%sbBurglaryAccess\n", buffer);
                                if (enex->m_nFlags.bEnteredWithoutExit)
                                    sprintf(buffer, "%sbEnteredWithoutExit\n", buffer);
                                if (enex->m_nFlags.bEnableAccess)
                                    sprintf(buffer, "%sbEnableAccess\n", buffer);
                                if (enex->m_nFlags.bDeleteEnex)
                                    sprintf(buffer, "%sbDeleteEnex\n", buffer);
                            }
                            sprintf(gString, "name %s\nflags %x\n%s\nleft %f\nbottom %f\narea %d\nlink %x\nlast %d",
                enex->m_szName, *(uint16_t*)&enex->m_nFlags, buffer, enex->m_recEntrance.left,
                enex->m_recEntrance.bottom, enex->m_nArea, enex->m_pLink,
                CEntryExitMarkerSync::ms_mapLastEnExUpdate[enex]); CDXFont::Draw((int)screenCoors.x, (int)screenCoors.y,
                gString, D3DCOLOR_ARGB(255, 255, 255, 255));
                        }
                    }

                }*/

                /*for (auto tag : CTagManager::ms_tagDesc)
                {
                    if (!tag.m_pEntity)
                        continue;
                    if (tag.m_nAlpha != 0)
                        continue;

                    CVector& posn = tag.m_pEntity->GetPosition();

                    RwV3d screenCoors;
                    float w, h;
                    if (CSprite::CalcScreenCoors({posn.x, posn.y, posn.z + 1.0f}, &screenCoors, &w, &h, true, true))
                    {
                        sprintf(gString, "alpha %d\n%f %f %f", tag.m_nAlpha, posn.x, posn.y, posn.z);
                        CDXFont::Draw(
                            (int)screenCoors.x, (int)screenCoors.y, gString, D3DCOLOR_ARGB(255, 255, 255, 255));
                    }
                }*/

                /*for (int i = 0; i < MAX_RADAR_TRACES; i++)
                {
                    auto& trace = CRadar::ms_RadarTrace[i];

                    if (trace.m_nBlipType != eBlipType::BLIP_CONTACTPOINT
                        && trace.m_nBlipType != eBlipType::BLIP_COORD)
                    {
                        continue;
                    }

                    RwV3d screenCoors; float w, h;
                    if (CSprite::CalcScreenCoors({ trace.m_vecPos.x, trace.m_vecPos.y, trace.m_vecPos.z + 1.0f },
                &screenCoors, &w, &h, false, true))
                    {
                        if (w >= 0.0f)
                        {
                        eBlipDisplay::BLIP_DISPLAY_NEITHER;
                        char* display = "BLIP_DISPLAY_NEITHER";

                        switch (trace.m_nBlipDisplay)
                        {
                        case eBlipDisplay::BLIP_DISPLAY_MARKER_ONLY:
                            display = "BLIP_DISPLAY_MARKER_ONLY";
                            break;
                        case eBlipDisplay::BLIP_DISPLAY_BOTH:
                            display = "BLIP_DISPLAY_BOTH";
                            break;
                        case eBlipDisplay::BLIP_DISPLAY_BLIP_ONLY:
                            display = "BLIP_DISPLAY_BLIP_ONLY";
                            break;
                        }

                        sprintf(gString, "pos %.1f %.1f %.1f\n%s\n%s",
                            trace.m_vecPos.x, trace.m_vecPos.y, trace.m_vecPos.z,
                            trace.m_nBlipType == eBlipType::BLIP_CONTACTPOINT ? "BLIP_CONTACTPOINT" : "BLIP_COORD",
                            display);

                        CDXFont::Draw((int)screenCoors.x, (int)screenCoors.y, gString, D3DCOLOR_ARGB(255, 255, 255,
                255));
                        //}
                    }
                }*/

                for (auto networkVehicle : CNetworkVehicleManager::m_pVehicles)
                {
                    if (!networkVehicle || !networkVehicle->m_pVehicle)
                        continue;

                    CVehicle* vehicle = networkVehicle->m_pVehicle;
                    if (!vehicle || !vehicle->m_matrix)
                        continue;

                    CVector posn = vehicle->m_matrix->pos;
                    RwV3d screenCoors;
                    float w, h;
                    if (CSprite::CalcScreenCoors({posn.x, posn.y, posn.z + 1.0f}, &screenCoors, &w, &h, true, true))
                    {
                        CDXFont::Draw((int)screenCoors.x, (int)screenCoors.y,
                            ("v " + to_string(networkVehicle->m_vehicleId) + "\nS " +
                                std::to_string(networkVehicle->m_bSyncing) + "\n" +
                                std::to_string(networkVehicle->m_pVehicle->m_fGasPedal) + " " +
                                std::to_string(networkVehicle->m_pVehicle->m_fBreakPedal) + "\nC " +
                                std::to_string(networkVehicle->m_pVehicle->m_nCreatedBy) + "\npaint " +
                                std::to_string(networkVehicle->m_pVehicle->GetRemapIndex())

                                    )
                                .c_str(),
                            D3DCOLOR_ARGB(255, 255, 255, 255));
                    }
                }

                for (auto networkPed : CNetworkPedManager::m_pPeds)
                {
                    if (!networkPed || !networkPed->m_pPed)
                        continue;

                    CPed* ped = networkPed->m_pPed;
                    if (!ped || !ped->m_matrix)
                        continue;

                    CVector posn = ped->m_matrix->pos;
                    RwV3d screenCoors;
                    float w, h;
                    if (CSprite::CalcScreenCoors({posn.x, posn.y, posn.z + 1.0f}, &screenCoors, &w, &h, true, true))
                    {
                        CDXFont::Draw((int)screenCoors.x, (int)screenCoors.y,
                            ("p " + to_string(networkPed->m_pedId) + "\nS " +
                                std::to_string(networkPed->m_bSyncing) + "\n" + std::to_string(ped->m_nMoveState))
                                .c_str(),
                            D3DCOLOR_ARGB(255, 255, 255, 255));
                    }
                }

                std::string buf;
                for (int i = 0; i < 180; i++)
                {
                    CAnimBlock& block = CAnimManager::ms_aAnimBlocks[i];
                    if (!block.bLoaded)
                        continue;

                    buf += std::string(block.szName) + '\n';
                }

                CDXFont::Draw(10, 25, buf.c_str(), D3DCOLOR_ARGB(255, 255, 255, 255));

                CVector posn = FindPlayerPed(0)->m_matrix->pos;
                RwV3d screenCoors;
                float w, h;
                // if (CSprite::CalcScreenCoors({posn.x, posn.y, posn.z + 1.0f}, &screenCoors, &w, &h, true, true))
                {
                    float orientsim = atan2f(TheCamera.m_aCams[TheCamera.m_nActiveCam].m_vecFront.x,
                        TheCamera.m_aCams[TheCamera.m_nActiveCam].m_vecFront.y);
                    char buf[512];
                    sprintf(buf, "cam %f/%f\ncur %f des %f\n%s/%s\nfront %f %f %f\nsource %f %f %f\nup %f %f %f",
                        TheCamera.m_fOrientation, orientsim, FindPlayerPed(0)->m_fCurrentRotation,
                        FindPlayerPed(0)->m_fAimingRotation, CamModeToString(TheCamera.m_aCams[0].m_nMode),
                        CamModeToString((eCamMode)TheCamera.m_PlayerWeaponMode.m_nMode),
                        TheCamera.m_aCams[TheCamera.m_nActiveCam].m_vecFront.x,
                        TheCamera.m_aCams[TheCamera.m_nActiveCam].m_vecFront.y,
                        TheCamera.m_aCams[TheCamera.m_nActiveCam].m_vecFront.z,
                        TheCamera.m_aCams[TheCamera.m_nActiveCam].m_vecSource.x,
                        TheCamera.m_aCams[TheCamera.m_nActiveCam].m_vecSource.y,
                        TheCamera.m_aCams[TheCamera.m_nActiveCam].m_vecSource.z,
                        TheCamera.m_aCams[TheCamera.m_nActiveCam].m_vecUp.x,
                        TheCamera.m_aCams[TheCamera.m_nActiveCam].m_vecUp.y,
                        TheCamera.m_aCams[TheCamera.m_nActiveCam].m_vecUp.z);
                    CDXFont::Draw((int)100, (int)100, buf, D3DCOLOR_ARGB(255, 255, 255, 255));

                    /*CVehicle* pVehicle = FindPlayerPed(0)->m_pVehicle;
                    if (pVehicle)
                    {
                        static float maxaaa = 0.0f;
                        maxaaa = std::max(maxaaa, std::abs(pVehicle->m_vecTurnSpeed.x));
                        maxaaa = std::max(maxaaa, std::abs(pVehicle->m_vecTurnSpeed.y));
                        maxaaa = std::max(maxaaa, std::abs(pVehicle->m_vecTurnSpeed.z));
                        sprintf(buf,
                            "pos {%f, %f, %f}\n"
                            "right {%f, %f, %f}\n"
                            "up {%f, %f, %f}\n"
                            "moveSpeed {%f, %f, %f}\n"
                            "turnSpeed {%f, %f, %f} (%f)"
                            ,
                            pVehicle->m_matrix->pos.x, pVehicle->m_matrix->pos.y, pVehicle->m_matrix->pos.z,
                            pVehicle->m_matrix->right.x, pVehicle->m_matrix->right.y, pVehicle->m_matrix->right.z,
                            pVehicle->m_matrix->up.x, pVehicle->m_matrix->up.y, pVehicle->m_matrix->up.z,
                            pVehicle->m_vecMoveSpeed.x, pVehicle->m_vecMoveSpeed.y, pVehicle->m_vecMoveSpeed.z,
                            pVehicle->m_vecTurnSpeed.x, pVehicle->m_vecTurnSpeed.y, pVehicle->m_vecTurnSpeed.z, maxaaa
                        );
                        CDXFont::Draw((int)350, (int)100, buf, D3DCOLOR_ARGB(255, 255, 255, 255));
                    }*/
                }

                /*
                for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
                {
                    posn = networkPlayer->m_pPed->m_matrix->pos;
                    if (CSprite::CalcScreenCoors({posn.x, posn.y, posn.z + 1.0f}, &screenCoors, &w, &h, true, true))
                    {
                        char buf[128];
                        sprintf(buf, "cam %f\nncur %f ndes %f\ncur %f des %f",
                            networkPlayer->m_cameraSnapshot.orientation.m_angle,
                            networkPlayer->m_onFootSnapshotInterpolated.currentRotation.m_angle,
                            networkPlayer->m_onFootSnapshotInterpolated.aimingRotation.m_angle,
                            networkPlayer->m_pPed->m_fCurrentRotation, networkPlayer->m_pPed->m_fAimingRotation);

                        CDXFont::Draw((int)screenCoors.x, (int)screenCoors.y, buf, D3DCOLOR_ARGB(255, 255, 255, 255));
                    }
                }
                */
            }
        };
        CCore::Init();
        // after CCore::Init: PatchSCM redirects 0x748CFB, which would wipe an initGameEvent hook installed earlier
        Events::initGameEvent.before += [] { CCutscenePartners::Reset(); };
        // the script space still moved (FixLimitAdjusterScriptSpace): the adjuster read its ini before it was set
        Events::initGameEvent.after += [] {
            static bool warned = false;
            if (warned)
                return;
            warned = true;
            if (*reinterpret_cast<uint32_t*>(0x469F34) != 0xA49960 || *reinterpret_cast<uint32_t*>(0x489A60) != 0xA7A6A0)
            {
                CMissionMirror::LogEventf("script-space-moved main=%08X mission=%08X", *reinterpret_cast<uint32_t*>(0x469F34),
                    *reinterpret_cast<uint32_t*>(0x489A60));
                // set at load: the adjuster had read the old values, one restart takes the new ones
                if (ms_bLimitIniChangedAtLoad)
                    MessageBoxA(NULL,
                        "Open Limit Adjuster movio la memoria de los scripts y el coop no anda bien en misiones.\n\n"
                        "Ya quedo corregida su configuracion: cerra el GTA y volve a abrirlo (una sola vez).",
                        "CoopSA", MB_ICONWARNING);
                else
                    MessageBoxA(NULL,
                        "Otro mod (Open Limit Adjuster) movio la memoria de los scripts y el coop no va a andar bien en misiones.\n\n"
                        "Abri III.VC.SA.LimitAdjuster.ini y en la seccion [SALIMITS] agrega:\n"
                        "MAINsegmentSize = 0\nMissionSize = 0\n\nDespues reinicia el juego.",
                        "CoopSA", MB_ICONWARNING);
            }
        };
    };
} CoopSanAndreasPlugin;
