// Remember a pool handle, never an unreferenced vehicle pointer.
static int nGuestCarRef = -1;
static CRunningScript* pGuestCarScript = nullptr;
static int nGuestCarMission = -1;

// T97: requiresHostCar scripts whose original block is known exclude only these sections (offsets from the mission's
// start, equal to the labels of scm/scripts/*.txt). Block: offset in the original main.scm, size and FNV-1a, like
// CMissionGuestActions; anything else (other main.scm, coop scripts) keeps the whole-script exclusion.
struct SHostCarRange { uint32_t from, to; };
struct SHostCarSections { const char* name; uint32_t start, length, hash; SHostCarRange ranges[2]; int count; };
static const SHostCarSections hostCarSections[] = {
    // MUSIC1_14856..15131: beach arrival scene, turns the car the host is in (T12 MUSIC1.txt:2373/2381)
    {"MUSIC1", 0x0ECBFF, 15958, 0xD17035CC, {{14856, 15131}}, 1},
    // SMOKE3_10370..16921: the train chase, stage 80@ == 2 (drive-by and distances to the train, T12 SMOKE3.txt:1535/1578)
    {"SMOKE3", 0x0DABCA, 20874, 0x03FA2446, {{10370, 16921}}, 1},
};

static const SHostCarSections* FindHostCarSections(CRunningScript* script)
{
    static int cachedMission = -2;
    static const SHostCarSections* cachedEntry = nullptr;
    static bool cachedMatch = false;
    if (!script->m_bIsMission || script->m_pBaseIP != reinterpret_cast<uint8_t*>(CTheScripts::MissionBlock))
        return nullptr;
    const int mission = CMissionMirror::GetRunningMissionId();
    for (const auto& entry : hostCarSections)
    {
        if (_strnicmp(script->m_szName, entry.name, 8) != 0)
            continue;
        if (cachedMission == mission && cachedEntry == &entry)
            return cachedMatch ? &entry : nullptr;
        uint32_t hash = 2166136261u;
        for (uint32_t i = 0; i < entry.length; ++i)
            hash = (hash ^ script->m_pBaseIP[i]) * 16777619u;
        CMissionMirror::LogEventf("rule-guest-car-sections script=%.8s match=%d", entry.name, hash == entry.hash ? 1 : 0);
        cachedMission = mission;
        cachedEntry = &entry;
        cachedMatch = hash == entry.hash;
        return cachedMatch ? &entry : nullptr;
    }
    return nullptr;
}

// T97 MUSIC1: LOCATE_CHAR_ANY_MEANS_2D($scplayer, 34@ 35@ = 526.84 -1885.19, 50 x 50) at MUSIC1_3053 starts the arrival
// scene (MUSIC1_14856: widescreen, fixed camera, MUS1_2, the host snapped to the ground). Not a marker: it started with
// the host alone and its snap gathered the guests that were still arriving.
static bool IsBeachArrival(CRunningScript* script, const SLocate& locate)
{
    return CConfigLoader::ms_bMusic1BeachArrival && _strnicmp(script->m_szName, "MUSIC1", 8) == 0 && !locate.bSphere &&
        fabsf(locate.radius - 50.0f) < 0.01f && fabsf(locate.center.x - 526.84f) < 0.5f &&
        fabsf(locate.center.y + 1885.19f) < 0.5f;
}

