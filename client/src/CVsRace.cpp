#include "CHighStakesRace.h"
#include "stdafx.h"
#include "CVsRace.h"
#include "CMissionMirror.h"
#include "CVsLobby.h"
#include "CNetworkVehicleManager.h"
#include "network/packets/scripts.h"
#include "imgui.h"
#include "CMissionHints.h"
#include <CFont.h>
#include <CSprite2d.h>
#include <CWorld.h>
#include <mission_ids.h>
#include <algorithm>
#include <vector>

// CCarCtrl::CarDensityMultiplier and CCarCtrl::bAllowEmergencyServicesToBeCreated (verified in gta-reversed CarCtrl.h), the same
// the temporary patches of CPatch.cpp use. CPopulation::PedDensityMultiplier is in the sdk.
static constexpr uintptr_t ADDR_CAR_DENSITY = 0x8A5B20;
static constexpr uintptr_t ADDR_EMERGENCY_ALLOWED = 0x8A5B28;
static constexpr int FINISH_SCORE_BASE = 90000000;

bool CVsRace::IsActive()
{
    return ms_bActive;
}

void CVsRace::Enter()
{
    ms_bActive = true;
    ms_nLastClearTick = 0;
    ms_vRacers.clear();
    ms_vNpcs.clear();
    ms_bHudActive = false;
    ms_nLocalState = 0;
    ms_nLastSendTick = 0;

    // what the race found, to give it back
    ms_fPedDensity = CPopulation::PedDensityMultiplier;
    ms_fCarDensity = *reinterpret_cast<float*>(ADDR_CAR_DENSITY);
    ms_bEmergency = *reinterpret_cast<unsigned char*>(ADDR_EMERGENCY_ALLOWED);
    ms_nMaxWanted = CWanted::MaximumWantedLevel;

    CWanted::SetMaximumWantedLevel(0);
    Command<Commands::CLEAR_WANTED_LEVEL>(0);

    CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Carrera VS: sin peatones, trafico ni policia mientras corran juntos.");
}

void CVsRace::Exit()
{
    // tell the others this racer is out (state 2: disqualified or left, struck out on their board); one that crossed the line
    // stays finished with his time
    if (CNetwork::m_bAuthenticated && ms_nLastSendTick != 0)
    {
        Packets::Scripts::MinigameVsScore packet{};
        packet.gameId = PROGRESS_GAME_ID;
        packet.score = ms_nLocalState == 1 ? FINISH_SCORE_BASE + ms_nLocalFinishSeconds : ms_nLocalScore;
        packet.state = ms_nLocalState == 1 ? 1 : 2;
        GetPacketFactory().Send(packet);
    }
    ms_nLastSendTick = 0;
    ms_bActive = false;
    ms_bHudActive = false;
    ms_vRacers.clear();
    ms_vNpcs.clear();

    CPopulation::PedDensityMultiplier = ms_fPedDensity;
    *reinterpret_cast<float*>(ADDR_CAR_DENSITY) = ms_fCarDensity;
    *reinterpret_cast<unsigned char*>(ADDR_EMERGENCY_ALLOWED) = ms_bEmergency;
    CWanted::SetMaximumWantedLevel(static_cast<int>(ms_nMaxWanted));

    CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Termino la carrera VS: vuelve el trafico y la gente.");
}

// the race script writes the car density itself (1.0 when it ends), so it is put back to 0 every frame
void CVsRace::Reassert()
{
    CPopulation::PedDensityMultiplier = 0.0f;
    *reinterpret_cast<float*>(ADDR_CAR_DENSITY) = 0.0f;
    *reinterpret_cast<unsigned char*>(ADDR_EMERGENCY_ALLOWED) = 0;

    // the ones around when it started (and the ones the other players' games stream in): the cars and peds of the game, not
    // the mission ones (the racers' cars and the other players are mission entities)
    const uint32_t now = GetTickCount();
    if (now - ms_nLastClearTick < 3000)
        return;
    ms_nLastClearTick = now;

    CPlayerPed* pPlayer = FindPlayerPed(0);
    if (pPlayer == nullptr)
        return;

    const CVector pos = pPlayer->GetPosition();
    Command<Commands::CLEAR_AREA_OF_CARS>(pos.x - 250.0f, pos.y - 250.0f, pos.z - 100.0f, pos.x + 250.0f, pos.y + 250.0f, pos.z + 100.0f);
    Command<Commands::CLEAR_AREA_OF_CHARS>(pos.x - 250.0f, pos.y - 250.0f, pos.z - 100.0f, pos.x + 250.0f, pos.y + 250.0f, pos.z + 100.0f);
}

