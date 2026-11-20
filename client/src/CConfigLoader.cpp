#include "stdafx.h"
#include "CConfigLoader.h"
#include "CCustomMenuManager.h"
#include <CCrashReporter.h>
#include "CPreciseTime.h"
#include <CLaunchManager.h>

void CConfigLoader::BuildPath()
{
    ms_sDataPath = std::string((const char*)0xC92368) + "\\" + CONFIG_FILE_NAME;  // 0xC92368 - gta_user_dir_path var
}

void CConfigLoader::BuildDefaultConfig()
{
    FILE* file = fopen(ms_sDataPath.c_str(), "wb");

    if (!file)
    {
        printf("Failed to create config file\n");
        return;
    }

    std::string config = "[" + CONFIG_SECTION + "]" + "\n";

    for (const auto& [key, value] : ms_umDefaultConfig)
    {
        config += key + "=" + value + "\n";
    }

    fwrite(config.c_str(), sizeof(char), config.length(), file);
    fclose(file);
}

void CConfigLoader::EnsureCreated()
{
    if (FileExists(ms_sDataPath.c_str()))
    {
        return;
    }

    BuildDefaultConfig();
}

uintptr_t SetDirMyDocuments_Hook_ptr = 0x0;
void CConfigLoader::SetDirMyDocuments_Hook()
{
    BuildPath();
    Load();
    CLaunchManager::ApplyOverrides();

    CCrashReporter::FetchIP();  // init crash reporting stuff AFTER loading the config

    CCustomMenuManager::UpdateFromConfig();
    plugin::CallDyn(SetDirMyDocuments_Hook_ptr);
}

uint32_t CConfigLoader::GetSyncTime()
{
    return ms_bPreciseTimers ? GetPreciseTickCount() : GetTickCount();
}

void CConfigLoader::Init()
{
    SetDirMyDocuments_Hook_ptr = injector::GetBranchDestination(0x748995).as_int();
    patch::RedirectCall(0x748995, SetDirMyDocuments_Hook);
}

// "F1".."F12" -> virtual key, anything else -> F8
static int ParseFunctionKey(const char* name)
{
    if ((name[0] == 'F' || name[0] == 'f') && name[1])
    {
        int number = atoi(name + 1);
        if (number >= 1 && number <= 12)
            return VK_F1 + number - 1;
    }
    return VK_F8;
}