void ProcessBeachArrival()
{
    if (!bBeachArrived && !nBeachArrivalTick)
        return;
    for (auto script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
    {
        if (script->m_bIsMission && _strnicmp(script->m_szName, "MUSIC1", 8) == 0)
            return;
    }
    // a new attempt waits again
    bBeachArrived = false;
    nBeachArrivalTick = 0;
}

// T98: a vehicle the running mission script made itself (CTheScripts::MissionCleanUp, gta-reversed MissionCleanup.cpp):
// not a network copy of a guest's vehicle nor one this DLL parked (both are MISSION_VEHICLE too)


// the mission needs the host himself in its vehicle here: a guest driving it does not count
bool IsHostCarRequired(CRunningScript* script, const uint8_t* ip)
{
    // These scripts read the host's car or need him in a chase or a continuous scene.
    static const char* const requiresHostCar[] = {"CAT4", "GARAG1", "GUNS1", "MANSIO2", "RYDER2", "SYN1", "SYN2",
        "CASINO5", "CAT2", "DRUGS3", "FINALEA", "SCRASH3", "SYN7", "SMOKE2", "SMOKE3", "DRIV3", "DRIV6",
        "DES3", "MUSIC2", "HOODS5", "INTRO1", "DRIV2", "MUSIC1"};
    for (const char* name : requiresHostCar)
    {
        if (_strnicmp(script->m_szName, name, 8) != 0)
            continue;
        const SHostCarSections* sections =
            CConfigLoader::ms_bMissionGuestCarSections && CConfigLoader::ms_bVanillaScripts ? FindHostCarSections(script) : nullptr;
        if (!sections || !ip || ip < script->m_pBaseIP || ip >= script->m_pBaseIP + sections->length)
            return true;
        const uint32_t offset = static_cast<uint32_t>(ip - script->m_pBaseIP);
        for (int i = 0; i < sections->count; ++i)
        {
            if (offset >= sections->ranges[i].from && offset < sections->ranges[i].to)
                return true;
        }
        return false;
    }
    return false;
}

static bool CanUseGuestCar(CRunningScript* script, const uint8_t* ip)
{
    if (IsHostCarRequired(script, ip))
        return false;
    CPlayerPed* player = FindPlayerPed(0);
    return CMissionSyncRules::IsEnabled() && CConfigLoader::ms_bMissionAnyPlayerInCar &&
        CNetwork::m_bConnected && !COpCodeSync::bProcessingNetworkOpcode && player && player->m_fHealth > 0.0f &&
        CCoopMission::IsScriptAuthority() && script->m_bIsMission && COpCodeSync::IsScriptSynced(script) &&
        CMissionSyncRules::IsStoryScript(script) && !CMirrorBlips::IsPerPlayerMissionRunning();
}

static CNetworkPlayer* FindGuestInMissionCar(CVehicle* vehicle)
{
    CPlayerPed* player = FindPlayerPed(0);
    // gta-reversed Vehicle.h: MISSION_VEHICLE=2; CharacterCommands.cpp: IsCharInCar.
    if (!vehicle || vehicle->m_nCreatedBy != MISSION_VEHICLE || vehicle->m_fHealth <= 0.0f || !player ||
        vehicle->m_nAreaCode != player->m_nAreaCode)
        return nullptr;
    for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        CPed* ped = networkPlayer->m_pPed;
        if (CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer) && ped && ped->m_fHealth > 0.0f &&
            ped->m_nAreaCode == vehicle->m_nAreaCode && ped->m_nPedFlags.bInVehicle && ped->m_pVehicle == vehicle)
            return networkPlayer;
    }
    return nullptr;
}

static void ClearGuestCar()
{
    nGuestCarRef = -1;
    pGuestCarScript = nullptr;
    nGuestCarMission = -1;
}

