// CPRACE route/progress adapted from Karimov-Akbar/sa-coop Race.cpp (MIT, 4135fc5).
// Native locals verified against the original main.scm, not the archived coop scripts.
#include "stdafx.h"
#include "CHighStakesRace.h"
#include "CVsRace.h"
#include "CConfigLoader.h"
#include "CCoopMission.h"
#include "CMissionMirror.h"
#include "CMissionMode.h"
#include "CMissionLock.h"
#include "CMissionHints.h"
#include "CLocalPlayer.h"
#include "CStoryProgress.h"
#include <CRadar.h>
#include <CFont.h>
#include <CSprite2d.h>
#include <string>
#include <vector>
#include "COpCodeSync.h"
#include "CMirrorRaceCheckpoints.h"
#include "network/packets/scripts.h"
#include "network/packets/mission_wait_notice.h"
#include <CTimer.h>
#include <CPools.h>
#include <algorithm>
#include <cmath>
#include <map>

namespace {
using P = Packets::Scripts::StoryRace;
constexpr int STAGE = 49, LAPS = 224, COUNT = 250, RACERS = 256, PLAYER = 442;
constexpr int RX = 454, RY = 536, RZ = 618, ROUTE_MAX = 82;
// CESAR's original globals (verified at its 03A4 and the following launch branch).
constexpr int RACE_GO = 9344, SELECTION = 1408, SHOW_MENU = 484;
// $flag_cesar_mission_counter (ScriptSpace +1828, same table as CMissionMode): CPRACE adds 1 and removes
// $cesar_contact_blip (radar sprite 13 at $cesarX/Y) when the story race is won.
constexpr int CESAR_CHAIN = 1828;
constexpr float CESAR_X = 1801.08f, CESAR_Y = -2117.92f;
int& Global(int off) { return *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[off]); }
int& Local(int n) { return reinterpret_cast<int*>(CTheScripts::LocalVariablesForCurrentMission)[n]; }
float Float(int n) { float f; const int v = Local(n); memcpy(&f, &v, 4); return f; }
CVector Route(int n) { return {Float(RX+n), Float(RY+n), Float(RZ+n)}; }
bool Named(CRunningScript* s, const char* name) { return s && _strnicmp(s->m_szName, name, 8) == 0; }
bool Finite(CVector v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
CVehicle* DriverCar(CPed* ped) {
    if (!ped || !ped->IsVTableValid() || !ped->m_nPedFlags.bInVehicle) return nullptr;
    CVehicle* v = ped->m_pVehicle;
    return v && CPools::ms_pVehiclePool->IsObjectValid(v) && v->IsVTableValid() &&
        v->m_pDriver == ped && v->m_fHealth > 0.0f ? v : nullptr;
}
bool Lowrider(CPed* ped) {
    auto v = DriverCar(ped);
    // Original 096E at 0x4784E6 checks handling flag bit 25 (LOW_RIDER).
    // Reading it directly also leaves the current script's ScriptParams untouched in UpdateCompareFlag.
    return v && v->m_pHandlingData && (v->m_pHandlingData->m_nHandlingFlags & VEHICLE_HANDLING_LOW_RIDER) != 0;
}
// Passenger of somebody's lowrider (host's or another guest's): he starts the mission riding along and does not race his
// own car; the driver's result is the one that counts (high-stakes-passenger).
// Car where the ped sits in any seat. Remote copies can have bInVehicle/m_pVehicle out of sync with the seat arrays
// (see CNetworkPlayer::DestroyPed), so with high-stakes-seat-presence the seats of the nearby cars are looked up as well.
CVehicle* SeatedCar(CPed* ped) {
    if (!ped || !ped->IsVTableValid()) return nullptr;
    if (ped->m_nPedFlags.bInVehicle) {
        CVehicle* v = ped->m_pVehicle;
        if (v && CPools::ms_pVehiclePool->IsObjectValid(v) && v->IsVTableValid()) return v;
    }
    if (!CConfigLoader::ms_bHighStakesSeatPresence) return nullptr;
    const CVector pos = ped->GetPosition();
    for (int i = 0; i < CPools::ms_pVehiclePool->m_nSize; ++i) {
        CVehicle* v = CPools::ms_pVehiclePool->GetAt(i);
        if (!v || !v->IsVTableValid() || (v->GetPosition() - pos).Magnitude() > 30.0f) continue;
        bool seated = v->m_pDriver == ped;
        for (int k = 0; k < 8 && !seated; ++k) seated = v->m_apPassengers[k] == ped;
        if (seated) return v;
    }
    return nullptr;
}
bool IsLowriderCar(CVehicle* v) {
    return v && v->m_fHealth > 0.0f && v->m_pHandlingData &&
        (v->m_pHandlingData->m_nHandlingFlags & VEHICLE_HANDLING_LOW_RIDER) != 0;
}
bool InLowrider(CPed* ped) {
    if (!CConfigLoader::ms_bHighStakesPassenger) return Lowrider(ped);
    if (Lowrider(ped)) return true;
    return IsLowriderCar(SeatedCar(ped));
}
struct Progress { int next = 0, laps = 0, finishPlace = 0; bool done = false; uint32_t sent = 0; };
std::map<playerid_t, Progress> guests;
std::vector<playerid_t> entrants;
CRunningScript* race = nullptr;
bool armed = false, owning = false, shared = false, holding = false, released = false;
uint32_t armedAt = 0, session = 0, sequence = 0, frame = 0;
uint32_t guestSession = 0, guestSequence = 0, heard = 0, retired = 0;
playerid_t guestOwner{};
int checkpoint = -1, blip = -1, frozenCar = -1, nativePlace = 1;
bool gridPlaced = false, lockedControls = false;
int chainAtStart = 0;
uint32_t lastBlipSweep = 0;
int hudPlace = 0, hudTotal = 0, hudLap = 0, hudLaps = 0;
// Race clock of the board: from GO (authority: CPRACE stage 11; guest: first RACING snapshot) until this player finishes.
uint32_t clockStart = 0, clockStop = 0;
bool nativeHudLogged = false;
// Standings table: the authority builds it from the CPRACE tables and the guests' progress; guests receive it.
using Row = P::BoardRow;
Row board[P::BOARD_MAX]; int boardCount = 0; uint32_t boardAt = 0;
std::map<std::string, uint32_t> doneOrder; uint32_t doneSeq = 0;
CVector marked{};
int markedType = -1;
bool ActiveScript(CRunningScript* wanted) {
    for (auto s = CTheScripts::pActiveScripts; s; s = s->m_pNext)
        if (s == wanted) return Named(s, "CPRACE");
    return false;
}
void Unmark() {
    if (checkpoint != -1) Command<Commands::DELETE_CHECKPOINT>(checkpoint);
    if (blip != -1) Command<Commands::REMOVE_BLIP>(blip);
    checkpoint = blip = -1; markedType = -1;
}
void Unlock() {
    if (frozenCar != -1) {
        if (auto v = CPools::GetVehicle(frozenCar))
            Command<Commands::FREEZE_CAR_POSITION>(frozenCar, false);
        frozenCar = -1;
    }
    if (lockedControls) {
        if (auto ped = FindPlayerPed(0); ped && ped->m_fHealth > 0.0f)
            Command<Commands::SET_PLAYER_CONTROL>(0, true);
        lockedControls = false;
    }
}
void GuestEnd() {
    Unmark(); Unlock(); retired = guestSession; guestSession = 0; boardCount = 0;
    guestSequence = 0; heard = 0; guestOwner = {}; gridPlaced = false; clockStart = clockStop = 0;
}
bool ValidRoute() {
    if (Local(COUNT) < 1 || Local(COUNT) > ROUTE_MAX || Local(LAPS) < 1 || Local(LAPS) > 20 ||
        Local(RACERS) < 1 || Local(RACERS) > 16 || Local(PLAYER) < 0 || Local(PLAYER) >= Local(RACERS)) return false;
    for (int i = 0; i < Local(COUNT); ++i) if (!Finite(Route(i))) return false;
    return Float(322) > 0 && Float(322) <= 100 && Float(323) > 0 && Float(323) <= 100 &&
        Float(324) > 0 && Float(324) <= 100;
}
void SendPrepare(playerid_t id, Progress& t) {
    if (GetTickCount() - t.sent < 500) return;
    t.sent = GetTickCount();
    P p{}; p.owner = CNetworkPlayerManager::m_localPlayerId; p.target = id;
    p.session = session; p.sequence = ++sequence; p.phase = P::PREPARE;
    p.total = uint8_t(1 + guests.size());
    GetPacketFactory().Send(p);
}
// Until the countdown: the racers are the entrants that drive a lowrider right now; the others get a hint to board one.
void RefreshGuests() {
    static uint32_t hintAt = 0;
    const uint32_t now = GetTickCount();
    const bool hint = now - hintAt > 2000;
    if (hint) hintAt = now;
    for (auto id : entrants) {
        auto p = CNetworkPlayerManager::GetPlayer(id);
        if (!p || !CCoopMission::ShouldIncludeNetworkPlayer(p)) { guests.erase(id); continue; }
        if (p->m_pPed && !p->m_bDead && Lowrider(p->m_pPed)) { guests.emplace(id, Progress{}); continue; }
        guests.erase(id);
        if (!hint) continue;
        P h{}; h.owner = CNetworkPlayerManager::m_localPlayerId; h.target = id;
        h.session = session; h.sequence = ++sequence; h.phase = P::PREPARE; h.type = 8; h.total = 1;
        GetPacketFactory().Send(h);
    }
}
// The story race was won: the contact icon of Cesar must go for everybody. Only that sprite at his spot.
void RemoveCesarContactBlip() {
    // His spot is $cesarX/$cesarY (ScriptSpace +1920/+1924, original INITIAL 02A7); the constants are the fallback.
    float cx = CESAR_X, cy = CESAR_Y;
    float gx, gy; memcpy(&gx, &CTheScripts::ScriptSpace[1920], 4); memcpy(&gy, &CTheScripts::ScriptSpace[1924], 4);
    if (std::isfinite(gx) && std::isfinite(gy) && std::fabs(gx) > 1.0f && std::fabs(gy) > 1.0f) { cx = gx; cy = gy; }
    for (int i = 0; i < MAX_RADAR_TRACES; ++i) {
        const tRadarTrace& t = CRadar::ms_RadarTrace[i];
        if (!t.m_bInUse || t.m_nRadarSprite != RADAR_SPRITE_CESARVIAPANDO) continue;
        if (std::fabs(t.m_vecPos.x - cx) > 6.0f || std::fabs(t.m_vecPos.y - cy) > 6.0f) continue;
        CRadar::ClearActualBlip(i);
        CMissionMirror::LogEventf("high-stakes-contact-blip-removed index=%d", i);
    }
}
// Result of the whole coop team (high-stakes-team-result): CPRACE decides win/loss with the host's own place ($450@ == 1),
// so the best place among the host and the guests that finished is the one handed to the script.
int TeamPlace() {
    int best = nativePlace;
    if (!CConfigLoader::ms_bHighStakesTeamResult) return best;
    for (const auto& entry : guests)
        if (entry.second.done && entry.second.finishPlace > 0) best = (std::min)(best, entry.second.finishPlace);
    if (best != nativePlace)
        CMissionMirror::LogEventf("high-stakes-team-place host=%d team=%d", nativePlace, best);
    return best;
}
void EndOwner() {
    const bool passed = CConfigLoader::ms_bHighStakesEndCleanup && shared && race && Global(CESAR_CHAIN) > chainAtStart;
    if (race && ActiveScript(race) && holding) { Local(STAGE) = 13; Local(450) = TeamPlace(); }
    for (auto& entry : guests) {
        P p{}; p.owner = CNetworkPlayerManager::m_localPlayerId; p.target = entry.first;
        p.session = session; p.sequence = ++sequence; p.phase = P::END; p.type = passed ? 1 : 0;
        GetPacketFactory().Send(p);
    }
    // Entrants that never raced (no lowrider) learn the result too.
    if (shared) for (auto id : entrants) {
        if (guests.count(id)) continue;
        P p{}; p.owner = CNetworkPlayerManager::m_localPlayerId; p.target = id;
        p.session = session; p.sequence = ++sequence; p.phase = P::END; p.type = passed ? 1 : 0;
        GetPacketFactory().Send(p);
    }
    if (passed) { CStoryProgress::OnMissionPassed(); RemoveCesarContactBlip(); }
    race = nullptr; owning = shared = holding = false; guests.clear(); session = 0; boardCount = 0;
}
struct Racer { std::string name; bool human; int laps, next; bool done; float dist; uint32_t order; };
void BuildBoard() {
    std::vector<Racer> racers;
    auto distanceTo = [](CPed* ped, int next) {
        return ped ? (ped->GetPosition() - Route((std::clamp)(next, 0, Local(COUNT)-1))).Magnitude() : 1.0e9f;
    };
    auto finished = [&](const std::string& name, bool done) {
        if (!done) return 0u;
        auto it = doneOrder.find(name);
        if (it == doneOrder.end()) it = doneOrder.emplace(name, ++doneSeq).first;
        return it->second;
    };
    int npc = 0;
    for (int i = 0; i < Local(RACERS); ++i) {
        auto ped = CPools::GetPed(Local(395+i));
        const bool me = i == Local(PLAYER);
        if (!me && (!ped || ped->m_fHealth <= 0)) continue;
        std::string name = me ? std::string(CLocalPlayer::m_Name) : "Corredor " + std::to_string(++npc);
        const bool done = Local(273+i) >= Local(LAPS) || Float(363+i) == 99999.0f;
        racers.push_back({name, me, Local(273+i), Local(257+i), done, distanceTo(ped, Local(257+i)), finished(name, done)});
    }
    for (const auto& entry : guests) {
        auto p = CNetworkPlayerManager::GetPlayer(entry.first);
        if (!p) continue;
        const auto& t = entry.second;
        racers.push_back({p->GetName(), true, t.laps, t.next, t.done, distanceTo(p->m_pPed, t.next), finished(p->GetName(), t.done)});
    }
    std::sort(racers.begin(), racers.end(), [](const Racer& a, const Racer& b) {
        if (a.done != b.done) return a.done;
        if (a.done) return a.order < b.order;
        if (a.laps != b.laps) return a.laps > b.laps;
        if (a.next != b.next) return a.next > b.next;
        return a.dist < b.dist;
    });
    boardCount = (std::min)(int(racers.size()), int(P::BOARD_MAX));
    for (int i = 0; i < boardCount; ++i) {
        board[i] = Row{};
        strncpy_s(board[i].name, racers[i].name.c_str(), _TRUNCATE);
        board[i].kind = racers[i].human ? 1 : 0;
        board[i].lap = uint8_t((std::clamp)(racers[i].laps + 1, 1, (std::max)(1, Local(LAPS))));
        board[i].done = racers[i].done ? 1 : 0;
    }
    boardAt = GetTickCount();
}
int BoardPlace(const std::string& name) {
    for (int i = 0; i < boardCount; ++i) if (name == board[i].name) return i + 1;
    return 0;
}
int Place(playerid_t id, const Progress& t, CVector pos) {
    int place = 1;
    for (int i = 0; i < Local(RACERS); ++i) {
        auto ped = CPools::GetPed(Local(395+i));
        if (!ped || ped->m_fHealth <= 0) continue;
        const int laps = Local(273+i), next = Local(257+i);
        bool ahead = laps > t.laps || (laps == t.laps && next > t.next);
        if (laps == t.laps && next == t.next && !t.done) {
            CVector d = ped->GetPosition() - Route((std::min)(next, Local(COUNT)-1));
            CVector mine = pos - Route(t.next);
            ahead = d.Magnitude() < mine.Magnitude();
        }
        if (ahead) ++place;
    }
    for (const auto& entry : guests) {
        auto p = CNetworkPlayerManager::GetPlayer(entry.first);
        if (entry.first == id || !p || !p->m_pPed || p->m_bDead) continue;
        const auto& other = entry.second;
        bool ahead = other.laps > t.laps || (other.laps == t.laps && other.next > t.next);
        if (other.laps == t.laps && other.next == t.next && !t.done) {
            CVector d = p->m_pPed->GetPosition() - Route(t.next), mine = pos - Route(t.next);
            ahead = d.Magnitude() < mine.Magnitude();
        }
        if (ahead) ++place;
    }
    return place;
}
void SendState(playerid_t id, Progress& t, int rank) {
    const uint32_t now = GetTickCount();
    if (now - t.sent < 200) return;
    t.sent = now;
    P p{}; p.owner = CNetworkPlayerManager::m_localPlayerId; p.target = id;
    p.session = session; p.sequence = ++sequence;
    const int stage = Local(STAGE);
    p.phase = t.done ? P::FINISHED : stage < 7 ? P::PREPARE : stage < 11 ? P::COUNTDOWN : P::RACING;
    const int index = t.done ? Local(COUNT)-1 : t.next;
    CVector n = Route(index), a = Route((index+1) % Local(COUNT));
    p.next[0] = n.x; p.next[1] = n.y; p.next[2] = n.z;
    p.after[0] = a.x; p.after[1] = a.y; p.after[2] = a.z;
    p.type = t.laps == Local(LAPS)-1 && index == Local(COUNT)-1 ? 1 :
        uint8_t((std::clamp)(Local(205), 0, 8));
    p.size = (std::clamp)(Float(325), 1.0f, 15.0f);
    auto me = FindPlayerPed(0);
    // The grid is laid out behind a car that faces the start: the host's own car when he drives. When the host rides along
    // (high-stakes-seat-presence: a guest drives and the host is his passenger) he has no car of his own and the heading went
    // out as 0 (north), so the guest's car stood sideways on the grid (2026-11-15): use a native racer's car then.
    CVehicle* v = DriverCar(me);
    if (!v) for (int i = 0; i < Local(RACERS) && !v; ++i) {
        if (i == Local(PLAYER)) continue;
        auto car = CPools::GetVehicle(Local(379+i));
        if (car && car->m_fHealth > 0.0f) v = car;
    }
    CVector grid = me->GetPosition();
    if (v) {
        grid = v->GetPosition() - v->GetForward() * (12.0f + rank * 7.0f) + v->GetRight() * (rank % 2 ? -5.0f : 5.0f);
        // Do not put a guest inside an NPC car on the native grid.
        for (int attempt = 0; attempt < 16; ++attempt) {
            bool clear = true;
            for (int i = 0; i < Local(RACERS); ++i) {
                auto car = CPools::GetVehicle(Local(379+i));
                if (car && (car->GetPosition()-grid).Magnitude() < 5.0f) clear = false;
            }
            if (clear) break;
            grid -= v->GetForward() * 6.0f;
        }
        p.heading = v->GetHeading() * 57.2957795f;
    }
    p.grid[0] = grid.x; p.grid[1] = grid.y; p.grid[2] = grid.z;
    p.place = uint8_t(t.finishPlace ? t.finishPlace : Place(id, t, CNetworkPlayerManager::GetPlayer(id)->m_pPed->GetPosition()));
    p.total = uint8_t(Local(RACERS) + guests.size());
    if (CConfigLoader::ms_bHighStakesBoard) {
        if (!t.finishPlace) if (int onBoard = BoardPlace(CNetworkPlayerManager::GetPlayer(id)->GetName())) p.place = uint8_t(onBoard);
        p.rows = uint8_t(boardCount);
        for (int i = 0; i < boardCount; ++i) p.board[i] = board[i];
    }
    if (p.place > p.total) p.total = p.place;
    p.lap = uint8_t((std::min)(t.laps+1, Local(LAPS))); p.laps = uint8_t(Local(LAPS));
    GetPacketFactory().Send(p);
}
}