// VS with NPCs, a guest: his own grid slot is the owner's (the owner's car sits there), the NPC cars hold the others. He goes
// behind the rearmost car of the grid, row by row and column by column (the grid's own spacing), on a spot with ground under it,
// no car within 4.5 m and nothing built between it and the grid: the old "7 m straight back" put the second guest inside the
// stadium's structures (8-Track, 2026-11-15).
bool CVsRace::FindGuestGridSpot(CVehicle* pMine, int nBehind, CVector& out)
{
    if (pMine == nullptr || pMine->m_matrix == nullptr)
        return false;
    const CVector origin = pMine->GetPosition();
    CVector fwd = pMine->m_matrix->up, right = pMine->m_matrix->right;
    fwd.z = 0.0f; right.z = 0.0f;
    fwd.Normalise(); right.Normalise();

    // the grid: every racer car (NPCs, hidden or copies, and the players') near the start
    std::vector<CVehicle*> grid;
    for (auto pVehicle : CPools::ms_pVehiclePool)
    {
        if (pVehicle == nullptr || pVehicle == pMine || pVehicle->m_matrix == nullptr || pVehicle->m_pDriver == nullptr)
            continue;
        if ((pVehicle->GetPosition() - origin).Magnitude() > 70.0f)
            continue;
        if (pVehicle->m_nCreatedBy != MISSION_VEHICLE && CNetworkPlayerManager::GetPlayer(pVehicle->m_pDriver) == nullptr)
            continue;
        grid.push_back(pVehicle);
    }
    // rows (along the start direction) and columns (across it) of the grid
    std::vector<float> rows, cols;
    auto addUnique = [](std::vector<float>& v, float x, float tol)
    {
        for (float y : v)
            if (fabsf(x - y) < tol) return;
        v.push_back(x);
    };
    addUnique(rows, 0.0f, 2.5f);
    addUnique(cols, 0.0f, 2.0f);
    CVehicle* pRear = pMine;
    float rearProj = 0.0f;
    for (CVehicle* pCar : grid)
    {
        const CVector d = pCar->GetPosition() - origin;
        const float p = d.x * fwd.x + d.y * fwd.y, l = d.x * right.x + d.y * right.y;
        addUnique(rows, p, 2.5f);
        if (fabsf(l) < 12.0f)
            addUnique(cols, l, 2.0f);
        if (p < rearProj) { rearProj = p; pRear = pCar; }
    }
    std::sort(rows.begin(), rows.end());
    float spacing = 0.0f; int gaps = 0;
    for (size_t i = 1; i < rows.size(); ++i)
        if (rows[i] - rows[i - 1] < 15.0f) { spacing += rows[i] - rows[i - 1]; ++gaps; }
    spacing = gaps ? (std::clamp)(spacing / gaps, 5.0f, 12.0f) : 7.0f;
    std::sort(cols.begin(), cols.end(), [](float a, float b) { return fabsf(a) < fabsf(b); });

    const CVector rearPos = pRear->GetPosition();
    int found = 0;
    for (int k = 1; k <= 8; ++k)
    {
        for (float lat : cols)
        {
            CVector spot = origin + fwd * (rearProj - k * spacing) + right * lat;
            bool ground = false;
            const float z = CWorld::FindGroundZFor3DCoord(spot.x, spot.y, rearPos.z + 3.0f, &ground, nullptr);
            if (!ground || fabsf(z - rearPos.z) > 3.0f)
                continue;
            spot.z = z + 1.0f;
            bool clear = true;
            for (auto pVehicle : CPools::ms_pVehiclePool)
                if (pVehicle && pVehicle != pMine && pVehicle->m_matrix && (pVehicle->GetPosition() - spot).Magnitude() < 4.5f)
                    clear = false;
            const CVector from = rearPos + CVector(0.0f, 0.0f, 1.0f);
            if (!clear || !CWorld::GetIsLineOfSightClear(from, spot, true, false, false, true, false, false, false))
                continue;
            // the spot and both sides of the car (half a car wide, half a car long) free of buildings
            const CVector side = right * 1.4f, ahead = fwd * 2.6f;
            if (!CWorld::GetIsLineOfSightClear(spot - side, spot + side, true, false, false, true, false, false, false) ||
                !CWorld::GetIsLineOfSightClear(spot - ahead, spot + ahead, true, false, false, true, false, false, false))
                continue;
            if (++found >= nBehind)
            {
                out = spot;
                return true;
            }
        }
    }
    return false;
}