void ProcessGuestCar()
{
    if (!pGuestCarScript)
        return;
    for (CRunningScript* script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
    {
        if (script == pGuestCarScript && CanUseGuestCar(script, script->m_pCurrentIP) &&
            nGuestCarMission == CMissionMirror::GetRunningMissionId() &&
            FindGuestInMissionCar(CPools::GetVehicle(nGuestCarRef)))
            return;
    }
    ClearGuestCar();
}

// T98: the 2026-11-11 test had guests driving mission cars and not one rule-any-player-in-car: when a guest sits in the
// car IS_CHAR_IN_CAR / IS_CHAR_SITTING_IN_CAR asks about for the host and the rule does not take it, log why
static void LogGuestCarMiss(CRunningScript* script, uint16_t opcode)
{
    CPlayerPed* player = FindPlayerPed(0);
    CVehicle* vehicle = CPools::GetVehicle(ScriptParams[1]);
    if (!player || !vehicle || ScriptParams[0] != CPools::GetPedRef(player) || !script->m_bIsMission ||
        COpCodeSync::bProcessingNetworkOpcode)
        return;
    for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        CPed* ped = networkPlayer->m_pPed;
        if (!ped || !ped->m_nPedFlags.bInVehicle || ped->m_pVehicle != vehicle)
            continue;
        const uint8_t* ip = CommandHooks::GetCurrentCommandIP();
        if (CanUseGuestCar(script, ip) && FindGuestInMissionCar(vehicle))
            return;
        static DWORD nLastMiss = 0;
        const DWORD now = GetTickCount();
        if (now - nLastMiss < 5000)
            return;
        nLastMiss = now;
        CMissionMirror::LogEventf("rule-guest-car-miss script=%.8s opcode=%04X ip=%d host-car=%d rules=%d any-car=%d "
            "auth=%d synced=%d story=%d created=%d veh-area=%d ped-area=%d host-area=%d include=%d hp=%.0f driver=%d",
            script->m_szName, opcode, ip && ip >= script->m_pBaseIP ? (int)(ip - script->m_pBaseIP) : -1,
            IsHostCarRequired(script, ip) ? 1 : 0, CMissionSyncRules::IsEnabled() ? 1 : 0,
            CConfigLoader::ms_bMissionAnyPlayerInCar ? 1 : 0, CCoopMission::IsScriptAuthority() ? 1 : 0,
            COpCodeSync::IsScriptSynced(script) ? 1 : 0, CMissionSyncRules::IsStoryScript(script) ? 1 : 0,
            (int)vehicle->m_nCreatedBy, (int)vehicle->m_nAreaCode, (int)ped->m_nAreaCode, (int)player->m_nAreaCode,
            CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer) ? 1 : 0, ped->m_fHealth,
            vehicle->m_pDriver == ped ? 1 : 0);
        return;
    }
    // testeo 13-11 (every mission): no guest in the asked car here. A guest seen in another car or on foot right by it
    // means his entry did not reach this game as that same car
    const CVector carPos = vehicle->GetPosition();
    for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        CPed* ped = networkPlayer->m_pPed;
        if (!ped || ped->m_fHealth <= 0.0f)
            continue;
        CVehicle* other = ped->m_nPedFlags.bInVehicle ? ped->m_pVehicle : nullptr;
        const CVector pos = other ? other->GetPosition() : ped->GetPosition();
        // a guest in a car of the same model anywhere: the parked guest copy (same model and colours) taken for the
        // mission car; the script asks about the original, which stays where it was
        if (other && other != vehicle && other->m_nModelIndex == vehicle->m_nModelIndex)
        {
            static DWORD nLastTwin = 0;
            const DWORD now = GetTickCount();
            if (now - nLastTwin < 5000)
                return;
            nLastTwin = now;
            CMissionMirror::LogEventf("rule-guest-car-twin script=%.8s opcode=%04X car=%d guest=%s other=%d other-created=%d "
                "driver=%d dist=%.1f", script->m_szName, opcode, ScriptParams[1], networkPlayer->GetName().c_str(),
                CPools::GetVehicleRef(other), (int)other->m_nCreatedBy, other->m_pDriver == ped ? 1 : 0,
                (pos - carPos).Magnitude());
            return;
        }
        if ((pos - carPos).Magnitude() > 8.0f)
            continue;
        static DWORD nLastNear = 0;
        const DWORD now = GetTickCount();
        if (now - nLastNear < 5000)
            return;
        nLastNear = now;
        CMissionMirror::LogEventf("rule-guest-car-near script=%.8s opcode=%04X car=%d created=%d guest=%s in-vehicle=%d "
            "other=%d other-created=%d dist=%.1f include=%d", script->m_szName, opcode, ScriptParams[1],
            (int)vehicle->m_nCreatedBy, networkPlayer->GetName().c_str(), ped->m_nPedFlags.bInVehicle ? 1 : 0,
            other ? CPools::GetVehicleRef(other) : -1, other ? (int)other->m_nCreatedBy : -1, (pos - carPos).Magnitude(),
            CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer) ? 1 : 0);
        return;
    }
}