// high-stakes-restore-seat: Cesar's cutscene takes the guest out of his car. The car and the seat he had before the mission
// are remembered; when the scene ends he is warped back into the same seat (same natives as the scripted boarding:
// WARP_CHAR_INTO_CAR / WARP_CHAR_INTO_CAR_AS_PASSENGER, 05CA seat = index of m_apPassengers).
static struct {
    CVehicle* car = nullptr; int ref = -1; int seat = -2;   // -1 driver, 0..7 passenger index, -2 none
    bool frozen = false, sawScene = false, done = false;
    uint32_t startedAt = 0, sceneEndAt = 0;
} mySeat;
static void ProcessSeatRestore() {
    auto me = FindPlayerPed(0);
    if (!CConfigLoader::ms_bHighStakesRestoreSeat || !CHighStakesRace::Enabled() || CCoopMission::IsScriptAuthority() || CMissionMode::IsPlayingAlone() ||
        !me || me->m_fHealth <= 0.0f) { mySeat = {}; return; }
    const uint32_t now = GetTickCount();
    if (CMissionMirror::GetRunningMissionId() != 36) {
        if (mySeat.frozen) mySeat = {};
        // before the mission: car and seat as they are right now (only inside a lowrider)
        CVehicle* v = me->m_nPedFlags.bInVehicle ? SeatedCar(me) : nullptr;
        if (!v || !IsLowriderCar(v)) { mySeat = {}; return; }
        int seat = v->m_pDriver == me ? -1 : -2;
        for (int k = 0; k < 8 && seat == -2; ++k) if (v->m_apPassengers[k] == me) seat = k;
        mySeat.car = v; mySeat.ref = CPools::GetVehicleRef(v); mySeat.seat = seat;
        return;
    }
    if (mySeat.seat == -2 || mySeat.done) return;
    if (!mySeat.frozen) { mySeat.frozen = true; mySeat.startedAt = now; }
    if (CCutsceneMgr::ms_running) { mySeat.sawScene = true; mySeat.sceneEndAt = 0; return; }
    // the scene of the guest may not go through the game's cutscene manager: out of the car 4 s into the mission counts too
    if (!mySeat.sawScene && !me->m_nPedFlags.bInVehicle && now - mySeat.startedAt > 4000) mySeat.sawScene = true;
    if (!mySeat.sawScene) {
        if (now - mySeat.startedAt > 60000) mySeat.done = true;   // no scene came
        return;
    }
    if (!mySeat.sceneEndAt) mySeat.sceneEndAt = now;
    if (now - mySeat.sceneEndAt < 700) return;
    CVehicle* v = CPools::GetVehicle(mySeat.ref);
    if (me->m_nPedFlags.bInVehicle || guestSession != 0 || now - mySeat.startedAt > 180000 || !v || v != mySeat.car || v->m_fHealth <= 0.0f ||
        v->m_nAreaCode != me->m_nAreaCode) {
        CMissionMirror::LogEventf("high-stakes-restore-seat skipped inCar=%d car=%d", me->m_nPedFlags.bInVehicle ? 1 : 0, v ? 1 : 0);
        mySeat.done = true; return;
    }
    const int ped = CPools::GetPedRef(me), car = mySeat.ref;
    if (mySeat.seat == -1 && !v->m_pDriver) Command<Commands::WARP_CHAR_INTO_CAR>(ped, car);
    else if (mySeat.seat >= 0 && mySeat.seat < v->m_nMaxPassengers && !v->m_apPassengers[mySeat.seat])
        Command<Commands::WARP_CHAR_INTO_CAR_AS_PASSENGER>(ped, car, mySeat.seat);
    else { CMissionMirror::LogEventf("high-stakes-restore-seat seat-taken seat=%d", mySeat.seat); mySeat.done = true; return; }
    CMissionMirror::LogEventf("high-stakes-restore-seat warped seat=%d", mySeat.seat);
    mySeat.done = true;
}

