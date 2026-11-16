#include "stdafx.h"
#include "CJustBusinessEscort.h"
#include "CConfigLoader.h"
#include "CCoopMission.h"
#include "CMissionMirror.h"
#include "CMissionSyncRules.h"
#include "COpCodeSync.h"
#include "Commands/Commands/CCommandPutNetworkPlayersInCar.h"
#include <deque>
#include <vector>

// gta-reversed VehicleRecording.cpp: StartPlaybackRecordedCar takes the first inactive of 16 playback slots without
// checking (bPlaybackGoingOn, 0x97D6F0) and reads the recording's buffer (HasRecordingFileBeenLoaded, 0x45A060), so both
// are checked here before a start.
static constexpr uintptr_t PLAYBACK_GOING_ON = 0x97D6F0;
static constexpr int PLAYBACK_SLOTS = 16;

static constexpr DWORD ESCORT_STEP_MS = 500;   // delay of the nth guest's bike: n steps behind Smoke's (owner, 2026-11-13)
static constexpr int MAX_ESCORTS = 3;
static constexpr int BIKE_MODEL = 581;                           // BF-400, Smoke's bike
static constexpr int RIDER_MODELS[MAX_ESCORTS] = {105, 106, 107};  // FAM1..FAM3, Grove Street
// the parking spots next to Smoke's bike on the roof (the three bikes the archived coop DRUGS1 made there), per escort
static constexpr float PARK_STEP_X = 1.15f, PARK_STEP_Y = -2.215f;

namespace
{
enum EventKind { EV_START, EV_STOP, EV_SPEED, EV_SKIP, EV_PAUSE, EV_UNPAUSE };

struct SEvent
{
    DWORD due;
    EventKind kind;
    int path;
    float speed;
};

struct SEscort
{
    playerid_t player;
    DWORD delay = 0;
    int slot = 0;
    int bike = -1;
    int rider = -1;
    bool failed = false;
    std::deque<SEvent> events;
};

CRunningScript* g_script = nullptr;
int g_smokeBike = -1;
CVector g_spawn;
float g_spawnHeading = 0.0f;
bool g_built = false;
std::vector<SEscort> g_escorts;
}

static bool Enabled()
{
    return CConfigLoader::ms_bJustBusinessBikeEscort && CConfigLoader::ms_bCoopJustBusiness &&
        CConfigLoader::ms_bVanillaScripts && CMissionSyncRules::IsEnabled() && CNetwork::m_bConnected &&
        CCoopMission::IsScriptAuthority();
}