// A marker of the mission on the authority's player that he has not reached yet while a guest already stands in it: the
// authority learns he is the one awaited (every story mission's markers, like the start markers of the launchers)
static void NotifyAwaitedAuthority(CRunningScript* script, uint16_t opcode)
{
    SLocate locate;
    if (opcode == 0x03EE || !CMissionHints::WaitNoticesEnabled() || !script->m_bIsMission || !CNetwork::m_bConnected ||
        COpCodeSync::bProcessingNetworkOpcode || !CCoopMission::IsScriptAuthority() || !COpCodeSync::IsScriptSynced(script) ||
        !CMissionSyncRules::IsStoryScript(script) || CMirrorBlips::IsPerPlayerMissionRunning() ||
        !ReadLocate(opcode, locate) || !locate.bSphere || locate.bCarSelf)
        return;
    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (!localPlayer || ScriptParams[0] != CPools::GetPedRef(localPlayer))
        return;
    for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        CPed* ped = networkPlayer->m_pPed;
        if (!ped || networkPlayer->m_bDead || ped->m_fHealth <= 0.0f || !CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer) ||
            ped->m_nAreaCode != localPlayer->m_nAreaCode)
            continue;
        const CVector pos = ped->m_nPedFlags.bInVehicle && ped->m_pVehicle ? ped->m_pVehicle->GetPosition() : ped->GetPosition();
        const float dx = pos.x - locate.center.x, dy = pos.y - locate.center.y, reach = locate.radius + 1.5f;
        if (dx * dx + dy * dy > reach * reach ||
            (locate.b3D && fabsf(pos.z - locate.center.z) > (std::max)(locate.height, 2.0f) + 2.0f))
            continue;
        CMissionHints::AuthorityAwaited(Packets::Scripts::MissionWaitNotice::MARKER);
        static DWORD nLastLog = 0;
        if (GetTickCount() - nLastLog > 5000)
        {
            nLastLog = GetTickCount();
            CMissionMirror::LogEventf("rule-host-awaited opcode=%04X at=%.1f,%.1f guest=%s", opcode, locate.center.x,
                locate.center.y, networkPlayer->GetName().c_str());
        }
        return;
    }
}