bool CHighStakesRace::Enabled() {
    return CConfigLoader::ms_bHighStakesCoop && CConfigLoader::ms_bVanillaScripts && CNetwork::m_bAuthenticated;
}
bool CHighStakesRace::Active() { return Enabled() && (owning || guestSession != 0); }
bool CHighStakesRace::OwnMarkers() { return Enabled() && guestSession != 0; }

bool CHighStakesRace::FilterStart(CRunningScript* s, uint16_t op, bool state) {
    if (!Enabled() || !s || !state) return state;
    // CPRACE's early "229@ > 0" failure print follows the assignment to stage 13.
    // Defer this single message with the result, not the NPC updates (unique signature in original CPRACE).
    const uint8_t earlyFail[] = {3, 0xE5, 0, 4, 0};
    if (owning && s == race && holding && op == 0x0019 && s->m_pCurrentIP >= s->m_pBaseIP + 7 &&
        memcmp(s->m_pCurrentIP - sizeof earlyFail, earlyFail, sizeof earlyFail) == 0) return false;
    const bool launch = Named(s, "CESAR") && !s->m_bIsMission && op == 0x03EE;
    const float* f = reinterpret_cast<const float*>(ScriptParams);
    const bool grid = Named(s, "CESAR1") && op == 0x0100 && ScriptParams[7] != 0 &&
        f[4] == 4.0f && f[5] == 4.0f && f[6] == 4.0f;
    if (!launch && !grid) return state;
    if (launch && CMissionLock::IsLaunchBlocked(s, 36)) return false;
    if (grid && CConfigLoader::ms_bHighStakesNoGridWait) {
        // The race itself puts every guest on the grid wherever he is: nobody waits for anybody at this marker.
        return (CConfigLoader::ms_bHighStakesSeatPresence ? InLowrider(FindPlayerPed(0)) : Lowrider(FindPlayerPed(0))) ? state : false;
    }
    const bool coop = !CMissionMode::IsPlayingAlone();
    const bool authority = !coop || CCoopMission::IsScriptAuthority();
    // A guest without the old early return: it also has to see who the host waits for (guest-start-notice).
    if (!authority && !CConfigLoader::ms_bHighStakesGuestNotice) return false;
    auto me = FindPlayerPed(0);
    // Original CESAR prints CES_CK1 itself when this player is not in a lowrider. The authority runs the race, so he drives;
    // a guest may also ride along in anybody's lowrider.
    // high-stakes-seat-presence: the host may also ride along (the native launcher accepts any seat: LocateAnyMeans3D +
    // StoreCarIsInNoSave + IsLowRider of the car), e.g. a guest drives and the host is the passenger.
    if ((authority && !CConfigLoader::ms_bHighStakesSeatPresence) ? !Lowrider(me) : !InLowrider(me)) return false;
    std::string missing, detail;
    if (coop) for (auto p : CNetworkPlayerManager::m_pPlayers) {
        if (!p->m_pPed || !CCoopMission::ShouldIncludeNetworkPlayer(p)) continue;
        if (p->m_bDead || (launch && p->m_nRunningMission >= 0) || !InLowrider(p->m_pPed) || p->m_pPed->m_nAreaCode != me->m_nAreaCode ||
            (p->m_pPed->GetPosition()-me->GetPosition()).Magnitude() > 12.0f) {
            if (!missing.empty()) missing += ", "; missing += p->GetName();
            char why[96];
            sprintf_s(why, " [%s seated=%d lowrider=%d area=%d dist=%.1f]", p->GetName().c_str(), SeatedCar(p->m_pPed) ? 1 : 0,
                InLowrider(p->m_pPed) ? 1 : 0, p->m_pPed->m_nAreaCode == me->m_nAreaCode ? 1 : 0,
                (p->m_pPed->GetPosition()-me->GetPosition()).Magnitude());
            detail += why;
        }
    }
    if (!authority) {
        // The host runs the mission and is not in the marker: tell this guest who is missing and the host that he is awaited.
        if (launch && !missing.empty()) {
            CMissionHints::LocalStartWait(missing);
            if (!CMissionHints::WaitNoticesEnabled()) CMissionHints::Help("Esperando lowrider y cercania de: " + missing);
            static uint32_t lastGuest = 0;
            if (GetTickCount()-lastGuest > 5000) {
                lastGuest = GetTickCount();
                CMissionMirror::LogEventf("high-stakes-guest-wait missing=%s%s", missing.c_str(), detail.c_str());
            }
        }
        return false;
    }
    if (!missing.empty()) {
        // Everybody else sees "Esperando a X" (or "Te esperan" if they are X) through the common wait notice.
        CMissionHints::AuthorityWait(Packets::Scripts::MissionWaitNotice::START, missing);
        static uint32_t last = 0;
        if (GetTickCount()-last > 3000) {
            last = GetTickCount();
            CChat::AddMessage("{c7a6ff}[High Stakes]{ffffff} Esperando lowrider y cercania de: %s", missing.c_str());
            if (!CMissionHints::WaitNoticesEnabled()) CMissionHints::Help(std::string("Esperando lowrider y cercania de: ") + missing);
            CMissionMirror::LogEventf("high-stakes-start-wait missing=%s%s", missing.c_str(), detail.c_str());
        }
        return false;
    }
    if (launch) {
        entrants.clear();
        if (coop) for (auto p : CNetworkPlayerManager::m_pPlayers)
            if (CCoopMission::ShouldIncludeNetworkPlayer(p)) entrants.push_back(p->m_playerId);
        armed = true; armedAt = GetTickCount();
    }
    return true;
}