// the racers' cars of the script (mission vehicles with a driver that is not a player) are the NPC opponents: every game runs its
// own and they overlap the other players' ones, so in a VS battle they are hidden and intangible. The cars the players drive stay.
// Every game places its player on the same grid slot: a player that starts on the spot of a lower id player is moved to the
// slot of one of the hidden NPC cars (the grid of the race, a valid place by construction). The slot depends on how many racers
// have a lower id, so every game gives each player a different one
void CVsRace::HideNpcRacersAndSeparate()
{
    CPlayerPed* pLocal = FindPlayerPed(0);
    if (pLocal == nullptr)
        return;

    CVehicle* pMine = pLocal->m_nPedFlags.bInVehicle ? pLocal->m_pVehicle : nullptr;
    const uint32_t myId = CNetworkPlayerManager::m_localPlayerId.value();
    bool bOverlap = false;
    std::vector<CVehicle*> npcCars;

    for (auto pVehicle : CPools::ms_pVehiclePool)
    {
        if (pVehicle == nullptr || pVehicle == pMine || pVehicle->m_matrix == nullptr)
            continue;

        CPed* pDriver = pVehicle->m_pDriver;
        CNetworkPlayer* pDriverPlayer = pDriver ? CNetworkPlayerManager::GetPlayer(pDriver) : nullptr;

        if (pDriverPlayer != nullptr)
        {
            // the cars of the players are always seen and solid
            pVehicle->m_bIsVisible = true;
            pDriver->m_bIsVisible = true;
            // the stadium is its own area (the log showed the remote car in area 0 and the local player in area 7): the game only
            // draws what is in the area of the camera, so the cars of the other players are put in ours
            pVehicle->m_nAreaCode = pLocal->m_nAreaCode;
            pDriver->m_nAreaCode = pLocal->m_nAreaCode;

            if (pMine != nullptr && pMine->m_matrix != nullptr && pDriverPlayer->m_playerId.value() < myId &&
                (pVehicle->GetPosition() - pMine->GetPosition()).Magnitude() < 2.0f)
                bOverlap = true;
            continue;
        }

        if (pVehicle->m_nCreatedBy != MISSION_VEHICLE || pDriver == nullptr || pDriver == pLocal)
            continue;

        // VS with NPCs (lobby key 2): only the owner's racers are real; his game keeps its own, the others see his copies
        if (CVsLobby::WithNpcs())
        {
            CNetworkVehicle* pNetwork = CNetworkVehicleManager::GetVehicle(pVehicle);
            const bool bCopy = pNetwork != nullptr && !pNetwork->m_bSyncing;
            if (CVsLobby::IsOwnerLocal() != bCopy)
            {
                pVehicle->m_bIsVisible = true;
                pVehicle->m_bUsesCollision = true;
                pDriver->m_bIsVisible = true;
                pVehicle->m_nAreaCode = pLocal->m_nAreaCode;
                pDriver->m_nAreaCode = pLocal->m_nAreaCode;
                continue;
            }
        }

        pVehicle->m_bIsVisible = false;
        pVehicle->m_bUsesCollision = false;
        pDriver->m_bIsVisible = false;
        npcCars.push_back(pVehicle);
    }

    // with NPCs their cars hold the grid: the owner keeps his place, every other player starts behind it
    if (CVsLobby::WithNpcs() && pMine != nullptr)
    {
        static CVehicle* placed = nullptr;
        if (CVsLobby::IsOwnerLocal() || placed == pMine)
            return;
        placed = pMine;
        int nBehind = 1;
        for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
            if (pNetworkPlayer && IsRaceMissionId(pNetworkPlayer->m_nRunningMission) && pNetworkPlayer->m_playerId.value() < myId)
                nBehind++;
        CVector pos;
        if (FindGuestGridSpot(pMine, nBehind, pos))
        {
            const float heading = pMine->GetHeading();
            pMine->Teleport(pos, false);
            pMine->SetHeading(heading);
            pMine->m_vecMoveSpeed = {};
            pMine->m_vecTurnSpeed = {};
            CMissionMirror::LogEventf("vs-race-npc-behind n=%d pos=%.1f,%.1f,%.1f", nBehind, pos.x, pos.y, pos.z);
        }
        else
        {
            // no checked spot: the old one (straight behind his own grid slot)
            const CMatrix& m = *pMine->m_matrix;
            pos = pMine->GetPosition() - m.up * (7.0f * nBehind) + m.right * ((nBehind % 2) ? -3.0f : 3.0f);
            pos.z += 0.5f;
            pMine->Teleport(pos, false);
            CMissionMirror::LogEventf("vs-race-npc-behind n=%d fallback", nBehind);
        }
        return;
    }

    if (!bOverlap || pMine == nullptr)
        return;

    // how many racers go before me (lower id)
    int nRank = 0;
    for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (pNetworkPlayer && IsRaceMissionId(pNetworkPlayer->m_nRunningMission) && pNetworkPlayer->m_playerId.value() < myId &&
            pNetworkPlayer->m_pPed != nullptr && (pNetworkPlayer->m_pPed->GetPosition() - pMine->GetPosition()).Magnitude() < 60.0f)
            nRank++;
    }
    if (nRank == 0)
        return;

    // the grid slots, nearest first
    const CVector start = pMine->GetPosition();
    std::sort(npcCars.begin(), npcCars.end(), [&start](CVehicle* a, CVehicle* b)
        { return (a->GetPosition() - start).Magnitude() < (b->GetPosition() - start).Magnitude(); });

    // the copies of the same slot (the NPC cars of the other games) count once
    {
        std::vector<CVehicle*> slots;
        for (CVehicle* pCar : npcCars)
        {
            bool bDup = false;
            for (CVehicle* pSlot : slots)
            {
                if ((pSlot->GetPosition() - pCar->GetPosition()).Magnitude() < 1.5f)
                {
                    bDup = true;
                    break;
                }
            }
            if (!bDup)
                slots.push_back(pCar);
        }
        npcCars.swap(slots);
    }

    if ((size_t)nRank <= npcCars.size())
    {
        CVehicle* pSlot = npcCars[nRank - 1];
        const CVector pos = pSlot->GetPosition();
        const float heading = pSlot->GetHeading();
        pMine->Teleport(pos, false);
        pMine->SetHeading(heading);
        CMissionMirror::LogEventf("vs-race-slot rank=%d npcCars=%d pos=%.0f,%.0f,%.0f", nRank, (int)npcCars.size(), pos.x, pos.y, pos.z);
    }
    else
    {
        // no free grid slot (more players than NPC cars): behind the others
        const CMatrix& m = *pMine->m_matrix;
        CVector pos = start - m.up * (7.0f * nRank) + m.right * ((nRank % 2) ? -3.0f : 3.0f);
        pos.z += 0.5f;
        pMine->Teleport(pos, false);
    }
}

