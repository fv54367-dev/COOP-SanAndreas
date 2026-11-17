static bool __cdecl FilterCompareFlag(CRunningScript* script, bool state)
{
    if (!script || CommandHooks::GetCurrentScript() != script)
        return state;

    const uint16_t opcode = CommandHooks::GetCurrentCommand() & 0x7FFF;
    bool crateHandled = false;
    const bool crateState = CHomeInvasion::FilterCompare(script, opcode, state, crateHandled);
    if (crateHandled) return crateState;
    bool actionHandled = false;
    const bool actionState = CMissionGuestActions::FilterCompare(script, opcode, state, actionHandled);
    if (actionHandled) return actionState;
    state = CActivityChallenge::FilterCompare(script, opcode, state);
    state = CPatientHandover::FilterCompare(script, opcode, state);
    state = CCoopLowrider::FilterDanceResult(script, opcode, state);
    // FilterStart only ever turns a true into false: with a false state it returned false too and this cut every rule
    // below that turns a false into true (guest at the wheel, guest in the marker, Badlands, countryside...)
    if (state && !CHighStakesRace::FilterStart(script, opcode, state)) return false;
    if (CCoopPool::Enabled())
        state = CCoopPool::FilterCompare(script, opcode, state);
    // T63 has an explicit allowlist; its spots must not enter the all-player wait below.
    bool guestHandled = false;
    const bool guestState = CMissionGuestCounts::FilterCompare(script, opcode, state, guestHandled);
    if (guestHandled || guestState != state)
        return guestState;
    // shop scripts run locally, outside mission authority and synchronization rules.
    if (opcode == 0x0102 && IsShopCounterScript(script) && !COpCodeSync::bProcessingNetworkOpcode)
        return FilterShopCounter(script, state);

    if (!CMissionSyncRules::IsEnabled())
        return state;

    // Sample before false comparisons return: onmission can prevent reaching 03EE.
    if (CNetwork::m_bConnected && !COpCodeSync::bProcessingNetworkOpcode && IsStoryLauncher(script))
    {
        static std::unordered_map<std::string, DWORD> lastLaunchLog;
        const std::string name(script->m_szName, strnlen(script->m_szName, 8));
        const DWORD now = GetTickCount();
        const auto last = lastLaunchLog.find(name);
        if (last == lastLaunchLog.end() || now - last->second >= 5000)
        {
            lastLaunchLog[name] = now;
            const int counter = _strnicmp(script->m_szName, "SWEET", 8) == 0
                ? *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[1808]) : -1;
            const int onmission = CTheScripts::OnAMissionFlag
                ? *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag]) : -1;
            CMissionMirror::LogEventf("solo-launch script=%.8s counter=%d onmission=%d authority=%d individual=%d opcode=%04X state=%d",
                script->m_szName, counter, onmission, CCoopMission::IsScriptAuthority() ? 1 : 0,
                CMissionMode::IsIndividual() ? 1 : 0, opcode, state ? 1 : 0);
        }
    }
    if (!state && opcode >= 0x02CA && opcode <= 0x02CC && CConfigLoader::ms_bMissionSharedOnScreen && script &&
        script->m_bIsMission && CNetwork::m_bConnected && !COpCodeSync::bProcessingNetworkOpcode &&
        CCoopMission::IsScriptAuthority() && COpCodeSync::IsScriptSynced(script) &&
        CMissionSyncRules::IsStoryScript(script))
    {
        // gta-reversed eScriptCommands.h: IS_CAR/CHAR/OBJECT_ON_SCREEN, before the native NOT.
        CEntity* entity = nullptr;
        switch (opcode)
        {
        case 0x02CA: entity = CPools::GetVehicle(ScriptParams[0]); break;
        case 0x02CB: entity = CPools::GetPed(ScriptParams[0]); break;
        case 0x02CC: entity = CPools::GetObject(ScriptParams[0]); break;
        }
        if (entity)
        {
            const bool wideCar = opcode == 0x02CA && CConfigLoader::ms_bMissionWideCarOnScreen &&
                static_cast<CVehicle*>(entity)->m_nCreatedBy == MISSION_VEHICLE;
            const float radius = wideCar ? ON_SCREEN_SHARED_CAR_RADIUS : ON_SCREEN_SHARED_RADIUS;
            for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
            {
                if (!CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer) || !networkPlayer->m_pPed ||
                    networkPlayer->m_pPed->m_nAreaCode != entity->m_nAreaCode)
                    continue;
                const CVector delta = networkPlayer->m_pPed->GetPosition() - entity->GetPosition();
                if (delta.x * delta.x + delta.y * delta.y + delta.z * delta.z >= radius * radius)
                    continue;

                static DWORD nLastOnScreenLog = 0;
                const DWORD now = GetTickCount();
                if (now - nLastOnScreenLog >= 5000)
                {
                    nLastOnScreenLog = now;
                    CMissionMirror::LogEventf("rule-on-screen opcode=%04X near=%s", opcode, networkPlayer->GetName().c_str());
                }
                return true;
            }
        }
    }
    if (!state && opcode == 0x00FE && CConfigLoader::ms_bMissionAnyPlayerTrigger &&
        script->m_bIsMission && CNetwork::m_bConnected && !COpCodeSync::bProcessingNetworkOpcode &&
        CCoopMission::IsScriptAuthority() && COpCodeSync::IsScriptSynced(script))
    {
        struct SAnyPlayerTrigger
        {
            const char* script;
            CVector center;
            CVector radius;
        };
        static const SAnyPlayerTrigger triggers[] = {
            {"HOODS5", {2775.793f, -1942.909f, 15.6576f}, {100.0f, 100.0f, 50.0f}}
        };
        CPlayerPed* localPlayer = FindPlayerPed(0);
        const float* f = reinterpret_cast<const float*>(&ScriptParams[0]);
        for (const auto& trigger : triggers)
        {
            if (!localPlayer || ScriptParams[0] != CPools::GetPedRef(localPlayer) ||
                _strnicmp(script->m_szName, trigger.script, 8) != 0 ||
                !(fabsf(f[1] - trigger.center.x) <= 0.1f && fabsf(f[2] - trigger.center.y) <= 0.1f &&
                  fabsf(f[3] - trigger.center.z) <= 0.1f && fabsf(f[4] - trigger.radius.x) <= 0.1f &&
                  fabsf(f[5] - trigger.radius.y) <= 0.1f && fabsf(f[6] - trigger.radius.z) <= 0.1f))
                continue;
            for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
            {
                CPed* ped = networkPlayer->m_pPed;
                if (!ped || networkPlayer->m_bDead || !(ped->m_fHealth > 0.0f) ||
                    !CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer) ||
                    ped->m_nAreaCode != localPlayer->m_nAreaCode)
                    continue;
                if (ped->m_nPedFlags.bInVehicle && !ped->m_pVehicle)
                    continue;
                // Native GetCharCoordinates uses the vehicle position; CBox bounds are inclusive.
                const CVector pos = ped->m_nPedFlags.bInVehicle ? ped->m_pVehicle->GetPosition() : ped->GetPosition();
                if (!(pos.x >= f[1] - f[4] && pos.x <= f[1] + f[4] &&
                      pos.y >= f[2] - f[5] && pos.y <= f[2] + f[5] &&
                      pos.z >= f[3] - f[6] && pos.z <= f[3] + f[6]))
                    continue;
                static DWORD nLastTriggerLog = 0;
                const DWORD now = GetTickCount();
                if (now - nLastTriggerLog >= 5000)
                {
                    nLastTriggerLog = now;
                    CMissionMirror::LogEventf("rule-any-player-trigger script=%.8s player=%s",
                        script->m_szName, networkPlayer->GetName().c_str());
                }
                return true;
            }
        }
    }
    // gta-reversed LocateCharAnyMeansChar2D: rectangular bounds, before native NOT.
    // Only the 300 m escape test; Smoke's small dialogue trigger stays host-only.
    if (!state && opcode == 0x00F2 && CConfigLoader::ms_bCoopRunningDog &&
        CConfigLoader::ms_bVanillaScripts && script->m_bIsMission &&
        !_strnicmp(script->m_szName, "SMOKE2", 8) && CNetwork::m_bConnected &&
        !COpCodeSync::bProcessingNetworkOpcode && CCoopMission::IsScriptAuthority() &&
        COpCodeSync::IsScriptSynced(script))
    {
        const float* f = reinterpret_cast<const float*>(ScriptParams);
        auto* host = FindPlayerPed(0);
        auto* target = CPools::GetPed(ScriptParams[1]);
        if (host && target && ScriptParams[0] == CPools::GetPedRef(host) &&
            f[2] == 300.0f && f[3] == 300.0f && ScriptParams[4] == 0)
        {
            const auto center = target->GetPosition();
            for (auto guest : CNetworkPlayerManager::m_pPlayers)
            {
                auto* ped = guest->m_pPed;
                if (!ped || guest->m_bDead || ped->m_fHealth <= 0.0f ||
                    !CCoopMission::ShouldIncludeNetworkPlayer(guest) ||
                    ped->m_nAreaCode != target->m_nAreaCode ||
                    (ped->m_nPedFlags.bInVehicle && !ped->m_pVehicle)) continue;
                const auto pos = ped->m_nPedFlags.bInVehicle ? ped->m_pVehicle->GetPosition() : ped->GetPosition();
                if (fabsf(pos.x - center.x) <= f[2] && fabsf(pos.y - center.y) <= f[3])
                    return true;
            }
        }
    }
    // DRUGS3 boss chase (coop-gray-imports): LOCATE_CHAR_ANY_MEANS_CHAR_2D between the host and the boss, either order.
    // 200 m = lost him (fail), 60 m = caught up, 40 m = his recording's pace: any mission player near counts.
    // TWAR7 (coop-og-loc-escape): Freddy escapes when the host is past 250 m. The 3-35 m triggers stay host-only.
    const bool grayImports = CConfigLoader::ms_bCoopGrayImports && !_strnicmp(script->m_szName, "DRUGS3", 8);
    const bool ogLocEscape = CConfigLoader::ms_bCoopOgLocEscape && !_strnicmp(script->m_szName, "TWAR7", 8);
    if (!state && opcode == 0x00F2 && (grayImports || ogLocEscape) && CConfigLoader::ms_bVanillaScripts &&
        script->m_bIsMission && CNetwork::m_bConnected &&
        !COpCodeSync::bProcessingNetworkOpcode && CCoopMission::IsScriptAuthority() && COpCodeSync::IsScriptSynced(script))
    {
        const float* f = reinterpret_cast<const float*>(ScriptParams);
        auto* host = FindPlayerPed(0);
        const int hostRef = host ? CPools::GetPedRef(host) : -1;
        const bool chaseSize = f[2] == f[3] &&
            (grayImports ? (f[2] == 200.0f || f[2] == 60.0f || f[2] == 40.0f) : f[2] == 250.0f);
        CPed* target = nullptr;
        if (host && chaseSize && ScriptParams[4] == 0)
        {
            if (ScriptParams[0] == hostRef) target = CPools::GetPed(ScriptParams[1]);
            else if (ScriptParams[1] == hostRef) target = CPools::GetPed(ScriptParams[0]);
        }
        if (target && !target->IsPlayer())
        {
            const auto center = target->m_nPedFlags.bInVehicle && target->m_pVehicle ? target->m_pVehicle->GetPosition()
                                                                                       : target->GetPosition();
            for (auto guest : CNetworkPlayerManager::m_pPlayers)
            {
                auto* ped = guest->m_pPed;
                if (!ped || guest->m_bDead || ped->m_fHealth <= 0.0f || !CCoopMission::ShouldIncludeNetworkPlayer(guest) ||
                    ped->m_nAreaCode != target->m_nAreaCode || (ped->m_nPedFlags.bInVehicle && !ped->m_pVehicle)) continue;
                const auto pos = ped->m_nPedFlags.bInVehicle ? ped->m_pVehicle->GetPosition() : ped->GetPosition();
                if (fabsf(pos.x - center.x) <= f[2] && fabsf(pos.y - center.y) <= f[3])
                {
                    if (f[2] >= 200.0f)
                        CMissionMirror::LogEventf("rule-chase-near script=%.8s player=%s", script->m_szName,
                            guest->GetName().c_str());
                    return true;
                }
            }
        }
    }
    if (!state && CConfigLoader::ms_bCoopBadlands && CConfigLoader::ms_bVanillaScripts && script->m_bIsMission &&
        !_strnicmp(script->m_szName, "BCRASH1", 8) && CNetwork::m_bConnected && !COpCodeSync::bProcessingNetworkOpcode &&
        CCoopMission::IsScriptAuthority() && COpCodeSync::IsScriptSynced(script) && CMissionRulesWorld::Badlands(opcode))
        return true;
    if (!state && CConfigLoader::ms_bCoopCountryside && CConfigLoader::ms_bVanillaScripts && script->m_bIsMission &&
        CNetwork::m_bConnected && !COpCodeSync::bProcessingNetworkOpcode && CCoopMission::IsScriptAuthority() &&
        COpCodeSync::IsScriptSynced(script) && CMissionRulesWorld::Countryside(script, opcode))
        return true;
    // gta-reversed LocateCharEntity2D/LocateChar2D: inclusive rectangle, before NOT.
    // Preserve the train speed/traffic tests (20-50 m); only extend escape checks.
    if (!state && CConfigLoader::ms_bCoopWrongSideTracks && CConfigLoader::ms_bVanillaScripts &&
        script->m_bIsMission && !_strnicmp(script->m_szName, "SMOKE3", 8) &&
        CNetwork::m_bConnected && !COpCodeSync::bProcessingNetworkOpcode &&
        CCoopMission::IsScriptAuthority() && COpCodeSync::IsScriptSynced(script))
    {
        const float* f = reinterpret_cast<const float*>(ScriptParams);
        auto* host = FindPlayerPed(0);
        CEntity* target = nullptr;
        if (opcode == 0x0202 && ((f[2] == 200.0f && f[3] == 200.0f) ||
            (f[2] == 320.0f && f[3] == 320.0f)))
        {
            auto* vehicle = CPools::GetVehicle(ScriptParams[1]);
            if (vehicle && vehicle->m_nVehicleSubType == VEHICLE_TRAIN) target = vehicle;
        }
        else if (opcode == 0x00F2 && f[2] == 50.0f && f[3] == 50.0f)
        {
            auto* ped = CPools::GetPed(ScriptParams[1]);
            if (ped && ped->m_nCreatedBy == 2 && !ped->IsPlayer() &&
                !CNetworkPlayerManager::GetPlayer(ped)) target = ped;
        }
        if (target && host && ScriptParams[0] == CPools::GetPedRef(host) && ScriptParams[4] == 0)
        {
            const auto center = target->GetPosition();
            for (auto guest : CNetworkPlayerManager::m_pPlayers)
            {
                auto* ped = guest->m_pPed;
                if (!ped || guest->m_bDead || ped->m_fHealth <= 0.0f ||
                    !CCoopMission::ShouldIncludeNetworkPlayer(guest) || ped->m_nAreaCode != target->m_nAreaCode ||
                    (ped->m_nPedFlags.bInVehicle && !ped->m_pVehicle)) continue;
                const auto pos = ped->m_nPedFlags.bInVehicle ? ped->m_pVehicle->GetPosition() : ped->GetPosition();
                if (fabsf(pos.x - center.x) <= f[2] && fabsf(pos.y - center.y) <= f[3]) return true;
            }
        }
    }
    const bool guestCarState = FilterGuestCar(script, opcode, state);
    const bool bGuestCarOverride = !state && guestCarState;
    state = guestCarState;
    if (!state)
    {
        NotifyAwaitedAuthority(script, opcode);
        return state;
    }
    if (opcode == 0x03EE)
        return (CNetwork::m_bConnected && !script->m_bNotFlag && IsStoryLauncher(script)) ? FilterMissionStart(script)
                                                                                              : state;

    // Object LOCATEs had no wait rule before this feature.
    if ((opcode == 0x0471 || opcode == 0x0473 || opcode == 0x0474 || opcode == 0x0476) && !bGuestCarOverride)
        return state;
    SLocate locate;
    if (!ReadLocate(opcode, locate) || !script->m_bIsMission || !CNetwork::m_bConnected ||
        COpCodeSync::bProcessingNetworkOpcode ||
        !CCoopMission::IsScriptAuthority() || !COpCodeSync::IsScriptSynced(script) ||
        !CMissionSyncRules::IsStoryScript(script) || CMirrorBlips::IsPerPlayerMissionRunning())
        return state;

    // Just Business only (the user, 2026-11-12): no marker of the mission waits for the others, it bugs the mission.
    // The start of the mission (03EE) and the entry window teleport keep their rules.
    if (CConfigLoader::ms_bJustBusinessNoMarkerWait && CConfigLoader::ms_bCoopJustBusiness &&
        CConfigLoader::ms_bVanillaScripts && !_strnicmp(script->m_szName, "DRUGS1", 8))
        return state;

    // on the authority's player, or on the vehicle he is in
    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (!localPlayer)
        return state;
    const bool bGuestDrivenCar = locate.bCarSelf && CConfigLoader::ms_bMissionAnyPlayerInCar &&
        FindGuestInMissionCar(CPools::GetVehicle(ScriptParams[0]));
    if (locate.bCarSelf)
    {
        const bool bLocalCar = localPlayer->m_nPedFlags.bInVehicle && localPlayer->m_pVehicle &&
            ScriptParams[0] == CPools::GetVehicleRef(localPlayer->m_pVehicle);
        // Keep marker waits when the authority is on foot and a guest drives the remembered car.
        const bool bGuestCar = pGuestCarScript == script && CanUseGuestCar(script, CommandHooks::GetCurrentCommandIP()) &&
            nGuestCarMission == CMissionMirror::GetRunningMissionId() && ScriptParams[0] == nGuestCarRef &&
            FindGuestInMissionCar(CPools::GetVehicle(nGuestCarRef));
        if (!bLocalCar && !bGuestCar && !bGuestDrivenCar)
            return state;
    }
    else if (ScriptParams[0] != CPools::GetPedRef(localPlayer))
        return state;

    // A guest's car reaching a small LOCATE must not leave the authority behind.
    if ((bGuestCarOverride || bGuestDrivenCar) && locate.radius <= 20.0f && localPlayer->m_fHealth > 0.0f &&
        CWorld::Players[0].m_nPlayerState != PLAYERSTATE_HASDIED &&
        CWorld::Players[0].m_nPlayerState != PLAYERSTATE_HASBEENARRESTED)
    {
        CVehicle* vehicle = CPools::GetVehicle(bGuestDrivenCar ? ScriptParams[0] : nGuestCarRef);
        // 12 m for every mission (the user, 2026-11-12: T98's 50 m in the LS missions is gone)
        if (vehicle && !IsPedNearMissionLocate(localPlayer, locate.center, locate.radius, locate.height,
                locate.b3D, vehicle->m_nAreaCode, 12.0f))
        {
            static DWORD nLastHostMessage = 0;
            const DWORD now = GetTickCount();
            if (now - nLastHostMessage >= 5000)
            {
                nLastHostMessage = now;
                CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Acercate al marcador para seguir la mision");
                CMissionMirror::LogEventf("rule-wait-host-guest-car opcode=%04X at=%.1f,%.1f", opcode,
                    locate.center.x, locate.center.y);
            }
            return false;
        }
    }

    const bool driveBy = CConfigLoader::ms_bCoopDriveBy && CConfigLoader::ms_bVanillaScripts &&
        !_strnicmp(script->m_szName, "SWEET4", 8);
    const bool sepulcros = CConfigLoader::ms_bCoopLosSepulcros && CConfigLoader::ms_bVanillaScripts &&
        !_strnicmp(script->m_szName, "SWEET7", 8);
    const bool runningDog = CConfigLoader::ms_bCoopRunningDog && CConfigLoader::ms_bVanillaScripts &&
        !_strnicmp(script->m_szName, "SMOKE2", 8);
    const bool tracks = CConfigLoader::ms_bCoopWrongSideTracks && CConfigLoader::ms_bVanillaScripts &&
        !_strnicmp(script->m_szName, "SMOKE3", 8);
    const bool business = CConfigLoader::ms_bCoopJustBusiness && CConfigLoader::ms_bVanillaScripts &&
        !_strnicmp(script->m_szName, "DRUGS1", 8);
    // The spray-shop checks guide the authority's car, not a convoy inside one garage.
    if (driveBy && locate.bFixedPlace && locate.b3D && locate.radius <= 20.0f &&
        ((DistanceBetweenPoints(locate.center, CVector(2075.55f, -1831.09f, 12.21f)) < 0.2f) ||
         (DistanceBetweenPoints(locate.center, CVector(1024.55f, -1039.09f, 35.21f)) < 0.2f))) return state;
    if (opcode == 0x00EC && !script->m_bNotFlag && IsBeachArrival(script, locate))
    {
        // the arrival scene waits for the players of the mission (60 m around the party; this 50 x 50 box plus a margin)
        std::string waiting;
        if (bBeachArrived || AreMissionPlayersNear(locate.center, 20.0f, 0.0f, false, waiting, 60.0f))
        {
            if (!bBeachArrived)
            {
                bBeachArrived = true;
                nBeachArrivalTick = GetTickCount();
                CMissionMirror::LogEvent("rule-beach-arrival");
            }
            return state;
        }
        CMissionHints::AuthorityWait(Packets::Scripts::MissionWaitNotice::BEACH, waiting);
        static DWORD nLastBeachMessage = 0;
        if (GetTickCount() - nLastBeachMessage > 5000)
        {
            nLastBeachMessage = GetTickCount();
            if (!CMissionHints::WaitNoticesEnabled())
                CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Esperando a %s para seguir la mision.", waiting.c_str());
            CMissionMirror::LogEventf("rule-wait-beach missing=%s", waiting.c_str());
        }
        return false;
    }

    std::string missing;
    if (locate.bSphere || (bGuestDrivenCar && locate.radius <= 20.0f))
    {
        // a marker of the mission (red cylinder, also for a car or around a char): everybody of the mission in it, the
        // NOT form included (`not locate` loops until the player arrives)
        if (AreMissionPlayersNear(locate.center, locate.radius, locate.height, locate.b3D, missing, 4.0f))  // 4 m for every mission, nobody teleported in (the user, 2026-11-12: 12 m pulled the other in)
            return state;

        CMissionHints::AuthorityWait(Packets::Scripts::MissionWaitNotice::MARKER, missing);
        static DWORD nLastMessage = 0;
        if (GetTickCount() - nLastMessage > 5000)
        {
            nLastMessage = GetTickCount();
            if (!CMissionHints::WaitNoticesEnabled())
                CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Esperando a %s para seguir la mision.", missing.c_str());
            CMissionMirror::LogEventf("rule-wait opcode=%04X at=%.1f,%.1f missing=%s", opcode, locate.center.x,
                locate.center.y, missing.c_str());
        }
        return false;
    }

    // Process also runs after scripts; catch entry before a same-frame scene LOCATE.
    ProcessEntryWindow();

    // no marker: a small fixed place the authority reached while players of the mission are in another place (still
    // inside the barber when he walked out of it, still outside the pizza when he bought the food, both in Ryder); the
    // scene it starts waits until they are where he is. Inside an interior only the "stand still here" locates
    // (LOCATE_STOPPED_CHAR_*, gta-reversed eScriptCommands.h): the other ones there drive fights (CASIN10, FINALEA,
    // DRUGS4), where a false result would start or skip an attack. The short entry window also admits moving LOCATEs.
    const bool bOutside = localPlayer->m_nAreaCode == 0;
    const bool bStopped = (opcode >= 0x00EF && opcode <= 0x00F1) || (opcode >= 0x0101 && opcode <= 0x0103);
    if (!locate.bFixedPlace || locate.radius > 20.0f ||
        (!bOutside && !(CConfigLoader::ms_bMissionWaitSamePlace && bStopped) && !bEntryWindow) ||
        !FindPlayersInOtherPlace(localPlayer->m_nAreaCode, missing, CConfigLoader::ms_bMissionWaitOnEntry))
        return state;

    CMissionHints::AuthorityWait(bOutside ? Packets::Scripts::MissionWaitNotice::LEAVE_INTERIOR
                                          : Packets::Scripts::MissionWaitNotice::ENTER_INTERIOR, missing);
    static DWORD nLastInteriorMessage = 0;
    if (GetTickCount() - nLastInteriorMessage > 5000)
    {
        nLastInteriorMessage = GetTickCount();
        if (CMissionHints::WaitNoticesEnabled())
        {
            // the help box of every player of the mission (AuthorityWait above)
        }
        else if (bOutside)
            CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Esperando a que %s salga para seguir la mision.", missing.c_str());
        else
            CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Esperando a que %s entre para seguir la mision.", missing.c_str());
        CMissionMirror::LogEventf("%s opcode=%04X at=%.1f,%.1f area=%d missing=%s",
            bOutside ? "rule-wait-interior" : "rule-wait-place", opcode, locate.center.x, locate.center.y,
            localPlayer->m_nAreaCode, missing.c_str());
    }
    return false;
}

static void __declspec(naked) CRunningScript__UpdateCompareFlag_Hook()
{
    __asm
    {
        push ecx
        push dword ptr [esp + 8]
        push ecx
        call FilterCompareFlag
        add esp, 8
        pop ecx
        mov byte ptr [esp + 4], al

        // the instruction the jump replaced, then the rest of the function
        mov al, byte ptr [ecx + 0xD2]
        push 0x4859D6
        ret
    }
}