static bool FilterGuestCar(CRunningScript* script, uint16_t opcode, bool state)
{
    if (!state && (opcode == 0x00DB || opcode == 0x0448))
        LogGuestCarMiss(script, opcode);
    if (!CanUseGuestCar(script, CommandHooks::GetCurrentCommandIP()) || ScriptParams[0] != CPools::GetPedRef(FindPlayerPed(0)))
        return state;

    CVehicle* vehicle = nullptr;
    CNetworkPlayer* occupant = nullptr;
    if (opcode == 0x00DB || opcode == 0x0448)
    {
        vehicle = CPools::GetVehicle(ScriptParams[1]);
        occupant = FindGuestInMissionCar(vehicle);
        if (!state && occupant)
        {
            nGuestCarRef = ScriptParams[1];
            pGuestCarScript = script;
            nGuestCarMission = CMissionMirror::GetRunningMissionId();
        }
    }
    else
    {
        bool b3D = false, bStopped = false, bArea = false;
        bool bFixed = true;
        CEntity* target = nullptr;
        switch (opcode)
        {
        case 0x00EC:  // LOCATE_CHAR_ANY_MEANS_2D
        case 0x00EE: break;  // LOCATE_CHAR_IN_CAR_2D
        case 0x00EF:
        case 0x00F1: bStopped = true; break;
        case 0x00FE:
        case 0x0100: b3D = true; break;
        case 0x0101:
        case 0x0103: b3D = true; bStopped = true; break;
        case 0x00A3:
        case 0x01A2: bArea = true; break;
        case 0x01A3:
        case 0x01A5: bArea = true; bStopped = true; break;
        case 0x00A4:
        case 0x01A7: bArea = true; b3D = true; break;
        case 0x01A8:
        case 0x01AA: bArea = true; b3D = true; bStopped = true; break;
        case 0x00F2:
        case 0x00F4: target = CPools::GetPed(ScriptParams[1]); bFixed = false; break;
        case 0x0104:
        case 0x0106: target = CPools::GetPed(ScriptParams[1]); bFixed = false; b3D = true; break;
        case 0x0202:
        case 0x0204: target = CPools::GetVehicle(ScriptParams[1]); bFixed = false; break;
        case 0x0205:
        case 0x0207: target = CPools::GetVehicle(ScriptParams[1]); bFixed = false; b3D = true; break;
        case 0x0471:
        case 0x0473: target = CPools::GetObject(ScriptParams[1]); bFixed = false; break;
        case 0x0474:
        case 0x0476: target = CPools::GetObject(ScriptParams[1]); bFixed = false; b3D = true; break;
        default: return state;
        }
        if (state || pGuestCarScript != script || nGuestCarMission != CMissionMirror::GetRunningMissionId())
            return state;
        vehicle = CPools::GetVehicle(nGuestCarRef);
        occupant = FindGuestInMissionCar(vehicle);
        if (!occupant)
        {
            ClearGuestCar();
            return state;
        }
        // Native LOCATE uses independent inclusive axis bounds; sphere only draws the marker.
        if (!bFixed && (!target || target->m_nAreaCode != vehicle->m_nAreaCode))
            return state;
        const float* f = reinterpret_cast<const float*>(&ScriptParams[0]);
        const CVector pos = vehicle->GetPosition();
        if (bArea)
        {
            // CharInAreaCheckCommand (0x488B50) normalizes both corners, inclusively.
            const int end = b3D ? 4 : 3;
            if (!(pos.x >= (std::min)(f[1], f[end]) && pos.x <= (std::max)(f[1], f[end]) &&
                  pos.y >= (std::min)(f[2], f[end + 1]) && pos.y <= (std::max)(f[2], f[end + 1]) &&
                  (!b3D || (pos.z >= (std::min)(f[3], f[6]) && pos.z <= (std::max)(f[3], f[6])))))
                return state;
        }
        else
        {
            const CVector center = bFixed ? CVector(f[1], f[2], b3D ? f[3] : 0.0f) : target->GetPosition();
            const int radius = bFixed ? (b3D ? 4 : 3) : 2;
            if (!(pos.x >= center.x - f[radius] && pos.x <= center.x + f[radius] &&
                  pos.y >= center.y - f[radius + 1] && pos.y <= center.y + f[radius + 1] &&
                  (!b3D || (pos.z >= center.z - f[radius + 2] && pos.z <= center.z + f[radius + 2]))))
                return state;
        }
        if (bStopped && !CTheScripts::IsPedStopped(occupant->m_pPed))
            return state;
    }
    if (state || !occupant)
        return state;
    static DWORD nLastLog = 0;
    const DWORD now = GetTickCount();
    if (now - nLastLog >= 5000)
    {
        nLastLog = now;
        CMissionMirror::LogEventf("rule-any-player-in-car opcode=%04X vehicle=%d player=%s", opcode,
            CPools::GetVehicleRef(vehicle), occupant->GetName().c_str());
    }
    return true;
}