// local variables of the running mission script (n@ is index n, see ESTADO.md)
static int ScriptLvar(int n)
{
    return reinterpret_cast<int*>(0xA48960)[n];
}

static float ScriptLvarFloat(int n)
{
    float f;
    const int v = ScriptLvar(n);
    memcpy(&f, &v, sizeof(f));
    return f;
}

static CRunningScript* FindRaceScript()
{
    for (CRunningScript* script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
    {
        if (_strnicmp(script->m_szName, "CPRSOLO", 7) == 0 || _strnicmp(script->m_szName, "CPRACE", 6) == 0)
            return script;
    }
    return nullptr;
}

// the race script keeps per racer slot (442@ is the slot of the local player): 273@ lap, 257@ checkpoint, 331@/347@/363@ position
// of the next checkpoint; 224@ laps, 451@ seconds since the start, 49@ the stage (12 racing)
void CVsRace::UpdateProgress()
{
    ms_bHudActive = false;

    CRunningScript* pScript = FindRaceScript();
    CPlayerPed* pLocal = FindPlayerPed(0);
    if (pScript == nullptr || pLocal == nullptr)
        return;

    const int slot = ScriptLvar(442);
    const int stage = ScriptLvar(49);
    const int laps = ScriptLvar(224);
    if (slot < 0 || slot > 15 || stage < 11 || stage > 30 || laps < 1 || laps > 50)
        return;

    const int lap = ScriptLvar(273 + slot);
    const int cp = ScriptLvar(257 + slot);
    if (lap < 0 || lap > 60 || cp < 0 || cp > 200)
        return;

    const CVector pos = pLocal->GetPosition();
    const CVector next(ScriptLvarFloat(331 + slot), ScriptLvarFloat(347 + slot), ScriptLvarFloat(363 + slot));
    float dist = (pos - next).Magnitude();
    if (!(dist >= 0.0f) || dist > 9998.0f)
        dist = 9998.0f;

    const uint32_t now = GetTickCount();
    ms_nLocalScore = (lap * 100 + cp) * 10000 + (9999 - static_cast<int>(dist));
    const uint8_t state = lap >= laps ? 1 : 0;
    if (state == 1 && ms_nLocalState != 1)
    {
        ms_nLocalFinishTick = now;
        ms_nLocalFinishSeconds = ScriptLvar(451);
    }
    ms_nLocalState = state;

    if (now - ms_nLastSendTick >= 250)
    {
        ms_nLastSendTick = now;
        if (ms_bActive)
        {
            Packets::Scripts::MinigameVsScore packet{};
            packet.gameId = PROGRESS_GAME_ID;
            // a finished racer sends his time instead of the progress (the receivers rank the finished ones by arrival)
            packet.score = ms_nLocalState == 1 ? FINISH_SCORE_BASE + ms_nLocalFinishSeconds : ms_nLocalScore;
            packet.state = ms_nLocalState;
            GetPacketFactory().Send(packet);
        }

        // VS with NPCs: the owner's NPC racers, for everybody's board (and his own); alone: the race's NPCs, this board only
        if ((ms_bActive && CVsLobby::WithNpcs() && CVsLobby::IsOwnerLocal()) || ms_bSoloBoard)
        {
            const int racers = (std::min)(ScriptLvar(256), (int)NPC_SLOTS);
            for (int i = 0; i < racers; ++i)
            {
                if (i == slot)
                    continue;
                CPed* pNpc = CPools::GetPed(ScriptLvar(395 + i));
                if (pNpc == nullptr || pNpc->m_fHealth <= 0.0f)
                    continue;
                const int nLap = ScriptLvar(273 + i), nCp = ScriptLvar(257 + i);
                if (nLap < 0 || nLap > 60 || nCp < 0 || nCp > 200)
                    continue;
                const float nz = ScriptLvarFloat(363 + i);
                const bool bDone = nLap >= laps || nz == 99999.0f;
                float d = bDone ? 0.0f : (pNpc->GetPosition() - CVector(ScriptLvarFloat(331 + i), ScriptLvarFloat(347 + i), nz)).Magnitude();
                if (!(d >= 0.0f) || d > 9998.0f)
                    d = 9998.0f;
                const int score = (nLap * 100 + nCp) * 10000 + (9999 - static_cast<int>(d));
                NoteNpc(CNetworkPlayerManager::m_localPlayerId.value(), i, score, bDone ? 1 : 0);
                if (!ms_bActive)
                    continue;
                Packets::Scripts::MinigameVsScore npcPacket{};
                npcPacket.gameId = static_cast<uint8_t>(NPC_GAME_ID + i);
                npcPacket.score = score;
                npcPacket.state = bDone ? 1 : 0;
                GetPacketFactory().Send(npcPacket);
            }
        }
    }

    // my place among the players
    int place = 1, players = 1;
    for (const SRacer& racer : ms_vRacers)
    {
        // a finished racer stops sending once his race ends: he keeps his place
        if (racer.state == 2 || (racer.state == 0 && now - racer.lastTick > 5000))
            continue;

        players++;
        bool bAhead;
        if (racer.state == 1 || ms_nLocalState == 1)
            bAhead = racer.state == 1 && (ms_nLocalState != 1 || racer.finishTick < ms_nLocalFinishTick);
        else
            bAhead = racer.score > ms_nLocalScore;
        if (bAhead)
            place++;
    }

    ms_nHudPosition = place;
    ms_nHudPlayers = players;
    ms_nHudLap = (std::min)(lap + 1, laps);
    ms_nHudLaps = laps;
    ms_nHudSeconds = ScriptLvar(451);
    // the board stays for the whole race once somebody raced against us, also when the others are disqualified or left
    ms_bHudActive = players > 1 || !ms_vRacers.empty() || ms_bSoloBoard;
}

void CVsRace::OnProgress(const Packets::Scripts::MinigameVsScore& packet)
{
    const uint32_t id = packet.playerid.value();
    const uint32_t now = GetTickCount();

    for (SRacer& racer : ms_vRacers)
    {
        if (racer.id != id)
            continue;

        if (packet.state == 1 && racer.state != 1)
            racer.finishTick = now;
        if (packet.state == 1)
            racer.finishSeconds = packet.score - FINISH_SCORE_BASE;
        racer.score = packet.score;
        racer.state = packet.state;
        racer.lastTick = now;
        return;
    }

    SRacer racer;
    racer.id = id;
    racer.score = packet.score;
    racer.state = packet.state;
    racer.lastTick = now;
    racer.finishTick = packet.state == 1 ? now : 0;
    racer.finishSeconds = packet.state == 1 ? packet.score - FINISH_SCORE_BASE : 0;
    ms_vRacers.push_back(racer);
}

void CVsRace::NoteNpc(uint32_t owner, int slot, int score, uint8_t state)
{
    const uint32_t now = GetTickCount();
    for (SNpc& npc : ms_vNpcs)
    {
        if (npc.owner != owner || npc.slot != slot)
            continue;
        if (state == 1 && npc.state != 1)
            npc.finishTick = now;
        npc.score = score;
        npc.state = state;
        npc.lastTick = now;
        return;
    }
    SNpc npc;
    npc.owner = owner;
    npc.slot = slot;
    npc.score = score;
    npc.state = state;
    npc.lastTick = now;
    npc.finishTick = state == 1 ? now : 0;
    ms_vNpcs.push_back(npc);
}

void CVsRace::OnNpcProgress(const Packets::Scripts::MinigameVsScore& packet)
{
    const int slot = packet.gameId - NPC_GAME_ID;
    if (!ms_bActive || slot < 0 || slot >= NPC_SLOTS)
        return;
    NoteNpc(packet.playerid.value(), slot, packet.score, packet.state);
}

bool CVsRace::ShouldDrawHud()
{
    return ms_bActive && ms_bHudActive;
}

// the same panel as the game's (black box with a blue-grey frame: place, "/players", time and the lap), drawn over it
void CVsRace::DrawHud()
{
    if (!ShouldDrawHud())
        return;

    const ImVec2 size = ImGui::GetIO().DisplaySize;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (CConfigLoader::ms_bVsRaceBoard)
    {
        // the old position/lap panel is replaced by the standings board (DrawBoard); only the final results stay
        DrawResults(dl, ImGui::GetFont(), size);
        return;
    }
    const ImU32 black = IM_COL32(0, 0, 0, 255);
    const ImU32 frame = IM_COL32(134, 155, 184, 255);
    const ImU32 white = IM_COL32(255, 255, 255, 255);

    // layout in the game's 640x448 virtual screen (as the HUD of SA and CHighStakesRace): anchored to the right/bottom edge
    // and scaled with the resolution, text sizes follow the vertical scale
    const float sx = size.x / 640.0f, sy = size.y / 448.0f;
    const float x1 = size.x - 20.5f * sx, x0 = x1 - 72.5f * sx;
    const float y1 = size.y - 27.0f * sy, y0 = y1 - 101.0f * sy;
    const float ySplit = y0 + (y1 - y0) * 0.64f;
    const float pad = 3.6f * sy;

    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), black);
    dl->AddRect(ImVec2(x0 + pad, y0 + pad), ImVec2(x1 - pad, ySplit - pad * 0.5f), frame, 0.0f, 0, 2.0f);
    dl->AddRect(ImVec2(x0 + pad, ySplit + pad * 0.5f), ImVec2(x1 - pad, y1 - pad), frame, 0.0f, 0, 2.0f);

    ImFont* font = ImGui::GetFont();
    const float fbig = 38.0f * sy, fsm = 13.4f * sy;
    char buf[32];

    snprintf(buf, sizeof(buf), "%d", ms_nHudPosition);
    const ImVec2 bigSize = font->CalcTextSizeA(fbig, FLT_MAX, 0.0f, buf);
    const float bx = x0 + (x1 - x0) * 0.12f;
    const float by = y0 + pad * 1.5f;
    dl->AddText(font, fbig, ImVec2(bx, by), white, buf);
    dl->AddText(font, fsm, ImVec2(bx + bigSize.x + pad * 0.5f, by), white, "\xC2\xB0");

    snprintf(buf, sizeof(buf), "/%d", ms_nHudPlayers);
    dl->AddText(font, fsm, ImVec2(x1 - (x1 - x0) * 0.36f, by + fbig * 0.45f), white, buf);

    snprintf(buf, sizeof(buf), "%d:%02d", ms_nHudSeconds / 60, ms_nHudSeconds % 60);
    const ImVec2 timeSize = font->CalcTextSizeA(fsm * 1.15f, FLT_MAX, 0.0f, buf);
    dl->AddText(font, fsm * 1.15f, ImVec2(x0 + (x1 - x0 - timeSize.x) * 0.5f, ySplit - pad * 0.5f - timeSize.y - pad), white, buf);

    const char* szLapLabel = "VUELTA";
    const ImVec2 labelSize = font->CalcTextSizeA(fsm * 0.9f, FLT_MAX, 0.0f, szLapLabel);
    dl->AddText(font, fsm * 0.9f, ImVec2(x0 + (x1 - x0 - labelSize.x) * 0.5f, ySplit + pad * 1.2f), white, szLapLabel);

    snprintf(buf, sizeof(buf), "%d/%d", ms_nHudLap, ms_nHudLaps);
    const ImVec2 lapSize = font->CalcTextSizeA(fsm * 0.9f, FLT_MAX, 0.0f, buf);
    dl->AddText(font, fsm * 0.9f, ImVec2(x0 + (x1 - x0 - lapSize.x) * 0.5f, ySplit + pad * 1.2f + labelSize.y), white, buf);

    DrawResults(dl, font, size);
}

