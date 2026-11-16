#include "stdafx.h"
#include "CPatientHandover.h"
#include "CMissionMirror.h"
#include "CCoopMission.h"
#include "CConfigLoader.h"
#include "COpCodeSync.h"

using PatientTask = Packets::Peds::PedPatientTask;

// the script asks again every time the task is not running (GetScriptTaskStatus 7): one request per patient this often
static constexpr uint32_t RESEND_MS = 2500;
// a command can arrive before the ASSIGN_PED that makes the ped ours: it waits this long
static constexpr uint32_t PENDING_MS = 3000;

struct SSentRequest
{
    pedid_t pedid{};
    playerid_t target{};
    uint32_t time = 0;
};

struct SPendingTask
{
    PatientTask packet;
    uint32_t until = 0;
};

static std::vector<SSentRequest> ms_vSent;
static std::vector<SPendingTask> ms_vPending;
// patients this game took over: released when the mission is over even if the owner never said so
static std::vector<pedid_t> ms_vTaken;

static float ParamFloat(int idx)
{
    return *reinterpret_cast<float*>(&ScriptParams[idx]);
}

static bool ShouldSend(const pedid_t& pedid, const playerid_t& target)
{
    const uint32_t now = CTimer::m_snTimeInMilliseconds;
    for (SSentRequest& sent : ms_vSent)
    {
        if (sent.pedid != pedid)
            continue;
        if (sent.target == target && now - sent.time < RESEND_MS)
            return false;
        sent.target = target;
        sent.time = now;
        return true;
    }
    ms_vSent.push_back({pedid, target, now});
    return true;
}

static void Send(PatientTask& packet)
{
    GetPacketFactory().Send(packet);
    CMissionMirror::LogEventf("patient tx action=%d ped=%s", packet.action, to_string(packet.pedid).c_str());
}

// ambulance-guest-patients: the guest whose ambulance took over each patient of the owner's script (patient ped handle)
struct SGuestMedic
{
    int pedHandle = 0;
    playerid_t player{};
};
static std::vector<SGuestMedic> ms_vGuestMedics;

static int& AmbulanLvar(int idx)
{
    return CTheScripts::LocalVariablesForCurrentMission[idx].iParam;
}

static SGuestMedic* FindGuestMedic(int pedHandle)
{
    for (SGuestMedic& medic : ms_vGuestMedics)
        if (medic.pedHandle == pedHandle)
            return &medic;
    return nullptr;
}

static CPed* GuestMedicPed(const SGuestMedic* medic, CVehicle** outVehicle = nullptr)
{
    CNetworkPlayer* player = medic ? CNetworkPlayerManager::GetPlayer(medic->player) : nullptr;
    CPed* ped = player ? player->m_pPed : nullptr;
    if (!ped || !ped->IsVTableValid() || ped->m_fHealth <= 0.0f || player->m_bDead || !ped->m_nPedFlags.bInVehicle ||
        !ped->m_pVehicle || ped->m_pVehicle->m_nModelIndex != 416 || ped->m_pVehicle->m_pDriver != ped ||
        !CCoopMission::ShouldIncludeNetworkPlayer(player))
        return nullptr;
    if (outVehicle)
        *outVehicle = ped->m_pVehicle;
    return ped;
}

static bool InBox(const CVector& a, const CVector& b, float rx, float ry, float rz, bool flat)
{
    return fabsf(a.x - b.x) <= rx && fabsf(a.y - b.y) <= ry && (flat || fabsf(a.z - b.z) <= rz);
}