// CESAR launches the story race with $race_selection = 0, $show_race_selection = 0 and $cs1_race_is_go = 1/2
// (original CESAR). The street races of the race club never have that combination: this is the story race even when
// StartRace cannot take it over (not armed, no signature), so the generic VS offer must not hold it either.
bool CHighStakesRace::IsStoryLaunch(CRunningScript* s) {
    return Enabled() && Named(s, "CPRACE") && Global(SELECTION) == 0 && Global(SHOW_MENU) == 0 &&
        (Global(RACE_GO) == 1 || Global(RACE_GO) == 2);
}

bool CHighStakesRace::StartRace(CRunningScript* s) {
    if (owning && race == s) EndOwner();
    if (Enabled() && Named(s, "CPRACE") && (!armed || Global(SELECTION) != 0 || Global(SHOW_MENU) != 0 ||
        (Global(RACE_GO) != 1 && Global(RACE_GO) != 2)))
        CMissionMirror::LogEventf("high-stakes-race-not-story armed=%d selection=%d menu=%d go=%d", armed ? 1 : 0,
            Global(SELECTION), Global(SHOW_MENU), Global(RACE_GO));
    if (!Enabled() || !armed || !Named(s, "CPRACE") || Global(SELECTION) != 0 || Global(SHOW_MENU) != 0 ||
        (Global(RACE_GO) != 1 && Global(RACE_GO) != 2)) return false;
    // Original CPRACE (main.scm mission 35 = table entry 36) does NOT begin with SCRIPT_NAME: it starts with a GOSUB (0050)
    // to the sub at +112 and the 03A4 'CPRACE' sits at +126, so the old check on the first 11 bytes never matched and the
    // story race fell into the generic VS lobby. The hook runs right after 03A4: look at the 11 bytes just executed.
    // Never read an unknown mission's locals as a route.
    const uint8_t signature[] = {0xA4, 0x03, 9, 'C','P','R','A','C','E',0,0};
    constexpr ptrdiff_t NAME_OFFSET = 126;
    if (!s->m_pBaseIP || s->m_pBaseIP[0] != 0x50 || memcmp(s->m_pBaseIP + NAME_OFFSET, signature, sizeof signature) != 0) {
        CMissionMirror::LogEventf("high-stakes-race-not-story reason=signature base0=%02x", s->m_pBaseIP ? s->m_pBaseIP[0] : 0);
        return false;
    }
    race = s; owning = true; armed = false; shared = CCoopMission::IsScriptAuthority();
    holding = released = false; session = GetTickCount() | 1u; sequence = 0; guests.clear();
    doneOrder.clear(); doneSeq = 0; boardCount = 0; chainAtStart = Global(CESAR_CHAIN);
    clockStart = clockStop = 0; nativeHudLogged = false;
    if (shared) {
        if (CConfigLoader::ms_bHighStakesNoGridWait) {
            entrants.clear();
            for (auto p : CNetworkPlayerManager::m_pPlayers)
                if (CCoopMission::ShouldIncludeNetworkPlayer(p)) entrants.push_back(p->m_playerId);
        }
        for (auto id : entrants) {
            auto p = CNetworkPlayerManager::GetPlayer(id);
            if (p && CCoopMission::ShouldIncludeNetworkPlayer(p) &&
                (!CConfigLoader::ms_bHighStakesNoGridWait || (p->m_pPed && Lowrider(p->m_pPed))))
                guests.emplace(id, Progress{});
        }
        bool found = false;
        for (size_t i = 0; i < COpCodeSync::ms_iFreeSyncedScript; ++i)
            found |= _strnicmp(COpCodeSync::ms_aszSyncedScripts[i], "CPRACE", 8) == 0;
        if (!found && COpCodeSync::ms_iFreeSyncedScript < ARRAY_SIZE(COpCodeSync::ms_aszSyncedScripts)) {
            strncpy_s(COpCodeSync::ms_aszSyncedScripts[COpCodeSync::ms_iFreeSyncedScript++], 8, "CPRACE", 7);
        }
    }
    for (auto& entry : guests) SendPrepare(entry.first, entry.second);
    CMissionMirror::LogEventf("high-stakes-race-start guests=%u session=%u", unsigned(guests.size()), session);
    return true;
}

