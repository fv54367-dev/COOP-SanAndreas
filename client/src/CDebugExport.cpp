#include "stdafx.h"
#include <cmath>
#include <CAnimBlendHierarchy.h>
#include "CDebugExport.h"
#include "CNetwork.h"
#include "CNetworkPlayerManager.h"
#include "CNetworkPedManager.h"
#include "CNetworkVehicleManager.h"
#include "CNetworkPlayer.h"
#include "CNetworkPed.h"
#include "CNetworkVehicle.h"
#include "CCoopMission.h"
#include "CMissionMirror.h"
#include "CLocalPlayer.h"
#include <string>
#include <algorithm>
#include <vector>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace
{
constexpr size_t MAX_PED_LIST = 60;
constexpr int MAX_ANIMS = 16;

void AppendF(std::string& out, const char* fmt, ...)
{
    char tmp[512];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    if (n > 0)
        out.append(tmp, (size_t)std::min<int>(n, (int)sizeof(tmp) - 1));
}

void AppendStr(std::string& out, const char* s)
{
    out += '"';
    for (; s != nullptr && *s; ++s)
    {
        unsigned char ch = (unsigned char)*s;
        if (ch == '"' || ch == '\\')
        {
            out += '\\';
            out += (char)ch;
        }
        else if (ch < 0x20)
            out += ' ';
        else
            out += (char)ch;
    }
    out += '"';
}

bool IsFiniteVec(const CVector& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// NaN / inf are not JSON: a broken position goes out as null (and is listed in bad_positions)
void AppendVec(std::string& out, const CVector& v)
{
    if (!IsFiniteVec(v))
    {
        out += "null";
        return;
    }
    AppendF(out, "{\"x\":%.3f,\"y\":%.3f,\"z\":%.3f}", v.x, v.y, v.z);
}

bool PedOk(CPed* ped)
{
    return ped != nullptr && ped->IsVTableValid();
}

// animations of a ped as a JSON array (max MAX_ANIMS)
void AppendAnims(std::string& out, CPed* ped)
{
    out += '[';
    if (PedOk(ped) && ped->m_pRwClump != nullptr)
    {
        int count = 0;
        for (CAnimBlendAssociation* assoc = RpAnimBlendClumpGetFirstAssociation(ped->m_pRwClump);
             assoc != nullptr && count < MAX_ANIMS; assoc = RpAnimBlendGetNextAssociation(assoc), ++count)
        {
            if (count)
                out += ',';
            AppendF(out, "{\"group\":%d,\"id\":%d,\"blend\":%.2f", (int)assoc->m_nAnimGroup, (int)assoc->m_nAnimId,
                assoc->m_fBlendAmount);
            if (assoc->m_pHierarchy != nullptr)
            {
                AppendF(out, ",\"hash\":%u", (unsigned)assoc->m_pHierarchy->m_hashKey);
                const int blockId = assoc->m_pHierarchy->m_nAnimBlockId;
                if (blockId >= 0 && blockId < CAnimManager::ms_numAnimBlocks)
                {
                    out += ",\"block\":";
                    AppendStr(out, CAnimManager::ms_aAnimBlocks[blockId].szName);
                }
            }
            out += '}';
        }
    }
    out += ']';
}

struct SPedEntry
{
    CNetworkPed* record;
    CPed* ped;
    CVector pos;
    float dist;
};

std::string Build()
{
    std::string out;
    out.reserve(8192);
    const uint32_t now = GetTickCount();
    CPlayerPed* local = FindPlayerPed(0);
    const bool localOk = PedOk(local);
    CVector localPos = localOk ? local->GetPosition() : CVector(0, 0, 0);

    AppendF(out, "{\"net_id\":%d,\"version\":\"%s\",\"connected\":%s,\"authenticated\":%s,\"is_host\":%s,\"area\":%d",
        (int)CNetworkPlayerManager::m_localPlayerId.value(), COOPSA_VERSION, CNetwork::m_bConnected ? "true" : "false",
        CNetwork::m_bAuthenticated ? "true" : "false", CLocalPlayer::m_bIsHost ? "true" : "false",
        localOk ? (int)local->m_nAreaCode : -1);

    // mission
    out += ",\"mission\":{";
    AppendF(out, "\"id\":%d,\"coop_running\":%s,\"coop_owner\":%s,\"script_authority\":%s,\"title\":",
        CMissionMirror::GetRunningMissionId(), CCoopMission::IsRunning() ? "true" : "false",
        CCoopMission::IsOwner() ? "true" : "false", CCoopMission::IsScriptAuthority() ? "true" : "false");
    AppendStr(out, CCoopMission::GetTitle());
    out += '}';

    out += ",\"local_position\":";
    AppendVec(out, localPos);
    out += ",\"local_anims\":";
    AppendAnims(out, localOk ? local : nullptr);

    // remote players
    out += ",\"remotes\":[";
    bool first = true;
    for (CNetworkPlayer* player : CNetworkPlayerManager::m_pPlayers)
    {
        if (player == nullptr || player->m_playerId == CNetworkPlayerManager::m_localPlayerId)
            continue;
        CPlayerPed* ped = player->m_pPed;
        const bool ok = PedOk(ped);
        if (!first)
            out += ',';
        first = false;
        AppendF(out, "{\"net_id\":%d,\"name\":", (int)player->m_playerId.value());
        AppendStr(out, player->m_Name);
        out += ",\"position\":";
        AppendVec(out, ok ? ped->GetPosition() : CVector(0, 0, 0));
        out += ",\"net_position\":";
        AppendVec(out, player->m_vecNetFootPos);
        AppendF(out, ",\"net_on_foot\":%s,\"age_ms\":%u,\"has_ped\":%s,\"area\":%d,\"in_vehicle\":%s,\"dead\":%s,\"anims\":",
            player->m_bNetOnFoot ? "true" : "false", player->m_netAliveTick ? (unsigned)(now - player->m_netAliveTick) : 4294967295u,
            ok ? "true" : "false", ok ? (int)ped->m_nAreaCode : -1,
            ok && ped->m_nPedFlags.bInVehicle ? "true" : "false", player->m_bDead ? "true" : "false");
        AppendAnims(out, ok ? ped : nullptr);
        out += '}';
    }
    out += ']';

    // peds
    int pedsMine = 0, pedsCopies = 0;
    std::vector<SPedEntry> entries;
    std::string badPositions;
    for (CNetworkPed* record : CNetworkPedManager::m_pPeds)
    {
        if (record == nullptr)
            continue;
        if (record->m_bSyncing)
            ++pedsMine;
        else
            ++pedsCopies;
        CPed* ped = PedOk(record->m_pPed) ? record->m_pPed : nullptr;
        CVector pos = ped ? ped->GetPosition() : record->m_vecLastPos;
        if (!IsFiniteVec(pos))
        {
            // a NaN distance would break the sort below (no strict weak ordering): report it instead
            AppendF(badPositions, "%s{\"ped_id\":%llu,\"mine\":%s,\"model\":%d,\"has_ped\":%s}", badPositions.empty() ? "" : ",",
                (unsigned long long)record->m_pedId.packed(), record->m_bSyncing ? "true" : "false", record->m_nModelId,
                ped ? "true" : "false");
            continue;
        }
        float dx = pos.x - localPos.x, dy = pos.y - localPos.y;
        entries.push_back({record, ped, pos, dx * dx + dy * dy});
    }
    AppendF(out, ",\"peds\":{\"mine\":%d,\"copies\":%d}", pedsMine, pedsCopies);
    out += ",\"bad_positions\":[" + badPositions + "]";
    std::sort(entries.begin(), entries.end(), [](const SPedEntry& a, const SPedEntry& b) { return a.dist < b.dist; });
    if (entries.size() > MAX_PED_LIST)
        entries.resize(MAX_PED_LIST);
    out += ",\"ped_list\":[";
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const SPedEntry& e = entries[i];
        if (i)
            out += ',';
        AppendF(out, "{\"ped_id\":%llu,\"mine\":%s,\"model\":%d,\"has_ped\":%s,\"streamed_out\":%s,\"position\":",
            (unsigned long long)e.record->m_pedId.packed(), e.record->m_bSyncing ? "true" : "false", e.record->m_nModelId,
            e.ped ? "true" : "false", e.record->m_bStreamedOut ? "true" : "false");
        AppendVec(out, e.pos);
        if (!e.record->m_bSyncing)
        {
            out += ",\"net_position\":";
            AppendVec(out, e.record->m_vecNetPos);
            AppendF(out, ",\"age_ms\":%u", e.record->m_nNetPosTick ? (unsigned)(now - e.record->m_nNetPosTick) : 4294967295u);
        }
        out += '}';
    }
    out += ']';

    // vehicles
    int vehMine = 0, vehCopies = 0;
    for (CNetworkVehicle* veh : CNetworkVehicleManager::m_pVehicles)
    {
        if (veh == nullptr)
            continue;
        if (veh->m_bSyncing)
            ++vehMine;
        else
            ++vehCopies;
    }
    AppendF(out, ",\"vehicles\":{\"mine\":%d,\"copies\":%d}}", vehMine, vehCopies);
    return out;
}
}  // namespace

extern "C" __declspec(dllexport) uint32_t sa_debug_json(char* buf, uint32_t cap)
{
    std::string json;
    try
    {
        json = Build();
    }
    catch (...)
    {
        json = "{\"error\":\"exception\"}";
    }
    const uint32_t need = (uint32_t)json.size();
    if (buf != nullptr && cap >= need)
        memcpy(buf, json.data(), need);
    return need;
}