void CConfigLoader::Load()
{
    EnsureCreated();

    GetPrivateProfileString(CONFIG_SECTION.c_str(), "nickname", "", CLocalPlayer::m_Name, sizeof(CLocalPlayer::m_Name),
        ms_sDataPath.c_str());
    GetPrivateProfileString(CONFIG_SECTION.c_str(), "ip", "", CNetwork::m_IpAddress, sizeof(CNetwork::m_IpAddress), ms_sDataPath.c_str());
    CNetwork::m_nPort =
        GetPrivateProfileInt(CONFIG_SECTION.c_str(), "port", Config::DEFAULT_PORT, ms_sDataPath.c_str());

    // crash logs went to the original author's server: this project never sends them (they stay in
    // CoopAndreas_crashes), whatever the file says
    CCrashReporter::ms_bReportingEnabled = false;

    auto readBool = [](const char* key, const char* defaultValue = "true")
    {
        // server-client-config: what the server imposes wins over the file
        auto it = ms_umServerOverrides.find(key);
        if (it != ms_umServerOverrides.end())
            return it->second;
        char value[6];
        GetPrivateProfileString(CONFIG_SECTION.c_str(), key, defaultValue, value, sizeof(value), ms_sDataPath.c_str());
        value[5] = 0;
        return _strnicmp(value, "true", 5) == 0;
    };
    ms_bRemoteHornSync = readBool("remote-horn-sync");
    ms_bRemoteHeliRotorSync = readBool("remote-heli-rotor-sync");
    ms_bRemoteBoatSync = readBool("remote-boat-sync");
    ms_bRemoteReviveAuto = readBool("remote-revive-auto");
    ms_bHighSyncRate = readBool("high-sync-rate");
    ms_bAdaptiveJitterBuffer = readBool("adaptive-jitter-buffer");
    ms_bNpcDistanceRate = readBool("npc-distance-rate");
    ms_bPreciseTimers = readBool("precise-timers");
    ms_bCameraMirror = readBool("camera-mirror");
    ms_bCameraMirrorInteriorGuard = readBool("camera-mirror-interior-guard");
    ms_bNpcTaskSync = readBool("npc-task-sync");
    ms_bNpcNamedAnimSync = readBool("npc-named-anim-sync");
    ms_bNpcSyncedPoseKeep = readBool("npc-synced-pose-keep");
    ms_bInteriorHandover = readBool("interior-handover");
    ms_bInteriorPopulationSync = readBool("interior-population-sync");
    ms_bInteriorAreaFallback = readBool("interior-area-fallback");
    ms_bInteriorObjectSync = readBool("interior-object-sync");
    ms_bInteriorServiceGuest = readBool("interior-service-guest");
    ms_bMissionInteriorSafety = readBool("mission-interior-safety");
    ms_bHandover = readBool("handover");
    ms_bZonePopulation = readBool("zone-population");
    ms_bPolicePerPlayer = readBool("police-per-player");
    ms_bRandomCopsCap = readBool("random-cops-cap");
    ms_bCoopSideMissions = readBool("coop-side-missions");
    ms_bOwnStaticBlips = readBool("own-static-blips");
    // off by the owner (testeo 17-11, buggy): a new key so the old ini files that kept true do not turn it on
    ms_bStoryHelpOptional = readBool("story-help-ask", "false");
    // old ini files kept contacts-follow-host=true: story-help-optional replaces it (each one sees his own contacts)
    ms_bContactsFollowHost = readBool("contacts-follow-host", "false") && !ms_bStoryHelpOptional;
    ms_bTeleportLoadInterior = readBool("teleport-load-interior");
    ms_bCoopLowrider = readBool("coop-lowrider");
    ms_bLowriderKeepCar = readBool("lowrider-keep-car");
    ms_bVsRaceSync = readBool("vs-race-sync");
    ms_bCoopDance = readBool("coop-dance");
    ms_bDanceTeamScore = readBool("dance-team-score");
    ms_bCheatMenuAll = readBool("cheat-menu-all");
    ms_bCheatAnnounce = readBool("cheat-announce");
    ms_bShopClerkFail = readBool("mission-shop-clerk-fail");
    ms_bPerfTelemetry = readBool("perf-telemetry");
    ms_bWeaponModelRelease = readBool("weapon-model-release");
    ms_bAnimDestroyNotify = readBool("anim-destroy-notify");
    ms_bRecruitDismissSync = readBool("recruit-dismiss-sync");
    ms_bRecruitAllPlayers = readBool("recruit-all-players");
    ms_bDanceGuestStory = readBool("dance-guest-story");
    ms_bDanceSideBySide = readBool("dance-side-by-side");
    ms_bPlayerFullRebuild = readBool("player-full-rebuild");
    ms_bMusic1BeachArrival = readBool("music1-beach-arrival");
    ms_bMissionAudioZones = readBool("mission-audio-zones");
    ms_bMissionGuestCarSections = readBool("mission-guest-car-sections");
    ms_bMissionGuestVehiclesSelfBoard = readBool("mission-guest-vehicles-self-board");
    ms_bMissionHostDriverHint = readBool("mission-host-driver-hint");
    ms_bMissionGuestCarHostReach = readBool("mission-guest-car-host-reach");
    ms_bMusic1GarageHostDriver = readBool("music1-garage-host-driver");
    ms_bFadeGuard = readBool("fade-guard");
    ms_bSkipDeathArrestHelp = readBool("skip-death-arrest-help", "false");
    ms_bPoolTwoPlayers = readBool("pool-two-players");
    ms_bActivityChallenge = readBool("activity-challenge");
    ms_bEmotes = readBool("emotes");
    ms_bGpsRoute = readBool("gps-route");
    ms_bMenusEstiloSa = readBool("menus-estilo-sa");
    ms_bHudHideAllF2 = readBool("hud-hide-all-f2");
    ms_bGarageSync = readBool("garage-sync");
    ms_bCoopMissionMachines = readBool("coop-mission-machines");
    ms_bCutscenePartners = readBool("cutscene-partners");
    ms_bCutsceneShowOthers = readBool("cutscene-show-others");
    ms_bSceneGuestsVisible = readBool("scene-guests-visible");
    ms_bCutscenePartnerSmooth = readBool("cutscene-partner-smooth");
    ms_bJustBusinessBikeEscort = readBool("just-business-bike-escort");
    ms_bJustBusinessNoMarkerWait = readBool("just-business-no-marker-wait");
    ms_bHighStakesCoop = readBool("high-stakes-coop");
    ms_bHighStakesGuestNotice = readBool("high-stakes-guest-notice");
    ms_bServerClothesRestore = readBool("server-clothes-restore", "false");
    ms_bGuestClearAreaKeepCar = readBool("guest-clear-area-keep-car");
    ms_bHighStakesNoGridWait = readBool("high-stakes-no-grid-wait");
    ms_bHighStakesGuestBlips = readBool("high-stakes-guest-blips");
    ms_bHighStakesBoard = readBool("high-stakes-board");
    ms_bHighStakesHideNativeHud = readBool("high-stakes-hide-native-hud");
    ms_bHighStakesFinishNotice = readBool("high-stakes-finish-notice");
    ms_bHighStakesEndCleanup = readBool("high-stakes-end-cleanup");
    ms_bHighStakesPassenger = readBool("high-stakes-passenger");
    ms_bHighStakesSeatPresence = readBool("high-stakes-seat-presence");
    ms_bHighStakesRestoreSeat = readBool("high-stakes-restore-seat");
    ms_bCutsceneKeepSeat = readBool("cutscene-keep-seat");
    ms_bHighStakesTeamResult = readBool("high-stakes-team-result");
    ms_bCutsceneHideExtras = readBool("cutscene-hide-extras");
    ms_bCutsceneBlackBars = readBool("cutscene-black-bars", "false");
    ms_bAutoReconnect = readBool("auto-reconnect");
    ms_bClearMessages = readBool("clear-messages");
    ms_bNativeMarkers = readBool("native-markers");
    ms_bMissionHunters = readBool("mission-hunters", "false");
    ms_bMirrorBlips = readBool("mirror-blips");
    ms_bVoiceChat = readBool("voice-chat");
    ms_nVoiceMicVolume = std::clamp<int>(GetPrivateProfileInt(CONFIG_SECTION.c_str(), "voice-mic-volume", 100, ms_sDataPath.c_str()), 0, 200);
    ms_nVoiceOutVolume = std::clamp<int>(GetPrivateProfileInt(CONFIG_SECTION.c_str(), "voice-out-volume", 100, ms_sDataPath.c_str()), 0, 200);
    ms_bVoiceToggle = readBool("voice-toggle", "false");
    GetPrivateProfileString(CONFIG_SECTION.c_str(), "voice-mic-device", "", ms_szVoiceMic, sizeof(ms_szVoiceMic), ms_sDataPath.c_str());
    ms_bTrainSync = readBool("train-sync");
    ms_bTrainSyncRail = readBool("train-sync-rail");
    ms_bTrainSyncKeep = readBool("train-sync-keep");
    ms_bMissionSyncRules = readBool("mission-sync-rules");
    ms_bIntroBoard = readBool("intro-board");
    ms_bIntroIndependent = readBool("intro-independent");
    ms_bNativeHints = readBool("native-hints");
    ms_bMissionWaitNotices = readBool("mission-wait-notices");
    ms_bSyncLocalTextVars = readBool("sync-local-text-vars");
    ms_bMissionGuestCounts = readBool("mission-guest-counts");
    ms_bMissionGuestActions = readBool("mission-guest-actions");
    ms_bCoopPhotos = readBool("coop-photos");
    ms_bCoopHomeInvasion = readBool("coop-home-invasion");
    ms_bMissionWaitSamePlace = readBool("mission-wait-same-place");
    ms_bMissionWaitOnEntry = readBool("mission-wait-on-entry");
    ms_bMissionWaitScenes = readBool("mission-wait-scenes");
    ms_bMissionScenePrelude = readBool("mission-scene-prelude");
    ms_bMissionHoldOnEntry = readBool("mission-hold-on-entry");
    ms_bMissionEntryNoTeleport = readBool("mission-entry-no-teleport");
    ms_bMissionNoAutoBoard = readBool("mission-no-auto-board");
    ms_bMissionSceneSkipNoControl = readBool("mission-scene-skip-no-control");
    ms_bMissionHoldKeepTags = readBool("mission-hold-keep-tags");
    ms_bCutsceneTableFallback = readBool("cutscene-table-fallback");
    ms_bIntroFollowHost = readBool("intro-follow-host");
    ms_bMirrorBlipsClearOnEnd = readBool("mirror-blips-clear-on-end");
    ms_bMissionCarOverlapDedupe = readBool("mission-car-overlap-dedupe");
    ms_bPhoneCallKeepsOnMission = readBool("phone-call-keeps-onmission");
    ms_bSceneGuestEndNearHost = readBool("scene-guest-end-near-host");
    ms_bMissionGuestTrain = readBool("mission-guest-train");
    ms_bMissionShopOneAtATime = readBool("mission-shop-one-at-a-time");
    ms_bMissionAnyPlayerInCar = readBool("mission-any-player-in-car");
    ms_bMissionAnyPlayerTrigger = readBool("mission-any-player-trigger");
    ms_bMissionAllyNoGuestDamage = readBool("mission-ally-no-guest-damage");
    ms_bMissionAllyFollowArea = readBool("mission-ally-follow-area");
    ms_bMissionPlayerEquipment = readBool("mission-player-equipment");
    ms_bMissionNinesAmmoRepair = readBool("mission-nines-ammo-repair");
    ms_bGuestStoryLaunchers = readBool("guest-story-launchers");
    ms_bMissionNinesFarBoarding = readBool("mission-nines-far-boarding");
    ms_bMissionNinesBoardNear = readBool("mission-nines-board-near");
    ms_bMissionEquipmentScriptAmmo = readBool("mission-equipment-script-ammo");
    ms_bMissionGuestWeaponParity = readBool("mission-guest-weapon-parity");
    ms_bRecruitBoardPassenger = readBool("recruit-board-passenger");
    ms_bMissionAudioGuard = readBool("mission-audio-guard");
    ms_bMissionHoodDoorOpen = readBool("mission-hood-door-open");
    ms_bRemoteBulletNoForce = readBool("remote-bullet-no-force");
    ms_bPedDamageAuthority = readBool("ped-damage-authority");
    ms_bPedDamageAnyPlayer = readBool("ped-damage-any-player");
    ms_bNpcAimSync = readBool("npc-aim-sync");
    ms_bNpcDriveBySync = readBool("npc-driveby-sync");
    ms_bNpcVelocitySync = readBool("npc-velocity-sync");
    ms_bNpcAnimMirror = readBool("npc-anim-mirror");
    ms_bPlayerAnimMirror = readBool("player-anim-mirror");
    ms_bGuestCarBlipHide = readBool("guest-car-blip-hide");
    ms_bChatTpmFijar = readBool("chat-tpm-fijar");
    ms_bPlayerListCoopSa = readBool("player-list-coopsa");
    ms_bEmergencyDedupe = readBool("emergency-dedupe");
    ms_bSpawnWatch = readBool("spawn-watch");
    ms_bPlayerJackNative = readBool("player-jack-native");
    ms_bMissionSharedOnScreen = readBool("mission-shared-on-screen");
    ms_bMissionWideCarOnScreen = readBool("mission-wide-car-on-screen");
    ms_bMissionScriptedBoarding = readBool("mission-scripted-boarding");
    ms_bMissionOgLocBikes = readBool("mission-og-loc-bikes");
    ms_bMissionOgLocGuestCar = readBool("mission-og-loc-guest-car");
    ms_bMissionDriveThruGuestCar = readBool("mission-drive-thru-guest-car");
    ms_bCoopDriveBy = readBool("coop-drive-by");
    ms_bCoopLosSepulcros = readBool("coop-los-sepulcros");
    ms_bCoopRunningDog = readBool("coop-running-dog");
    ms_bCoopWrongSideTracks = readBool("coop-wrong-side-tracks");
    ms_bCoopRobbingUncleSam = readBool("coop-robbing-uncle-sam");
    ms_bCoopRobbingGuestDeliver = readBool("coop-robbing-guest-deliver");
    ms_bObjectMovingNoCollide = readBool("object-moving-nocollide");
    ms_bMissionGuestCarBoard = readBool("mission-guest-car-board");
    ms_bMissionDriveThruFirstCar = readBool("mission-drive-thru-first-car");
    ms_bMissionTripSkipGuests = readBool("mission-trip-skip-guests");
    ms_bMissionCatalystGuestCar = readBool("mission-catalyst-guest-car");
    ms_bCoopJustBusiness = readBool("coop-just-business");
    ms_bCoopMaddDogg = readBool("coop-madd-dogg");
    ms_bCoopGrayImports = readBool("coop-gray-imports");
    ms_bCoopGreenSabre = readBool("coop-green-sabre");
    ms_bCoopOgLocEscape = readBool("coop-og-loc-escape");
    ms_bCoopBadlands = readBool("coop-badlands");
    ms_bCoopCountryside = readBool("coop-countryside");
    ms_bVehicleDamageAnyPlayer = readBool("vehicle-damage-any-player");
    ms_bCoopCarGunners = readBool("coop-car-gunners");
    ms_bMissionBoardingSharedCar = readBool("mission-boarding-shared-car");
    ms_bMissionNoMidCar = readBool("mission-no-midcar");
    ms_bMissionWaitPauseTimer = readBool("mission-wait-pause-timer");
    ms_bCoopOgLocSpeed = readBool("coop-og-loc-speed");
    ms_bVsRaceBoard = readBool("vs-race-board");
    ms_bVsLobbyHelpBox = readBool("vs-lobby-helpbox");
    ms_bCoopManagementIssues = readBool("coop-management-issues");
    ms_bMissionGuestCarCopies = readBool("mission-guest-car-copies", "false");
    ms_bMissionScriptedExit = readBool("mission-scripted-exit");
    ms_bMirrorPickups = readBool("mirror-pickups");
    ms_bMissionPickupWeaponShare = readBool("mission-pickup-weapon-share");
    ms_bPatientHandover = readBool("patient-handover");
    ms_bAmbulanceGuestPatients = readBool("ambulance-guest-patients");
    ms_nZonePedBudget = GetPrivateProfileInt(CONFIG_SECTION.c_str(), "zone-ped-budget", 25, ms_sDataPath.c_str());
    if (ms_nZonePedBudget < 0)
        ms_nZonePedBudget = 0;
    ms_nMissionGatherSkipNear = GetPrivateProfileInt(CONFIG_SECTION.c_str(), "mission-gather-skip-near", 20, ms_sDataPath.c_str());
    if (ms_nMissionGatherSkipNear < 0)
        ms_nMissionGatherSkipNear = 0;

    char szSmooth[6];
    GetPrivateProfileString(CONFIG_SECTION.c_str(), "smooth-vehicles", "true", szSmooth, sizeof(szSmooth), ms_sDataPath.c_str());
    szSmooth[5] = 0;
    ms_bSmoothVehicles = _strnicmp(szSmooth, "true", 5) == 0;

    GetPrivateProfileString(CONFIG_SECTION.c_str(), "smooth-onfoot", "true", szSmooth, sizeof(szSmooth), ms_sDataPath.c_str());
    szSmooth[5] = 0;
    ms_bSmoothOnFoot = _strnicmp(szSmooth, "true", 5) == 0;

    char szCheatKey[8];
    DWORD cheatKeyLength = GetPrivateProfileString(
        CONFIG_SECTION.c_str(), "cheat-menu-key", "", szCheatKey, sizeof(szCheatKey), ms_sDataPath.c_str());
    szCheatKey[7] = 0;
    CCheatMenu::ms_nToggleKey = ParseFunctionKey(szCheatKey);
    bool bSaveCheatKey = cheatKeyLength == 0;  // bind it in the file the first time
    // HUD hide (F2), voice (F4), chat (F6) and debug views (F9, F10) already use these.
    if (CCheatMenu::ms_nToggleKey == VK_F2 || CCheatMenu::ms_nToggleKey == VK_F4 || CCheatMenu::ms_nToggleKey == VK_F6 || CCheatMenu::ms_nToggleKey == VK_F9 || CCheatMenu::ms_nToggleKey == VK_F10)
        CCheatMenu::ms_nToggleKey = VK_F8;

    if (bSaveCheatKey)
        Save();

    // settings-version 1: the smoothing options became enabled by default; older files saved them as false
    if (GetPrivateProfileInt(CONFIG_SECTION.c_str(), "settings-version", 0, ms_sDataPath.c_str()) < 1)
    {
        ms_bSmoothVehicles = true;
        ms_bSmoothOnFoot = true;
        Save();
    }

#if DEBUG
    logger::info("CConfigLoader::Load - Name %s Address %s Port %d ReportCrashlogs %s\n", CLocalPlayer::m_Name,
        CNetwork::m_IpAddress, CNetwork::m_nPort, CCrashReporter::ms_bReportingEnabled ? "true" : "false");
#endif
}