void CHighStakesRace::AfterCommand(CRunningScript* s, uint16_t op) {
    if (!Enabled() || COpCodeSync::bProcessingNetworkOpcode) return;
    if (Named(s, "CESAR1")) { armed = true; armedAt = GetTickCount(); }
    if (owning && s == race && shared && CConfigLoader::ms_bHighStakesEndCleanup && Global(CESAR_CHAIN) > chainAtStart)
        CStoryProgress::OnMissionPassed();
    if (owning && s == race && Local(STAGE) == 13 && !released && !guests.empty() && ValidRoute()) {
        // Keep stage 12 processing the NPCs. The finished player's next Z is set to 99999 by CPRACE.
        // Only the stage-13 result is deferred; no script sleeps and no NPC loses its next driving task.
        if (!holding) nativePlace = Local(450);
        holding = true;
        Local(STAGE) = 12;
    }
}

void CHighStakesRace::Process() {
    const uint32_t now = GetTickCount(), currentFrame = CTimer::m_FrameCounter;
    if (currentFrame < frame) {
        // The game has already recreated pools and script slots; never delete old handles in the new world.
        race = nullptr; guests.clear(); entrants.clear(); owning = shared = holding = armed = false;
        checkpoint = blip = frozenCar = -1; lockedControls = gridPlaced = false;
        retired = guestSession; guestSession = guestSequence = heard = 0; guestOwner = {}; clockStart = clockStop = 0;
    }
    frame = currentFrame;
    // The story race (counter $flag_cesar_mission_counter >= 1, original CPRACE removes $cesar_contact_blip and adds 1) is done:
    // Cesar's icon must not stay on the radar, whoever won it and however it was run (alone, VS menu, coop). The original
    // INITIAL also re-creates the blip (02A7 at $cesarX/Y) in a refresh routine, so look again every second.
    if (CConfigLoader::ms_bHighStakesCoop && CConfigLoader::ms_bHighStakesEndCleanup && CConfigLoader::ms_bVanillaScripts &&
        Global(CESAR_CHAIN) >= 1 && now - lastBlipSweep > 1000) {
        lastBlipSweep = now;
        RemoveCesarContactBlip();
    }
    if (!Enabled()) { EndOwner(); GuestEnd(); armed = false; mySeat = {}; return; }
    ProcessSeatRestore();
    if (guestSession && (now-heard > 4000 || CMissionMode::IsIndividual() || CCoopMission::HasLocalLeft() ||
        !CNetworkPlayerManager::GetPlayer(guestOwner) || !FindPlayerPed(0) || FindPlayerPed(0)->m_fHealth <= 0)) GuestEnd();
    if (armed && now-armedAt > 120000 && CMissionMirror::GetRunningMissionId() != 36) armed = false;
    if (!owning) return;
    if (!ActiveScript(race) || !FindPlayerPed(0)) { EndOwner(); return; }
    if (FindPlayerPed(0)->m_fHealth <= 0.0f) { released = true; holding = false; return; }
    if (shared && CConfigLoader::ms_bHighStakesNoGridWait && (!ValidRoute() || Local(STAGE) < 11)) RefreshGuests();
    if (!ValidRoute() || Local(STAGE) < 7) {
        // Mark the story race before any NPC is spawned; refresh while models or the grid are loading.
        // Otherwise the generic VS detector can remove NPC copies before the first countdown snapshot.
        for (auto& entry : guests) SendPrepare(entry.first, entry.second);
        return;
    }
    const CVector finish = Route(Local(COUNT)-1);
    if (Local(STAGE) >= 11 && !clockStart) clockStart = now;
    if (clockStart && !clockStop && (holding || released || Local(STAGE) >= 13)) clockStop = now;
    if (CConfigLoader::ms_bHighStakesBoard && shared && Local(STAGE) >= 11) BuildBoard();
    bool all = true; int rank = 0;
    for (auto it = guests.begin(); it != guests.end();) {
        auto p = CNetworkPlayerManager::GetPlayer(it->first);
        if (!p || !CCoopMission::ShouldIncludeNetworkPlayer(p)) { it = guests.erase(it); continue; }
        if (p->m_bDead || (p->m_pPed && p->m_pPed->m_fHealth <= 0)) {
            guests.clear(); holding = false; race->m_nWakeTime = 0; CCoopMission::RequestMissionFail(); return;
        }
        auto& t = it->second;
        auto v = DriverCar(p->m_pPed);
        if (v && Lowrider(p->m_pPed) && p->m_pPed->m_nAreaCode == FindPlayerPed(0)->m_nAreaCode && Local(STAGE) >= 11 && !t.done) {
            const CVector d = v->GetPosition() - Route(t.next);
            if (std::fabs(d.x) < Float(322) && std::fabs(d.y) < Float(323) && std::fabs(d.z) < Float(324)) {
                if (++t.next >= Local(COUNT)) { t.next = 0; ++t.laps; }
                t.done = t.laps >= Local(LAPS);
            }
            if (holding && (v->GetPosition()-finish).Magnitude() < 12.0f) t.done = true;
        }
        if (t.done && !t.finishPlace) {
            t.finishPlace = 1;
            for (int i = 0; i < Local(RACERS); ++i)
                if (Local(273+i) >= Local(LAPS) || Float(363+i) == 99999.0f) ++t.finishPlace;
            for (const auto& other : guests)
                if (other.first != it->first && other.second.finishPlace) ++t.finishPlace;
            CMissionMirror::LogEventf("high-stakes-finish player=%u place=%d", it->first.value(), t.finishPlace);
        }
        if (!t.done) all = false;
        if (p->m_pPed) SendState(it->first, t, rank++);
        ++it;
    }
    if (CConfigLoader::ms_bHighStakesFinishNotice && shared && Local(STAGE) >= 11 && !released) {
        // The mission ends when every racer reached the last marker: say who is still missing (host and guests).
        std::string missing; bool anyDone = holding;
        for (const auto& entry : guests) {
            if (entry.second.done) { anyDone = true; continue; }
            auto p = CNetworkPlayerManager::GetPlayer(entry.first);
            if (p) { if (!missing.empty()) missing += ", "; missing += p->GetName(); }
        }
        if (anyDone) {
            if (holding) { if (!missing.empty()) CMissionHints::AuthorityWait(Packets::Scripts::MissionWaitNotice::RACE_FINISH, missing); }
            else CMissionHints::AuthorityAwaited(Packets::Scripts::MissionWaitNotice::RACE_FINISH);
        }
    }
    if (holding) {
        if (all) { Local(STAGE) = 13; Local(450) = TeamPlace(); }
        if (all) { released = true; holding = false; CMissionMirror::LogEventf("high-stakes-all-finished"); }
    }
}