// The original CPRACE draws its position/time panel (DRAW_RECT + DISPLAY_TEXT ST/ND/RD/TH, NUMBER, OUT_OF, TIME, TIME_0,
// RACES32 in the RACETOUR script's main loop, stages 11-12): with the standings board on screen it is dropped for the frame, as
// CHighStakesRace does (8-Track and the other VS races, 2026-11-15). The results screen (stage 13) stays.
void CVsRace::HideNativeHud()
{
    if (!CConfigLoader::ms_bVsRaceBoard || !(ms_bActive || ms_bSoloBoard) || FindRaceScript() == nullptr)
        return;
    const int stage = ScriptLvar(49);
    if (stage < 7 || stage >= 13)
        return;
    static bool logged = false;
    const int dropped = DropScriptRacePanel();
    if (dropped && !logged)
    {
        logged = true;
        CMissionMirror::LogEventf("vs-race-native-hud-hidden entries=%d stage=%d", dropped, stage);
    }
}

int CVsRace::DropScriptRacePanel()
{
    static const char* const keys[] = {"ST", "ND", "RD", "TH", "NUMBER", "OUT_OF", "TIME", "TIME_0", "RACES32"};
    int dropped = 0;
    // the rectangles of this frame are the panel's frame and backdrops (DRAW_RECT); type 0 = not drawn
    const int rects = (std::min)(static_cast<int>(CTheScripts::NumberOfIntroRectanglesThisFrame), 128);
    for (int i = 0; i < rects; ++i)
    {
        if (CTheScripts::IntroRectangles[i].type != 0)
        {
            CTheScripts::IntroRectangles[i].type = 0;
            ++dropped;
        }
    }
    // the panel's texts: an entry with an empty key is skipped
    const int texts = (std::min)(static_cast<int>(CTheScripts::NumberOfIntroTextLinesThisFrame), 96);
    for (int i = 0; i < texts; ++i)
    {
        char key[9] = {};
        memcpy(key, CTheScripts::IntroTextLines[i].gxtEntry, 8);
        for (auto k : keys)
        {
            if (!_stricmp(key, k))
            {
                CTheScripts::IntroTextLines[i].gxtEntry[0] = '\0';
                ++dropped;
                break;
            }
        }
    }
    return dropped;
}