void CConfigLoader::ApplyServerOverrides(const std::vector<std::pair<std::string, bool>>& overrides)
{
    ms_umServerOverrides.clear();
    for (const auto& [key, value] : overrides)
    {
        if (ms_umOverriddenFileValues.find(key) == ms_umOverriddenFileValues.end())
        {
            char fileValue[64];
            GetPrivateProfileString(CONFIG_SECTION.c_str(), key.c_str(), "\x01", fileValue, sizeof(fileValue), ms_sDataPath.c_str());
            ms_umOverriddenFileValues[key] = fileValue;
        }
        ms_umServerOverrides[key] = value;
    }

    // the connection in use stays as it is: the launcher may have given another address than the file's
    const std::string ip = CNetwork::m_IpAddress, name = CLocalPlayer::m_Name;
    const auto port = CNetwork::m_nPort;
    Load();
    strcpy_s(CNetwork::m_IpAddress, ip.c_str());
    strcpy_s(CLocalPlayer::m_Name, name.c_str());
    CNetwork::m_nPort = port;
}

void CConfigLoader::Save()
{
    EnsureCreated();

    WritePrivateProfileString(CONFIG_SECTION.c_str(), "nickname", CLocalPlayer::m_Name, ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "ip", CNetwork::m_IpAddress, ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "port", std::to_string(CNetwork::m_nPort).c_str(), ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "report-crashlogs", CCrashReporter::ms_bReportingEnabled ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "smooth-vehicles", ms_bSmoothVehicles ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "smooth-onfoot", ms_bSmoothOnFoot ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "high-sync-rate", ms_bHighSyncRate ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "adaptive-jitter-buffer", ms_bAdaptiveJitterBuffer ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "npc-distance-rate", ms_bNpcDistanceRate ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "precise-timers", ms_bPreciseTimers ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "camera-mirror", ms_bCameraMirror ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "npc-task-sync", ms_bNpcTaskSync ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "npc-named-anim-sync",
        ms_bNpcNamedAnimSync ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "npc-synced-pose-keep",
        ms_bNpcSyncedPoseKeep ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "interior-population-sync",
        ms_bInteriorPopulationSync ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "interior-area-fallback",
        ms_bInteriorAreaFallback ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "interior-object-sync",
        ms_bInteriorObjectSync ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "interior-service-guest",
        ms_bInteriorServiceGuest ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "interior-handover",
        ms_bInteriorHandover ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-interior-safety",
        ms_bMissionInteriorSafety ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "handover", ms_bHandover ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "zone-population", ms_bZonePopulation ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "zone-ped-budget", std::to_string(ms_nZonePedBudget).c_str(), ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "police-per-player", ms_bPolicePerPlayer ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "random-cops-cap", ms_bRandomCopsCap ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "coop-side-missions", ms_bCoopSideMissions ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "auto-reconnect", ms_bAutoReconnect ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "clear-messages", ms_bClearMessages ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "gps-route", ms_bGpsRoute ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "cutscene-show-others", ms_bCutsceneShowOthers ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "hud-hide-all-f2", ms_bHudHideAllF2 ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "camera-mirror-interior-guard",
        ms_bCameraMirrorInteriorGuard ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "skip-death-arrest-help", ms_bSkipDeathArrestHelp ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "garage-sync", ms_bGarageSync ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "coop-mission-machines", ms_bCoopMissionMachines ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "high-stakes-coop", ms_bHighStakesCoop ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "high-stakes-guest-notice", ms_bHighStakesGuestNotice ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "server-clothes-restore", ms_bServerClothesRestore ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "guest-clear-area-keep-car", ms_bGuestClearAreaKeepCar ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "high-stakes-no-grid-wait", ms_bHighStakesNoGridWait ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "high-stakes-guest-blips", ms_bHighStakesGuestBlips ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "high-stakes-board", ms_bHighStakesBoard ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "high-stakes-hide-native-hud", ms_bHighStakesHideNativeHud ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "high-stakes-finish-notice", ms_bHighStakesFinishNotice ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "high-stakes-end-cleanup", ms_bHighStakesEndCleanup ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "high-stakes-passenger", ms_bHighStakesPassenger ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "high-stakes-seat-presence", ms_bHighStakesSeatPresence ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "high-stakes-restore-seat", ms_bHighStakesRestoreSeat ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "cutscene-keep-seat", ms_bCutsceneKeepSeat ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "high-stakes-team-result", ms_bHighStakesTeamResult ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "native-markers", ms_bNativeMarkers ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "mission-hunters", ms_bMissionHunters ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "mirror-blips", ms_bMirrorBlips ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "mission-sync-rules", ms_bMissionSyncRules ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "intro-board", ms_bIntroBoard ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "intro-independent", ms_bIntroIndependent ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "native-hints", ms_bNativeHints ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-wait-notices", ms_bMissionWaitNotices ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "sync-local-text-vars", ms_bSyncLocalTextVars ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-wait-same-place", ms_bMissionWaitSamePlace ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-wait-on-entry", ms_bMissionWaitOnEntry ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-wait-scenes", ms_bMissionWaitScenes ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-scene-prelude", ms_bMissionScenePrelude ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-hold-on-entry", ms_bMissionHoldOnEntry ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-entry-no-teleport", ms_bMissionEntryNoTeleport ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-no-auto-board", ms_bMissionNoAutoBoard ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-scene-skip-no-control", ms_bMissionSceneSkipNoControl ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-hold-keep-tags", ms_bMissionHoldKeepTags ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "cutscene-table-fallback", ms_bCutsceneTableFallback ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "intro-follow-host", ms_bIntroFollowHost ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mirror-blips-clear-on-end", ms_bMirrorBlipsClearOnEnd ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-car-overlap-dedupe", ms_bMissionCarOverlapDedupe ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "phone-call-keeps-onmission", ms_bPhoneCallKeepsOnMission ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "scene-guest-end-near-host", ms_bSceneGuestEndNearHost ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-guest-train", ms_bMissionGuestTrain ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-shop-one-at-a-time", ms_bMissionShopOneAtATime ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-any-player-in-car", ms_bMissionAnyPlayerInCar ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-ally-no-guest-damage", ms_bMissionAllyNoGuestDamage ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-ally-follow-area", ms_bMissionAllyFollowArea ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-any-player-trigger", ms_bMissionAnyPlayerTrigger ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-player-equipment", ms_bMissionPlayerEquipment ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "remote-bullet-no-force", ms_bRemoteBulletNoForce ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "ped-damage-authority", ms_bPedDamageAuthority ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "npc-driveby-sync", ms_bNpcDriveBySync ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "npc-aim-sync", ms_bNpcAimSync ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "npc-velocity-sync", ms_bNpcVelocitySync ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "npc-anim-mirror", ms_bNpcAnimMirror ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "player-anim-mirror", ms_bPlayerAnimMirror ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "guest-car-blip-hide", ms_bGuestCarBlipHide ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "chat-tpm-fijar", ms_bChatTpmFijar ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "player-list-coopsa", ms_bPlayerListCoopSa ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "emergency-dedupe", ms_bEmergencyDedupe ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "spawn-watch", ms_bSpawnWatch ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "player-jack-native", ms_bPlayerJackNative ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-hood-door-open", ms_bMissionHoodDoorOpen ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "guest-story-launchers", ms_bGuestStoryLaunchers ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-nines-far-boarding", ms_bMissionNinesFarBoarding ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-nines-board-near", ms_bMissionNinesBoardNear ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-equipment-script-ammo", ms_bMissionEquipmentScriptAmmo ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-guest-weapon-parity", ms_bMissionGuestWeaponParity ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "recruit-board-passenger", ms_bRecruitBoardPassenger ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-audio-guard", ms_bMissionAudioGuard ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-nines-ammo-repair", ms_bMissionNinesAmmoRepair ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-shared-on-screen", ms_bMissionSharedOnScreen ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-wide-car-on-screen", ms_bMissionWideCarOnScreen ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-scripted-boarding", ms_bMissionScriptedBoarding ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-og-loc-guest-car", ms_bMissionOgLocGuestCar ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-og-loc-bikes", ms_bMissionOgLocBikes ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-drive-thru-guest-car", ms_bMissionDriveThruGuestCar ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "coop-drive-by", ms_bCoopDriveBy ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "coop-los-sepulcros", ms_bCoopLosSepulcros ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "coop-running-dog", ms_bCoopRunningDog ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "coop-wrong-side-tracks", ms_bCoopWrongSideTracks ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "coop-robbing-uncle-sam", ms_bCoopRobbingUncleSam ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "coop-robbing-guest-deliver", ms_bCoopRobbingGuestDeliver ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "object-moving-nocollide", ms_bObjectMovingNoCollide ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-guest-car-board", ms_bMissionGuestCarBoard ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-drive-thru-first-car", ms_bMissionDriveThruFirstCar ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-trip-skip-guests", ms_bMissionTripSkipGuests ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-catalyst-guest-car", ms_bMissionCatalystGuestCar ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "coop-just-business", ms_bCoopJustBusiness ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "coop-madd-dogg", ms_bCoopMaddDogg ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "coop-gray-imports", ms_bCoopGrayImports ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "coop-green-sabre", ms_bCoopGreenSabre ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "coop-og-loc-escape", ms_bCoopOgLocEscape ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "coop-badlands", ms_bCoopBadlands ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "coop-countryside", ms_bCoopCountryside ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "vehicle-damage-any-player", ms_bVehicleDamageAnyPlayer ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "coop-car-gunners", ms_bCoopCarGunners ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-boarding-shared-car", ms_bMissionBoardingSharedCar ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-no-midcar", ms_bMissionNoMidCar ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-wait-pause-timer", ms_bMissionWaitPauseTimer ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "coop-og-loc-speed", ms_bCoopOgLocSpeed ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "vs-race-board", ms_bVsRaceBoard ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "vs-lobby-helpbox", ms_bVsLobbyHelpBox ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "voice-mic-volume", std::to_string(ms_nVoiceMicVolume).c_str(), ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "voice-out-volume", std::to_string(ms_nVoiceOutVolume).c_str(), ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "voice-toggle", ms_bVoiceToggle ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "voice-mic-device", ms_szVoiceMic, ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "coop-management-issues", ms_bCoopManagementIssues ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-guest-car-copies", ms_bMissionGuestCarCopies ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-scripted-exit", ms_bMissionScriptedExit ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "mirror-pickups", ms_bMirrorPickups ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "mission-pickup-weapon-share", ms_bMissionPickupWeaponShare ? "true" : "false",
        ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "patient-handover", ms_bPatientHandover ? "true" : "false", ms_sDataPath.c_str());
    // The same switches: write them so an existing coopandreas.ini is completed with the value
    // it was already using (Load's default) instead of leaving them invisible.
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "activity-challenge", ms_bActivityChallenge ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "ambulance-guest-patients", ms_bAmbulanceGuestPatients ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "cheat-announce", ms_bCheatAnnounce ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "cheat-menu-all", ms_bCheatMenuAll ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "contacts-follow-host", ms_bContactsFollowHost ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "story-help-ask", ms_bStoryHelpOptional ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "coop-dance", ms_bCoopDance ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "coop-home-invasion", ms_bCoopHomeInvasion ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "coop-lowrider", ms_bCoopLowrider ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "coop-photos", ms_bCoopPhotos ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "cutscene-hide-extras", ms_bCutsceneHideExtras ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "cutscene-black-bars", ms_bCutsceneBlackBars ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "cutscene-partner-smooth", ms_bCutscenePartnerSmooth ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "cutscene-partners", ms_bCutscenePartners ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "dance-guest-story", ms_bDanceGuestStory ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "dance-side-by-side", ms_bDanceSideBySide ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "dance-team-score", ms_bDanceTeamScore ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "emotes", ms_bEmotes ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "fade-guard", ms_bFadeGuard ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "just-business-bike-escort", ms_bJustBusinessBikeEscort ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "just-business-no-marker-wait", ms_bJustBusinessNoMarkerWait ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "lowrider-keep-car", ms_bLowriderKeepCar ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "menus-estilo-sa", ms_bMenusEstiloSa ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "mission-audio-zones", ms_bMissionAudioZones ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "mission-guest-actions", ms_bMissionGuestActions ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "mission-guest-car-host-reach", ms_bMissionGuestCarHostReach ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "mission-guest-car-sections", ms_bMissionGuestCarSections ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "mission-guest-counts", ms_bMissionGuestCounts ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "mission-guest-vehicles-self-board", ms_bMissionGuestVehiclesSelfBoard ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "mission-host-driver-hint", ms_bMissionHostDriverHint ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "mission-shop-clerk-fail", ms_bShopClerkFail ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "music1-beach-arrival", ms_bMusic1BeachArrival ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "music1-garage-host-driver", ms_bMusic1GarageHostDriver ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "own-static-blips", ms_bOwnStaticBlips ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "ped-damage-any-player", ms_bPedDamageAnyPlayer ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "perf-telemetry", ms_bPerfTelemetry ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "player-full-rebuild", ms_bPlayerFullRebuild ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "pool-two-players", ms_bPoolTwoPlayers ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "recruit-all-players", ms_bRecruitAllPlayers ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "recruit-dismiss-sync", ms_bRecruitDismissSync ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "remote-boat-sync", ms_bRemoteBoatSync ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "remote-heli-rotor-sync", ms_bRemoteHeliRotorSync ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "remote-horn-sync", ms_bRemoteHornSync ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "remote-revive-auto", ms_bRemoteReviveAuto ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "scene-guests-visible", ms_bSceneGuestsVisible ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "teleport-load-interior", ms_bTeleportLoadInterior ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "train-sync", ms_bTrainSync ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "train-sync-rail", ms_bTrainSyncRail ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "train-sync-keep", ms_bTrainSyncKeep ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "voice-chat", ms_bVoiceChat ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "vs-race-sync", ms_bVsRaceSync ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "weapon-model-release", ms_bWeaponModelRelease ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(
        CONFIG_SECTION.c_str(), "anim-destroy-notify", ms_bAnimDestroyNotify ? "true" : "false", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "settings-version", "1", ms_sDataPath.c_str());
    WritePrivateProfileString(CONFIG_SECTION.c_str(), "cheat-menu-key",
        ("F" + std::to_string(CCheatMenu::ms_nToggleKey - VK_F1 + 1)).c_str(), ms_sDataPath.c_str());
    // server-client-config: an imposed switch is not this player's choice: the file keeps his own value
    for (const auto& [key, fileValue] : ms_umOverriddenFileValues)
        WritePrivateProfileString(CONFIG_SECTION.c_str(), key.c_str(), fileValue == "\x01" ? nullptr : fileValue.c_str(),
            ms_sDataPath.c_str());
}