static CRunningScript* FindMissionScript()
{
    for (auto script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
        if (script->m_bIsMission && !_strnicmp(script->m_szName, "DRUGS1", 8) && COpCodeSync::IsScriptSynced(script))
            return script;
    return nullptr;
}

static void Cleanup()
{
    for (auto& escort : g_escorts)
    {
        if (CVehicle* bike = CPools::GetVehicle(escort.bike))
        {
            Command<Commands::STOP_PLAYBACK_RECORDED_CAR>(escort.bike);
            Command<Commands::MARK_CAR_AS_NO_LONGER_NEEDED>(escort.bike);
        }
        if (CPools::GetPed(escort.rider))
            Command<Commands::MARK_CHAR_AS_NO_LONGER_NEEDED>(escort.rider);
    }
    if (!g_escorts.empty())
        CMissionMirror::LogEventf("just-business-escort end escorts=%d", (int)g_escorts.size());
    g_escorts.clear();
    g_built = false;
    g_smokeBike = -1;
    g_script = nullptr;
}

void CJustBusinessEscort::OnSmokeBike(CRunningScript* script, CVehicle* bike)
{
    if (!script || !bike) return;
    if (g_script != script) Cleanup();
    g_script = script;
    if (g_smokeBike != -1) return;
    g_smokeBike = CPools::GetVehicleRef(bike);
    g_spawn = bike->GetPosition();
    g_spawnHeading = bike->GetHeading();
    CMissionMirror::LogEventf("just-business-escort smoke-bike ref=%d at=%.1f,%.1f,%.1f", g_smokeBike, g_spawn.x, g_spawn.y,
        g_spawn.z);
}

// A bike of the escort: parked beside Smoke's with its Grove Street rider (driver seat, invulnerable like Smoke's bike);
// the guest is put behind him by the existing boarding queue. Nothing moves until the recordings start.
static bool CreateEscort(SEscort& escort, CRunningScript* script)
{
    const int riderModel = RIDER_MODELS[escort.slot % MAX_ESCORTS];
    for (int model : {BIKE_MODEL, riderModel})
        if (CStreaming::ms_aInfoForModel[model].m_nLoadState != LOADSTATE_LOADED)
        {
            CStreaming::RequestModel(model, eStreamingFlags::GAME_REQUIRED);
            CStreaming::LoadAllRequestedModels(false);
            if (CStreaming::ms_aInfoForModel[model].m_nLoadState != LOADSTATE_LOADED) return false;
        }
    const float n = (float)(escort.slot + 1);
    int bike = 0;
    Command<Commands::CREATE_CAR>(BIKE_MODEL, g_spawn.x + PARK_STEP_X * n, g_spawn.y + PARK_STEP_Y * n, g_spawn.z, &bike);
    CVehicle* vehicle = CPools::GetVehicle(bike);
    if (!vehicle) return false;
    Command<Commands::SET_CAR_HEADING>(bike, g_spawnHeading * 57.2957795f);
    Command<Commands::SET_CAR_PROOFS>(bike, 0, 1, 1, 1, 1);
    Command<Commands::SET_CAN_BURST_CAR_TYRES>(bike, 0);
    Command<Commands::SET_CAR_HEALTH>(bike, 2000);
    int rider = 0;
    Command<Commands::CREATE_CHAR_INSIDE_CAR>(bike, (int)PED_TYPE_MISSION1, riderModel, &rider);
    if (CPools::GetPed(rider)) Command<Commands::SET_CHAR_PROOFS>(rider, 1, 1, 1, 1, 1);
    escort.bike = bike;
    escort.rider = rider;
    // the guest behind him once the bike has its network id
    CCommandPutNetworkPlayersInCar::QueueGuestPassenger(escort.player, vehicle, 0, script, &CConfigLoader::ms_bCoopJustBusiness);
    CMissionMirror::LogEventf("just-business-escort create player=%u slot=%d delay=%u bike=%d rider=%d", escort.player.value(),
        escort.slot, (unsigned)escort.delay, bike, rider);
    return true;
}

static void BuildEscorts(CRunningScript* script)
{
    g_built = true;
    g_escorts.clear();
    for (auto player : CNetworkPlayerManager::m_pPlayers)
    {
        if ((int)g_escorts.size() >= MAX_ESCORTS) break;
        CPed* ped = player->m_pPed;
        if (!ped || player->m_bDead || !(ped->m_fHealth > 0.0f) || !CCoopMission::ShouldIncludeNetworkPlayer(player)) continue;
        SEscort escort;
        escort.player = player->m_playerId;
        escort.slot = (int)g_escorts.size();
        escort.delay = ESCORT_STEP_MS * (DWORD)(escort.slot + 1);  // 0.5 s, 1 s, 1.5 s behind Smoke
        escort.failed = !CreateEscort(escort, script);
        if (escort.failed)
            CMissionMirror::LogEventf("just-business-escort create-failed player=%u slot=%d", escort.player.value(), escort.slot);
        g_escorts.push_back(std::move(escort));
    }
}

void CJustBusinessEscort::OnCommandExecuted(CRunningScript* script, uint16_t opcode)
{
    EventKind kind;
    switch (opcode)
    {
    case 0x05EB: kind = EV_START; break;    // START_PLAYBACK_RECORDED_CAR
    case 0x05EC: kind = EV_STOP; break;     // STOP_PLAYBACK_RECORDED_CAR
    case 0x05ED: kind = EV_PAUSE; break;    // PAUSE_PLAYBACK_RECORDED_CAR
    case 0x05EE: kind = EV_UNPAUSE; break;  // UNPAUSE_PLAYBACK_RECORDED_CAR
    case 0x06C5: kind = EV_SKIP; break;     // SKIP_TO_END_AND_STOP_PLAYBACK_RECORDED_CAR
    case 0x06FD: kind = EV_SPEED; break;    // SET_PLAYBACK_SPEED
    default: return;
    }
    if (COpCodeSync::bProcessingNetworkOpcode || !script || !script->m_bIsMission || _strnicmp(script->m_szName, "DRUGS1", 8) ||
        !Enabled() || !COpCodeSync::IsScriptSynced(script))
        return;
    CVehicle* vehicle = CPools::GetVehicle(ScriptParams[0]);
    if (!vehicle) return;
    // the first playback of the bike that stands at Smoke's start, if the heading hook did not see it
    if (g_smokeBike == -1 && kind == EV_START && vehicle->m_nModelIndex == BIKE_MODEL &&
        DistanceBetweenPoints(vehicle->GetPosition(), CVector(1671.791f, -1695.106f, 19.48f)) < 4.0f)
        OnSmokeBike(script, vehicle);
    if (script != g_script || ScriptParams[0] != g_smokeBike) return;

    SEvent event{0, kind, kind == EV_START ? ScriptParams[1] : 0, kind == EV_SPEED ? *reinterpret_cast<float*>(&ScriptParams[1]) : 1.0f};
    if (!g_built)
    {
        if (kind != EV_START) return;
        BuildEscorts(script);
    }
    const DWORD now = GetTickCount();
    for (auto& escort : g_escorts)
    {
        if (escort.failed) continue;
        event.due = now + escort.delay;
        escort.events.push_back(event);
    }
}

static int FreePlaybackSlots()
{
    int free = 0;
    const bool* going = reinterpret_cast<const bool*>(PLAYBACK_GOING_ON);
    for (int i = 0; i < PLAYBACK_SLOTS; ++i)
        if (!going[i]) ++free;
    return free;
}

static void Apply(SEscort& escort, const SEvent& event)
{
    CVehicle* bike = CPools::GetVehicle(escort.bike);
    if (!bike || bike->m_fHealth <= 0.0f)
    {
        escort.failed = true;
        return;
    }
    switch (event.kind)
    {
    case EV_START:
        if (!plugin::CallAndReturn<bool, 0x45A060, int>(event.path) || FreePlaybackSlots() < 1)
        {
            CMissionMirror::LogEventf("just-business-escort start-skipped slot=%d path=%d", escort.slot, event.path);
            return;
        }
        Command<Commands::START_PLAYBACK_RECORDED_CAR>(escort.bike, event.path);
        CMissionMirror::LogEventf("just-business-escort start slot=%d path=%d", escort.slot, event.path);
        break;
    case EV_STOP: Command<Commands::STOP_PLAYBACK_RECORDED_CAR>(escort.bike); break;
    case EV_SPEED: Command<Commands::SET_PLAYBACK_SPEED>(escort.bike, event.speed); break;
    case EV_SKIP: Command<Commands::SKIP_TO_END_AND_STOP_PLAYBACK_RECORDED_CAR>(escort.bike); break;
    case EV_PAUSE: Command<Commands::PAUSE_PLAYBACK_RECORDED_CAR>(escort.bike); break;
    case EV_UNPAUSE: Command<Commands::UNPAUSE_PLAYBACK_RECORDED_CAR>(escort.bike); break;
    }
}

void CJustBusinessEscort::Process()
{
    if (g_escorts.empty() && g_smokeBike == -1 && !g_script) return;
    if (COpCodeSync::bProcessingNetworkOpcode) return;
    if (!Enabled() || FindMissionScript() != g_script)
    {
        Cleanup();
        return;
    }
    const DWORD now = GetTickCount();
    for (auto& escort : g_escorts)
    {
        while (!escort.failed && !escort.events.empty() && (int)(now - escort.events.front().due) >= 0)
        {
            const SEvent event = escort.events.front();
            escort.events.pop_front();
            Apply(escort, event);
        }
    }
}