// the live standings, drawn like CHighStakesRace's board (CFont, 640x448 virtual screen) above the position panel
void CVsRace::DrawBoard()
{
    if (!CConfigLoader::ms_bVsRaceBoard || !(ShouldDrawHud() || (ms_bSoloBoard && ms_bHudActive)))
        return;

    struct SRow
    {
        std::string name;
        bool finished;
        uint32_t tick;
        int score;
        bool local;
        bool npc = false;
        bool out = false;  // disqualified or left (state 2, or a racer silent for 5 s): last, struck out in red
    };
    std::vector<SRow> rows;
    rows.push_back({CLocalPlayer::m_Name[0] != '\0' ? CLocalPlayer::m_Name : "Vos", ms_nLocalState == 1, ms_nLocalFinishTick,
        ms_nLocalScore, true});
    const uint32_t now = GetTickCount();
    for (const SRacer& racer : ms_vRacers)
    {
        std::string name = "Jugador " + std::to_string(racer.id);
        for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
            if (pNetworkPlayer && pNetworkPlayer->m_playerId.value() == racer.id && !pNetworkPlayer->GetName().empty())
                name = pNetworkPlayer->GetName();
        SRow row{name, racer.state == 1, racer.finishTick, racer.score, false};
        row.out = racer.state == 2 || (racer.state == 0 && now - racer.lastTick > 5000);
        rows.push_back(row);
    }
    // VS with NPCs: the owner's NPC racers ("Corredor N" by their slot, like High Stakes' board); alone: the race's own ones.
    // VS against the players only has no NPC rows (HideNpcRacersAndSeparate takes them out of the race)
    if (CVsLobby::WithNpcs() || ms_bSoloBoard)
    {
        std::vector<const SNpc*> npcs;
        for (const SNpc& npc : ms_vNpcs)
            if (now - npc.lastTick < 5000)
                npcs.push_back(&npc);
        std::sort(npcs.begin(), npcs.end(), [](const SNpc* a, const SNpc* b) { return a->slot < b->slot; });
        for (size_t i = 0; i < npcs.size(); ++i)
        {
            SRow row{"Corredor " + std::to_string(i + 1), npcs[i]->state == 1, npcs[i]->finishTick, npcs[i]->score, false};
            row.npc = true;
            rows.push_back(row);
        }
    }
    std::stable_sort(rows.begin(), rows.end(), [](const SRow& a, const SRow& b)
        {
            if (a.out != b.out)
                return b.out;
            if (a.finished != b.finished)
                return a.finished;
            return a.finished ? a.tick < b.tick : a.score > b.score;
        });
    const int count = (std::min)(static_cast<int>(rows.size()), 12);

    const float sx = RsGlobal.maximumWidth / 640.0f, sy = RsGlobal.maximumHeight / 448.0f;
    const float w = 176.0f * sx, lineH = 15.0f * sy, titleH = 24.0f * sy, pad = 8.0f * sx;
    const float h = titleH + count * lineH + 6.0f * sy;
    const float x2 = RsGlobal.maximumWidth - 20.0f * sx, x1 = x2 - w;
    // where the old position panel was (bottom right, 27 from the bottom)
    const float y2 = RsGlobal.maximumHeight - 27.0f * sy, y1 = y2 - h;
    CSprite2d::DrawRect(CRect(x1, y1, x2, y2), CRGBA(0, 0, 0, 190));
    CFont::SetBackground(false, false);
    CFont::SetProportional(true);
    CFont::SetJustify(false);
    CFont::SetOrientation(ALIGN_LEFT);
    CFont::SetWrapx(RsGlobal.maximumWidth);
    CFont::SetEdge(1);
    CFont::SetDropColor(CRGBA(0, 0, 0, 255));
    CFont::SetFontStyle(FONT_PRICEDOWN);
    CFont::SetScale(0.5f * sx, 1.05f * sy);
    CFont::SetColor(CRGBA(225, 225, 225, 255));
    CFont::PrintString(x1 + pad, y1 + 2.0f * sy, const_cast<char*>("POSICIONES"));
    {
        // race clock (the script's own seconds since GO), frozen when the local player crossed the line
        const int seconds = (std::max)(0, ms_nLocalState == 1 ? ms_nLocalFinishSeconds : ms_nHudSeconds);
        char clock[16];
        sprintf_s(clock, "%d:%02d", seconds / 60, seconds % 60);
        CFont::SetOrientation(ALIGN_RIGHT);
        CFont::SetColor(ms_nLocalState == 1 ? CRGBA(172, 203, 241, 255) : CRGBA(255, 255, 255, 255));
        CFont::PrintString(x2 - pad, y1 + 2.0f * sy, clock);
        CFont::SetOrientation(ALIGN_LEFT);
    }
    CFont::SetFontStyle(FONT_SUBTITLES);
    CFont::SetScale(0.5f * sx, 0.95f * sy);
    for (int i = 0; i < count; ++i)
    {
        const SRow& r = rows[i];
        char left[48], right[16];
        sprintf_s(left, "%d. %s", i + 1, CMissionHints::ToGameText(r.name).c_str());
        // progress = (lap * 100 + checkpoint) * 10000 + ... (UpdateProgress)
        if (r.out) sprintf_s(left, "- %s", CMissionHints::ToGameText(r.name).c_str());
        if (r.out) sprintf_s(right, "DQ");
        else if (r.finished) sprintf_s(right, "META");
        else sprintf_s(right, "V%d", (std::min)(r.score / 1000000 + 1, (std::max)(ms_nHudLaps, 1)));
        CFont::SetColor(r.out ? CRGBA(210, 45, 45, 255) : r.local ? CRGBA(172, 203, 241, 255) : r.npc ? CRGBA(160, 160, 160, 255) : CRGBA(225, 225, 225, 255));
        const float y = y1 + titleH + i * lineH;
        CFont::PrintString(x1 + pad, y, left);
        CFont::PrintString(x2 - 38.0f * sx, y, right);
        if (r.out)
        {
            // the strike: a red line through the name, drawn with the text buffer of this row
            const float width = CFont::GetStringWidth(left, true, false);
            CFont::RenderFontBuffer();
            CSprite2d::DrawRect(CRect(x1 + pad, y + 7.0f * sy, x1 + pad + width, y + 8.5f * sy), CRGBA(210, 45, 45, 255));
        }
    }
    CFont::RenderFontBuffer();
}