void CHighStakesRace::OnPacket(const P& p) {
    if (!Enabled() || p.target != CNetworkPlayerManager::m_localPlayerId || CMissionMode::IsIndividual() ||
        CCoopMission::HasLocalLeft() || COpCodeSync::IsLocalMissionRunning()) return;
    for (int i = 0; i < 3; ++i) if (!std::isfinite(p.next[i]) || !std::isfinite(p.after[i]) || !std::isfinite(p.grid[i])) return;
    if (!std::isfinite(p.size) || !std::isfinite(p.heading) || p.size < 1 || p.size > 15 || p.phase > P::END) return;
    auto owner = CNetworkPlayerManager::GetPlayer(p.owner);
    if (!owner || !owner->m_bIsHost || p.session == 0 || p.session == retired) return;
    if (p.phase == P::PREPARE && p.type == 8) {
        // Not a racer yet (no lowrider under him): a hint only, no session.
        if (!InLowrider(FindPlayerPed(0)))
            CMissionHints::Help("High Stakes: sube a un lowrider (de conductor o de acompanante) para ir con los demas");
        return;
    }
    if (p.phase == P::END && p.type == 1 && guestSession == 0) {
        // Won race seen by somebody who never raced it (no lowrider): the contact icon still goes.
        if (CConfigLoader::ms_bHighStakesEndCleanup) RemoveCesarContactBlip();
        return;
    }
    if (guestSession != p.session) {
        if (guestSession && int32_t(p.session-guestSession) <= 0) return;
        if (p.phase == P::END) return;
        GuestEnd(); guestSession = p.session; guestOwner = p.owner; guestSequence = 0;
        CMirrorRaceCheckpoints::Clear();
    }
    if (int32_t(p.sequence-guestSequence) <= 0) return;
    guestSequence = p.sequence; heard = GetTickCount();
    hudPlace = p.place; hudTotal = p.total; hudLap = p.lap; hudLaps = p.laps;
    if (p.rows) {
        boardCount = (std::min)(int(p.rows), int(P::BOARD_MAX));
        for (int i = 0; i < boardCount; ++i) { board[i] = p.board[i]; board[i].name[sizeof board[i].name - 1] = 0; }
        boardAt = GetTickCount();
    }
    if (p.phase == P::END) {
        if (p.type == 1 && CConfigLoader::ms_bHighStakesEndCleanup) RemoveCesarContactBlip();
        GuestEnd(); CMirrorRaceCheckpoints::Clear(); return;
    }
    auto me = FindPlayerPed(0); auto car = DriverCar(me);
    if (!car) return;
    if (p.phase == P::PREPARE) return;
    if (!gridPlaced) {
        // One placement on the common grid; subsequent snapshots never warp a racer back.
        const int ref = CPools::GetVehicleRef(car);
        Command<Commands::SET_CAR_COORDINATES>(ref, p.grid[0], p.grid[1], p.grid[2]);
        Command<Commands::SET_CAR_HEADING>(ref, p.heading);
        car->m_vecMoveSpeed = {}; car->m_vecTurnSpeed = {};
        Command<Commands::RESTORE_CAMERA_JUMPCUT>(); Command<Commands::SET_CAMERA_BEHIND_PLAYER>();
        Command<Commands::SWITCH_WIDESCREEN>(false);
        gridPlaced = true;
    }
    if (p.phase == P::COUNTDOWN) {
        frozenCar = CPools::GetVehicleRef(car);
        Command<Commands::FREEZE_CAR_POSITION>(frozenCar, true);
        Command<Commands::SET_PLAYER_CONTROL>(0, false); lockedControls = true;
    } else {
        Unlock();
        // SET_PLAYER_CONTROL off may have arrived before our first snapshot; GO owns this transition too.
        Command<Commands::SET_PLAYER_CONTROL>(0, true);
    }
    if (p.phase == P::FINISHED) { if (clockStart && !clockStop) clockStop = GetTickCount(); Unmark(); return; }
    if (p.phase != P::RACING) return;
    if (!clockStart) clockStart = GetTickCount();
    const CVector next{p.next[0],p.next[1],p.next[2]};
    if (markedType != p.type || (next-marked).Magnitude() > 0.1f) {
        Unmark(); marked = next; markedType = p.type;
        Command<Commands::CREATE_CHECKPOINT>(int(p.type), p.next[0],p.next[1],p.next[2],
            p.after[0],p.after[1],p.after[2],p.size,&checkpoint);
        Command<Commands::ADD_BLIP_FOR_COORD>(p.next[0],p.next[1],p.next[2],&blip);
    }
}