bool CPatientHandover::FilterCompare(CRunningScript* pScript, uint16_t opcode, bool state)
{
    if (state || !CConfigLoader::ms_bAmbulanceGuestPatients || !CConfigLoader::ms_bPatientHandover || !pScript ||
        !pScript->m_bIsMission || COpCodeSync::bProcessingNetworkOpcode || !CCoopMission::IsActive() ||
        !CCoopMission::IsScriptAuthority() || strnicmp(pScript->m_szName, "ambulan", 7) != 0)
        return state;
    if (opcode != 0x0106 && opcode != 0x00F4 && opcode != 0x0103)
        return state;

    // 77@ = the patient this pass of the script works on, 80@ = its phase (1 look for an ambulance, 2 boarding,
    // 3 drive to the hospital)
    const int patientHandle = AmbulanLvar(77);
    const int phase = AmbulanLvar(80);
    CPed* owner = FindPlayerPed(0);
    if (!owner || ScriptParams[0] != CPools::GetPedRef(owner))
        return state;
    const bool locatePatient = opcode != 0x0103;
    if (locatePatient ? ScriptParams[1] != patientHandle : phase != 3)
        return state;
    CPed* patient = CPools::GetPed(patientHandle);
    if (!patient)
        return state;

    const float* f = reinterpret_cast<const float*>(&ScriptParams[0]);
    if (opcode == 0x0106 && phase == 1)
    {
        // an ambulance of a guest (driver) next to the patient: it is the one that takes him
        for (CNetworkPlayer* player : CNetworkPlayerManager::m_pPlayers)
        {
            if (!player || player->m_playerId == CNetworkPlayerManager::m_localPlayerId)
                continue;
            SGuestMedic candidate{patientHandle, player->m_playerId};
            CPed* medic = GuestMedicPed(&candidate);
            if (!medic || medic->m_nAreaCode != patient->m_nAreaCode ||
                !InBox(medic->GetPosition(), patient->GetPosition(), f[2], f[3], f[4], false))
                continue;
            if (SGuestMedic* old = FindGuestMedic(patientHandle))
                old->player = player->m_playerId;
            else
                ms_vGuestMedics.push_back(candidate);
            CMissionMirror::LogEventf("rule-ambulance-guest phase=1 player=%u patient=%d", player->m_playerId.value(),
                patientHandle);
            return true;
        }
        return state;
    }
    SGuestMedic* medic = FindGuestMedic(patientHandle);
    CVehicle* vehicle = nullptr;
    CPed* medicPed = GuestMedicPed(medic, &vehicle);
    if (!medicPed || medicPed->m_nAreaCode != patient->m_nAreaCode)
        return state;
    if (opcode == 0x00F4 && phase == 2)
        return InBox(medicPed->GetPosition(), patient->GetPosition(), f[2], f[3], 0.0f, true);
    if (opcode == 0x0103 && phase == 3 && patient->m_nPedFlags.bInVehicle && patient->m_pVehicle == vehicle)
    {
        // LOCATE_STOPPED_CHAR_IN_CAR_3D: ped, x, y, z, radii, draw
        const bool stopped = vehicle->m_vecMoveSpeed.Magnitude() < 0.02f;
        if (stopped && InBox(medicPed->GetPosition(), CVector(f[1], f[2], f[3]), f[4], f[5], f[6], false))
        {
            CMissionMirror::LogEventf("rule-ambulance-guest phase=3 player=%u patient=%d", medic->player.value(), patientHandle);
            return true;
        }
    }
    return state;
}

static void OnEnterCar(int pedHandle, int vehicleHandle, int seat)
{
    CPed* ped = CPools::GetPed(pedHandle);
    CVehicle* vehicle = CPools::GetVehicle(vehicleHandle);
    // the script boards the patient in the owner's ambulance, but a guest's ambulance took him (FilterCompare)
    if (CConfigLoader::ms_bAmbulanceGuestPatients && ped && FindGuestMedic(pedHandle))
    {
        CVehicle* guestVehicle = nullptr;
        if (GuestMedicPed(FindGuestMedic(pedHandle), &guestVehicle))
            vehicle = guestVehicle;
    }
    if (!ped || !vehicle || !vehicle->m_pDriver)
        return;

    CNetworkPed* networkPed = CNetworkPedManager::GetPed(ped);
    CNetworkVehicle* networkVehicle = CNetworkVehicleManager::GetVehicle(vehicle);
    if (!networkPed || !networkVehicle)
        return;

    playerid_t target{};
    if (CNetworkPlayer* driver = CNetworkPlayerManager::GetPlayer(vehicle->m_pDriver))
    {
        target = driver->m_playerId;
    }
    else if (vehicle->m_pDriver == FindPlayerPed(0) && !networkPed->m_bSyncing)
    {
        // our own ambulance and a patient another player took: it comes back, the command already runs here
        target = CNetworkPlayerManager::m_localPlayerId;
    }
    else
    {
        return;
    }

    if (!ShouldSend(networkPed->m_pedId, target))
        return;

    PatientTask packet{};
    packet.pedid = networkPed->m_pedId;
    packet.vehicleid = networkVehicle->m_vehicleId;
    packet.targetPlayerId = target;
    packet.action = PatientTask::ENTER;
    packet.param = static_cast<uint32_t>(seat);
    Send(packet);
}

// a command on a patient another player simulates: it runs there
static void ForwardToSyncer(int pedHandle, uint8_t action, const CVector& pos = CVector(), int param = 0)
{
    CPed* ped = CPools::GetPed(pedHandle);
    CNetworkPed* networkPed = ped ? CNetworkPedManager::GetPed(ped) : nullptr;
    if (!networkPed || networkPed->m_bSyncing)
        return;

    PatientTask packet{};
    packet.pedid = networkPed->m_pedId;
    packet.action = action;
    packet.pos = pos;
    packet.param = static_cast<uint32_t>(param);
    Send(packet);
}

