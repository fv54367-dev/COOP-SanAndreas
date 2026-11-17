// The locate and area conditions a mission checks the player (or his vehicle) with, read from the collected parameters
// (sa_sbl order: self first, the drawSphere flag last)
struct SLocate
{
    bool bCarSelf = false;  // the first parameter is a vehicle
    bool b3D = false;
    bool bSphere = false;   // drawSphere: the game draws a marker there
    bool bFixedPlace = false;  // a coordinate or an area, not around another entity
    CVector center;
    float radius = 0.0f;
    float height = 0.0f;
};

static CVector EntityPosition(int ref, bool bCar)
{
    CEntity* pEntity = bCar ? static_cast<CEntity*>(CPools::GetVehicle(ref)) : static_cast<CEntity*>(CPools::GetPed(ref));
    return pEntity ? pEntity->GetPosition() : CVector(0.0f, 0.0f, -1000.0f);
}

static bool ReadLocate(uint16_t opcode, SLocate& locate)
{
    const float* f = reinterpret_cast<const float*>(&ScriptParams[0]);
    auto coord = [&](bool b3D, int sphere) {
        locate.b3D = b3D;
        locate.bFixedPlace = true;
        locate.center = CVector(f[1], f[2], b3D ? f[3] : 0.0f);
        locate.radius = (std::max)(f[b3D ? 4 : 3], f[b3D ? 5 : 4]);
        locate.height = b3D ? f[6] : 0.0f;
        locate.bSphere = ScriptParams[sphere] != 0;
    };
    auto entity = [&](bool b3D, bool bTargetCar, int sphere) {
        locate.b3D = b3D;
        locate.center = EntityPosition(ScriptParams[1], bTargetCar);
        locate.radius = (std::max)(f[2], f[3]);
        locate.height = b3D ? f[4] : 0.0f;
        locate.bSphere = ScriptParams[sphere] != 0;
    };
    auto area = [&](bool b3D, int sphere) {
        const int x2 = b3D ? 4 : 3;
        locate.bFixedPlace = true;
        locate.b3D = b3D;
        locate.center = CVector((f[1] + f[x2]) * 0.5f, (f[2] + f[x2 + 1]) * 0.5f, b3D ? (f[3] + f[6]) * 0.5f : 0.0f);
        locate.radius = (std::max)(fabsf(f[x2] - f[1]), fabsf(f[x2 + 1] - f[2])) * 0.5f;
        locate.height = b3D ? fabsf(f[6] - f[3]) * 0.5f : 0.0f;
        locate.bSphere = ScriptParams[sphere] != 0;
    };

    if (opcode >= 0x00EC && opcode <= 0x00F1)  // LOCATE_(STOPPED_)CHAR_*_2D
        coord(false, 5);
    else if (opcode >= 0x00FE && opcode <= 0x0103)  // LOCATE_(STOPPED_)CHAR_*_3D
        coord(true, 7);
    else if (opcode >= 0x00F2 && opcode <= 0x00F4)  // LOCATE_CHAR_*_CHAR_2D
        entity(false, false, 4);
    else if (opcode >= 0x0104 && opcode <= 0x0106)  // LOCATE_CHAR_*_CHAR_3D
        entity(true, false, 5);
    else if (opcode >= 0x0202 && opcode <= 0x0204)  // LOCATE_CHAR_*_CAR_2D
        entity(false, true, 4);
    else if (opcode >= 0x0205 && opcode <= 0x0207)  // LOCATE_CHAR_*_CAR_3D
        entity(true, true, 5);
    else if (opcode == 0x0471 || opcode == 0x0473 || opcode == 0x0474 || opcode == 0x0476)
    {
        const bool b3D = opcode == 0x0474 || opcode == 0x0476;
        locate.b3D = b3D;
        CObject* object = CPools::GetObject(ScriptParams[1]);
        locate.center = object ? object->GetPosition() : CVector(0.0f, 0.0f, -1000.0f);
        locate.radius = (std::max)(f[2], f[3]);
        locate.height = b3D ? f[4] : 0.0f;
        locate.bSphere = ScriptParams[b3D ? 5 : 4] != 0;
    }
    else if (opcode == 0x00A3 || (opcode >= 0x01A1 && opcode <= 0x01A5))  // IS_CHAR_(STOPPED_)IN_AREA_*_2D
        area(false, 5);
    else if (opcode == 0x00A4 || (opcode >= 0x01A6 && opcode <= 0x01AA))  // IS_CHAR_(STOPPED_)IN_AREA_*_3D
        area(true, 7);
    else if (opcode == 0x01AD || opcode == 0x01AE)  // LOCATE_(STOPPED_)CAR_2D
    {
        locate.bCarSelf = true;
        coord(false, 5);
    }
    else if (opcode == 0x01AF || opcode == 0x01B0)  // LOCATE_(STOPPED_)CAR_3D
    {
        locate.bCarSelf = true;
        coord(true, 7);
    }
    else if (opcode == 0x00B0 || opcode == 0x01AB)  // IS_CAR_(STOPPED_)IN_AREA_2D
    {
        locate.bCarSelf = true;
        area(false, 5);
    }
    else if (opcode == 0x00B1 || opcode == 0x01AC)  // IS_CAR_(STOPPED_)IN_AREA_3D
    {
        locate.bCarSelf = true;
        area(true, 7);
    }
    else
        return false;
    return true;
}