static void DrawBoard() {
    // The big standings box is the only race HUD (2026-11-15: the small position/time panel goes). The title and the clock use
    // Pricedown, the game's own big-text font (easier to read than the gothic one); the rows stay in the subtitles font.
    const float sx = RsGlobal.maximumWidth / 640.0f, sy = RsGlobal.maximumHeight / 448.0f;
    const float w = 190.0f * sx, lineH = 15.0f * sy, titleH = 24.0f * sy, pad = 8.0f * sx;
    const float h = titleH + boardCount * lineH + 6.0f * sy;
    const float x2 = RsGlobal.maximumWidth - 20.0f * sx, x1 = x2 - w;
    const float y2 = RsGlobal.maximumHeight - 40.0f * sy, y1 = y2 - h;
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
    if (clockStart) {
        const uint32_t ms = (clockStop ? clockStop : GetTickCount()) - clockStart;
        char clock[16];
        sprintf_s(clock, "%u:%02u", ms / 60000, ms / 1000 % 60);
        CFont::SetOrientation(ALIGN_RIGHT);
        CFont::SetColor(clockStop ? CRGBA(172, 203, 241, 255) : CRGBA(255, 255, 255, 255));
        CFont::PrintString(x2 - pad, y1 + 2.0f * sy, clock);
        CFont::SetOrientation(ALIGN_LEFT);
    }
    CFont::SetFontStyle(FONT_SUBTITLES);
    CFont::SetScale(0.5f * sx, 0.95f * sy);
    for (int i = 0; i < boardCount; ++i) {
        const Row& r = board[i];
        char left[40], right[16];
        sprintf_s(left, "%d. %s", i + 1, CMissionHints::ToGameText(r.name).c_str());
        if (r.done) sprintf_s(right, "META"); else sprintf_s(right, "V%d", r.lap);
        const bool mine = r.kind == 1 && strcmp(r.name, CLocalPlayer::m_Name) == 0;
        CFont::SetColor(mine ? CRGBA(172, 203, 241, 255) : r.kind ? CRGBA(225, 225, 225, 255) : CRGBA(160, 160, 160, 255));
        const float y = y1 + titleH + i * lineH;
        CFont::PrintString(x1 + pad, y, left);
        CFont::PrintString(x2 - 38.0f * sx, y, right);
    }
    CFont::RenderFontBuffer();
}