void CPatientHandover::OnCommandExecuted(CRunningScript* pScript, uint16_t opcode)
{
    if (!CConfigLoader::ms_bPatientHandover || pScript == nullptr || COpCodeSync::bProcessingNetworkOpcode ||
        strnicmp(pScript->m_szName, "ambulan", 7) != 0)
        return;

    // the parameters the command collected are still in ScriptParams
    switch (opcode & 0x7FFF)
    {
    case COMMAND_TASK_ENTER_CAR_AS_PASSENGER:  // ped, vehicle, time, seat
        OnEnterCar(ScriptParams[0], ScriptParams[1], ScriptParams[3]);
        break;
    case COMMAND_TASK_GO_STRAIGHT_TO_COORD:  // ped, x, y, z, move state, time
        ForwardToSyncer(ScriptParams[0], PatientTask::GOTO, CVector(ParamFloat(1), ParamFloat(2), ParamFloat(3)),
            ScriptParams[4]);
        break;
    case COMMAND_EXPLODE_CHAR_HEAD:
        ForwardToSyncer(ScriptParams[0], PatientTask::EXPLODE);
        break;
    case COMMAND_MARK_CHAR_AS_NO_LONGER_NEEDED:
        ForwardToSyncer(ScriptParams[0], PatientTask::RELEASE);
        break;
    }
}

static void Forget(const pedid_t& pedid)
{
    ms_vTaken.erase(std::remove(ms_vTaken.begin(), ms_vTaken.end(), pedid), ms_vTaken.end());
}

// false: the ped is not ours yet (its ASSIGN_PED is still on the way)
static bool Apply(const PatientTask& packet)
{
    CNetworkPed* networkPed = CNetworkPedManager::GetPed(packet.pedid);
    if (!networkPed)
        return true;
    if (!networkPed->m_bSyncing || !networkPed->m_pPed)
        return false;

    CPed* ped = networkPed->m_pPed;
    const int pedRef = CPools::GetPedRef(ped);
    switch (packet.action)
    {
    case PatientTask::ENTER:
    {
        CNetworkVehicle* networkVehicle = CNetworkVehicleManager::GetVehicle(packet.vehicleid);
        if (!networkVehicle || !networkVehicle->m_pVehicle)
            return true;
        // the owner's script asks again while its copy is not seated: a boarding in progress is not restarted
        if (ped->m_nPedFlags.bInVehicle ||
            ped->m_pIntelligence->m_TaskMgr.FindActiveTaskByType(TASK_COMPLEX_ENTER_CAR_AS_PASSENGER))
            return true;
        Command<Commands::TASK_ENTER_CAR_AS_PASSENGER>(
            pedRef, CPools::GetVehicleRef(networkVehicle->m_pVehicle), -1, static_cast<int>(packet.param));
        if (std::find(ms_vTaken.begin(), ms_vTaken.end(), packet.pedid) == ms_vTaken.end())
            ms_vTaken.push_back(packet.pedid);
        break;
    }
    case PatientTask::GOTO:
        Command<Commands::TASK_GO_STRAIGHT_TO_COORD>(
            pedRef, packet.pos.x, packet.pos.y, packet.pos.z, static_cast<int>(packet.param), -1);
        break;
    case PatientTask::EXPLODE:
        Command<Commands::EXPLODE_CHAR_HEAD>(pedRef);
        Command<Commands::REMOVE_CHAR_ELEGANTLY>(pedRef);
        Forget(packet.pedid);
        break;
    case PatientTask::RELEASE:
        Command<Commands::SET_CHAR_BLEEDING>(pedRef, 0);
        Command<Commands::SET_CHAR_KEEP_TASK>(pedRef, 1);
        Command<Commands::MARK_CHAR_AS_NO_LONGER_NEEDED>(pedRef);
        Forget(packet.pedid);
        break;
    }

    CMissionMirror::LogEventf("patient rx action=%d ped=%s", packet.action, to_string(packet.pedid).c_str());
    return true;
}

void CPatientHandover::HandlePacket(const PatientTask& packet)
{
    if (!Apply(packet))
        ms_vPending.push_back({packet, CTimer::m_snTimeInMilliseconds + PENDING_MS});
}

void CPatientHandover::Process()
{
    const uint32_t now = CTimer::m_snTimeInMilliseconds;
    for (size_t i = 0; i < ms_vPending.size();)
    {
        if (Apply(ms_vPending[i].packet) || (int32_t)(now - ms_vPending[i].until) > 0)
            ms_vPending.erase(ms_vPending.begin() + i);
        else
            ++i;
    }

    // the mission ended without releasing them (failed, abandoned): they become ordinary peds again
    if (!CCoopMission::IsActive())
        ms_vGuestMedics.clear();
    if (!ms_vTaken.empty() && !CCoopMission::IsActive())
    {
        for (const pedid_t& pedid : ms_vTaken)
        {
            CNetworkPed* networkPed = CNetworkPedManager::GetPed(pedid);
            if (networkPed && networkPed->m_bSyncing && networkPed->m_pPed && networkPed->m_pPed->m_nCreatedBy == 2)
                Command<Commands::MARK_CHAR_AS_NO_LONGER_NEEDED>(CPools::GetPedRef(networkPed->m_pPed));
        }
        ms_vTaken.clear();
    }

    if (!CCoopMission::IsActive())
        ms_vSent.clear();
}