// the players of the mission in another place than the authority: another interior, or outside (names for the chat)
static bool FindPlayersInOtherPlace(unsigned int areaCode, std::string& missing, bool ignoreFallen = false)
{
    for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (!networkPlayer->m_pPed ||
            (ignoreFallen && (networkPlayer->m_bDead || networkPlayer->m_pPed->m_fHealth <= 0.0f)) ||
            !CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer) || networkPlayer->m_pPed->m_nAreaCode == areaCode)
            continue;
        if (!missing.empty())
            missing += ", ";
        missing += networkPlayer->GetName();
    }
    return !missing.empty();
}

static bool bEntryWindow = false;

// Recover group followers missed by the native entry/exit warp (EntryExit.cpp, 0x43F1F0).
void ProcessAllyFollowArea()
{
    static int missionId = -1, previousArea = -1, oldArea = -1;
    static int enteredArea = -1, enteredMission = -1;
    static unsigned int changedAt = 0;
    static bool pending = false;
    static std::vector<int> moved, requested;
    CPlayerPed* player = FindPlayerPed(0);
    const int currentMission = CMissionMirror::GetRunningMissionId();
    const bool eligible = CMissionSyncRules::IsEnabled() && CConfigLoader::ms_bMissionAllyFollowArea &&
        CNetwork::m_bConnected && !COpCodeSync::bProcessingNetworkOpcode && CCoopMission::IsScriptAuthority() &&
        !CCoopMission::IsLocalIndividualMission() && !CCoopMission::HasLocalLeft() &&
        !CMirrorBlips::IsPerPlayerMissionRunning() && currentMission >= 0 && IsStoryMissionRunning() &&
        player && player->m_fHealth > 0.0f && CWorld::Players[0].m_nPlayerState != PLAYERSTATE_HASDIED &&
        CWorld::Players[0].m_nPlayerState != PLAYERSTATE_HASBEENARRESTED;
    if (!eligible || missionId != currentMission)
    {
        missionId = eligible ? currentMission : -1;
        previousArea = eligible ? player->m_nAreaCode : -1;
        enteredArea = enteredMission = -1;
        pending = false;
        moved.clear();
        requested.clear();
        return;
    }
    const int area = player->m_nAreaCode;
    const unsigned int now = CTimer::m_snTimeInMilliseconds;
    if (previousArea != area)
    {
        oldArea = previousArea;
        previousArea = area;
        changedAt = now;
        // Same gate as the native warp: CEntryExit::ms_spawnPoint (0x96A7C0), bAcceptNpcGroup (EntryExit.cpp:500).
        CEntryExit* spawnPoint = *reinterpret_cast<CEntryExit**>(0x96A7C0);
        const bool returning = area == 0 && oldArea == enteredArea && enteredMission == currentMission;
        enteredArea = enteredMission = -1;
        pending = returning || (spawnPoint && spawnPoint->m_nFlags.bAcceptNpcGroup && spawnPoint->m_nArea == area);
        CPedGroup* group = CPedGroups::GetPedsGroup(player);
        CMissionMirror::LogEventf("rule-ally-follow-area change from=%d to=%d pending=%d spawn=%d group=%d",
            oldArea, area, pending ? 1 : 0, spawnPoint ? static_cast<int>(spawnPoint->m_nArea) : -1,
            group && group->m_groupMembership.IsLeader(player) ? 1 : 0);
        moved.clear();
        requested.clear();
    }
    if (!pending || now - changedAt >= 3000)
    {
        pending = false;
        return;
    }
    CPedGroup* group = CPedGroups::GetPedsGroup(player);
    if (!group || !group->m_groupMembership.IsLeader(player))
        return;
    for (int slot = 0; slot < 7; ++slot)
    {
        CPed* ped = group->m_groupMembership.GetMember(slot);
        if (!ped || !IsPedPointerValid(ped) || !ped->IsVTableValid() || !ped->m_matrix ||
            ped->IsPlayer() || ped->m_fHealth <= 0.0f || ped->m_nPedFlags.bInVehicle)
            continue;
        const int ref = CPools::GetPedRef(ped);
        if (std::find(moved.begin(), moved.end(), ref) != moved.end())
            continue;
        if (ped->m_nAreaCode != oldArea && DistanceBetweenPoints(ped->GetPosition(), player->GetPosition()) <= 30.0f)
            continue;
        CNetworkPed* networkPed = CNetworkPedManager::GetPed(ped);
        if (networkPed && !networkPed->m_bSyncing)
        {
            if (std::find(requested.begin(), requested.end(), ref) == requested.end())
            {
                Packets::Peds::PedTakeHost packet{};
                packet.pedid = networkPed->m_pedId;
                packet.allowReturnToPreviousHost = false;
                GetPacketFactory().Send(packet);
                requested.push_back(ref);
                CMissionMirror::LogEventf("rule-ally-follow-area claim ped=%d from=%d to=%d", ref, oldArea, area);
            }
            continue; // Wait for AssignPedSyncer: the previous owner must stop sending positions first.
        }
        moved.push_back(ref);
        // In front of the player: right after a transition the door is behind him (native check: IsTeleportPointValid).
        CVector destination = player->TransformFromObjectSpace(CVector((slot % 3 - 1) * 1.5f, 2.0f + slot / 3, 0.0f));
        if (!CWorld::GetIsLineOfSightClear(player->GetPosition(), destination, true, false, false, true, false, false, false))
            destination = player->GetPosition();
        ped->Teleport(destination, false);
        ped->m_pIntelligence->FlushImmediately(false);
        // SetAreaCode is a field assignment in gta-reversed Entity.h; SDK exposes the field.
        ped->m_nAreaCode = player->m_nAreaCode;
        ped->m_pEnex = player->m_pEnex;
        if (area != 0)
        {
            enteredArea = area;
            enteredMission = currentMission;
        }
        if (networkPed)
        {
            networkPed->m_nAreaCode = player->m_nAreaCode;
            networkPed->m_vecLastPos = destination;
            networkPed->m_bHasPosError = false;
        }
        CMissionMirror::LogEventf("rule-ally-follow-area warp ped=%d from=%d to=%d", ref, oldArea, area);
    }
}