// The original CPRACE draws its own position/time panel (script rects + text) on top of our board: drop those draws
// for the frame (they are only the queue the scripts filled this frame). From the countdown to the result, whoever runs the
// race script with the board on screen (no longer tied to the stage the board starts at).
void CHighStakesRace::HideNativeHud() {
    if (!CConfigLoader::ms_bHighStakesHideNativeHud || !CConfigLoader::ms_bHighStakesBoard || !owning || !race ||
        !ActiveScript(race) || Local(STAGE) < 7 || Local(STAGE) >= 13) return;
    // emptying the entries (lowering the counters alone hid nothing: the game draws every non-empty entry, 2026-11-15)
    const int dropped = CVsRace::DropScriptRacePanel();
    if (!nativeHudLogged && dropped) {
        nativeHudLogged = true;
        CMissionMirror::LogEventf("high-stakes-native-hud-hidden entries=%d stage=%d shared=%d", dropped, Local(STAGE), shared ? 1 : 0);
    }
}

void CHighStakesRace::Draw() {
    // Only the big board: the small "High Stakes: N/M Vuelta" line is gone (2026-11-15).
    if (CConfigLoader::ms_bHighStakesBoard && boardCount > 0 && GetTickCount() - boardAt < 2500 &&
        ((owning && shared && Local(STAGE) >= 11 && Local(STAGE) < 13) || (OwnMarkers() && gridPlaced)))
        DrawBoard();
}