// the results of the VS race: once the local player crossed the line, the players in the order they finish (the ones still
// racing below, by progress)
void CVsRace::DrawResults(ImDrawList* dl, ImFont* font, const ImVec2& size)
{
    if (ms_nLocalState != 1)
        return;

    struct SRow
    {
        std::string name;
        bool finished;
        int seconds;
        uint32_t tick;
        int score;
        bool local;
        bool out = false;
    };
    std::vector<SRow> rows;

    rows.push_back({CLocalPlayer::m_Name[0] != '\0' ? CLocalPlayer::m_Name : "Vos", true, ms_nLocalFinishSeconds,
        ms_nLocalFinishTick, ms_nLocalScore, true});

    const uint32_t now = GetTickCount();
    for (const SRacer& racer : ms_vRacers)
    {
        std::string name = "Jugador " + std::to_string(racer.id);
        for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
        {
            if (pNetworkPlayer && pNetworkPlayer->m_playerId.value() == racer.id && !pNetworkPlayer->GetName().empty())
                name = pNetworkPlayer->GetName();
        }
        SRow row{name, racer.state == 1, racer.finishSeconds, racer.finishTick, racer.score, false};
        row.out = racer.state == 2 || (racer.state == 0 && now - racer.lastTick > 5000);
        rows.push_back(row);
    }

    // finished first by arrival (ties by time), the others by progress, the disqualified last; stable for 3-4 players
    std::stable_sort(rows.begin(), rows.end(), [](const SRow& a, const SRow& b)
        {
            if (a.out != b.out)
                return b.out;
            if (a.finished != b.finished)
                return a.finished;
            if (a.finished)
                return a.tick != b.tick ? a.tick < b.tick : a.seconds < b.seconds;
            return a.score > b.score;
        });

    const float sx = size.x / 640.0f, sy = size.y / 448.0f;
    const float lineH = 16.0f * sy;
    const float fs = 13.4f * sy;
    const float w = 154.0f * sx;
    const float x0 = size.x * 0.5f - w * 0.5f;
    const float y0 = 72.0f * sy;
    const float h = lineH * (rows.size() + 1.6f);

    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), IM_COL32(0, 0, 0, 215));
    dl->AddRect(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), IM_COL32(134, 155, 184, 255), 0.0f, 0, 2.0f);
    dl->AddText(font, fs, ImVec2(x0 + fs * 0.6f, y0 + lineH * 0.2f), IM_COL32(201, 166, 255, 255), "Resultados");

    char buf[96];
    for (size_t i = 0; i < rows.size(); i++)
    {
        const SRow& row = rows[i];
        if (row.out)
            snprintf(buf, sizeof(buf), "- %s   descalificado", row.name.c_str());
        else if (row.finished)
            snprintf(buf, sizeof(buf), "%d. %s   %d:%02d", (int)i + 1, row.name.c_str(), row.seconds / 60, row.seconds % 60);
        else
            snprintf(buf, sizeof(buf), "%d. %s   corriendo...", (int)i + 1, row.name.c_str());

        const ImVec2 at(x0 + fs * 0.6f, y0 + lineH * (1.2f + i));
        const ImU32 color = row.out ? IM_COL32(210, 45, 45, 255) : row.local ? IM_COL32(201, 166, 255, 255) : IM_COL32(255, 255, 255, 255);
        dl->AddText(font, fs, at, color, buf);
        if (row.out)
        {
            const float nameW = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, ("- " + row.name).c_str()).x;
            dl->AddLine(ImVec2(at.x, at.y + fs * 0.55f), ImVec2(at.x + nameW, at.y + fs * 0.55f), color, 1.5f * sy);
        }
    }
}