void ProcessEntryWindow()
{
    static int missionId = -1;
    static int previousArea = -1;
    static unsigned int enteredAt = 0;
    CPlayerPed* player = FindPlayerPed(0);
    const int currentMission = CMissionMirror::GetRunningMissionId();
    const bool eligible = CMissionSyncRules::IsEnabled() && CConfigLoader::ms_bMissionWaitOnEntry &&
        CNetwork::m_bConnected && !COpCodeSync::bProcessingNetworkOpcode && CCoopMission::IsScriptAuthority() &&
        !CMirrorBlips::IsPerPlayerMissionRunning() && currentMission >= 0 && IsStoryMissionRunning() &&
        player && player->m_fHealth > 0.0f && CWorld::Players[0].m_nPlayerState != PLAYERSTATE_HASDIED &&
        CWorld::Players[0].m_nPlayerState != PLAYERSTATE_HASBEENARRESTED && !CHomeInvasion::SuppressGather(nullptr);
    if (!eligible)
    {
        missionId = -1;
        previousArea = -1;
        bEntryWindow = false;
        return;
    }

    const int area = player->m_nAreaCode;
    const unsigned int now = CTimer::m_snTimeInMilliseconds;
    if (missionId != currentMission)
    {
        missionId = currentMission;
        previousArea = area;
        bEntryWindow = false;
    }
    if (previousArea != area)
    {
        // Native entry transitions set the player's area (gta-reversed EntryExit.cpp:407).
        bEntryWindow = area != 0;
        enteredAt = now;
        previousArea = area;
        if (bEntryWindow)
            CMissionMirror::LogEventf("rule-entry-open area=%d", area);
    }
    if (!bEntryWindow)
        return;

    std::string missing;
    if (!FindPlayersInOtherPlace(area, missing, true))
    {
        bEntryWindow = false;
        CMissionMirror::LogEventf("rule-entry-complete area=%d", area);
        return;
    }
    if (now - enteredAt < 30000)
        return;
    // the ones who are missing are walking in from just outside the door: wait for them (up to 2 minutes)
    if (now - enteredAt < 120000 && CMissionSceneWait::MissingAreComing())
        return;

    // Consume before sending: no retries while the remote area update is in flight.
    bEntryWindow = false;
    if (CConfigLoader::ms_bMissionEntryNoTeleport)
    {
        // every player uses the door himself; the hold on entry already waited for him
        CMissionMirror::LogEventf("rule-entry-timeout area=%d players=0 missing=%s no-teleport=1", area, missing.c_str());
        return;
    }
    CNetworkPlayer* players[MAX_TELEPORT_SLOTS];
    int count = 0;
    for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        CPed* ped = networkPlayer->m_pPed;
        if (count < MAX_TELEPORT_SLOTS && ped && !networkPlayer->m_bDead && ped->m_fHealth > 0.0f &&
            CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer) && ped->m_nAreaCode != area)
            players[count++] = networkPlayer;
    }
    CCommandTeleportPlayersToHostSafely::TeleportToHost(players, count);
    CMissionMirror::LogEventf("rule-entry-timeout area=%d players=%d missing=%s", area, count, missing.c_str());
}

// gta-reversed CharacterCommands.cpp:0102; native LocateCharCommand (0x486D80)
// collects 8 parameters and preserves them through UpdateCompareFlag (0x486FC2).
// Every shop counter uses this same 1x1x4 stopped-on-foot blob: barber chair, food, Ammu-Nation, clothes, tattoo, wardrobe.
static bool IsShopCounterScript(const CRunningScript* script)
{
    for (const char* name : {"BARB", "JFUD", "AMUNAT", "CLOTH", "TATTO", "WARROBE"})
        if (_strnicmp(script->m_szName, name, 8) == 0)
            return true;
    return false;
}

static bool FilterShopCounter(CRunningScript* script, bool state)
{
    static bool bWaitMessageShown = false;
    CPlayerPed* localPlayer = FindPlayerPed(0);
    const float* f = reinterpret_cast<const float*>(&ScriptParams[0]);
    if (!CConfigLoader::ms_bVanillaScripts || !CConfigLoader::ms_bMissionShopOneAtATime ||
        !CNetwork::m_bConnected || !state || script->m_bNotFlag || !localPlayer ||
        localPlayer->m_fHealth <= 0.0f || ScriptParams[0] != CPools::GetPedRef(localPlayer) ||
        f[4] != 1.0f || f[5] != 1.0f || f[6] != 4.0f || ScriptParams[7] == 0)
    {
        bWaitMessageShown = false;
        return state;
    }

    const CVector center(f[1], f[2], f[3]);
    if (!CCommandIsAnyNetworkPlayerInArea3D::IsAnyNetworkPlayerInArea(center, CVector(1.2f, 1.2f, 3.0f)))
    {
        bWaitMessageShown = false;
        return state;
    }

    if (!bWaitMessageShown)
    {
        bWaitMessageShown = true;
        CChat::AddMessage("{c7a6ff}[Negocio]{ffffff} Ocupado: atienden de a uno, espera tu turno");
        CMissionMirror::LogEventf("rule-shop-wait at=%.1f,%.1f,%.1f area=%d", center.x, center.y, center.z,
            localPlayer->m_nAreaCode);
    }
    return false;
}