void CVsRace::Process()
{
    bool bVs = false;

    if (!CHighStakesRace::Active() && CNetwork::m_bAuthenticated && IsRaceMissionId(CMissionMirror::GetRunningMissionId()))
    {
        for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
        {
            if (pNetworkPlayer && IsRaceMissionId(pNetworkPlayer->m_nRunningMission))
            {
                bVs = true;
                break;
            }
        }
    }

    // once started it lasts until the local race ends: a disqualified or gone rival no longer brings the game's old panel back
    const bool bLocalRace = !CHighStakesRace::Active() && CNetwork::m_bAuthenticated &&
        IsRaceMissionId(CMissionMirror::GetRunningMissionId());
    if (bVs && !ms_bActive)
        Enter();
    else if (!bLocalRace && ms_bActive)
        Exit();

    // the board in every race (the user, 16-11): alone against the NPCs it ranks them and the local player
    const bool bSolo = !ms_bActive && CConfigLoader::ms_bVsRaceBoard && !CHighStakesRace::Active() &&
        IsRaceMissionId(CMissionMirror::GetRunningMissionId());
    if (bSolo != ms_bSoloBoard)
    {
        ms_bSoloBoard = bSolo;
        ms_bHudActive = false;
        ms_vNpcs.clear();
        ms_nLocalState = 0;
        ms_nLastSendTick = 0;
    }

    if (ms_bActive)
    {
        Reassert();
        HideNpcRacersAndSeparate();
        UpdateProgress();
    }
    else if (ms_bSoloBoard)
        UpdateProgress();
}

void CVsRace::Init()
{
    Events::gameProcessEvent.after += []
    {
        Process();
    };
}
