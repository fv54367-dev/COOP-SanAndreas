// Adapted from Karimov-Akbar/sa-coop, MIT (2026), Minigames.cpp and shared/game/Script.*.
// Engine semantics: gta-reversed AEAudioHardware::GetBeatInfo, AEStreamThread::GetTrackPlayTime.
#include "stdafx.h"
#include "CCoopLowrider.h"
#include "CCameraMirror.h"
#include "CCoopMission.h"
#include "CConfigLoader.h"
#include "CMissionMode.h"
#include "CMissionMirror.h"
#include "COpCodeSync.h"
#include "CTestMode.h"
#include "network/packets/scripts.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace {
using PacketT = Packets::Scripts::CoopLowrider;
uint64_t NowMs() { return GetTickCount64(); }
CVehicle* GetVehicle(int h) { return h >= 0 ? CPools::GetVehicle(h) : nullptr; }
CPed* GetPed(int h) { return h >= 0 ? CPools::GetPed(h) : nullptr; }
int GetVehicleRef(CVehicle* v) { return CPools::GetVehicleRef(v); }
int GetPedRef(CPed* p) { return CPools::GetPedRef(p); }
bool IsModelLoaded(int model) { return CStreaming::ms_aInfoForModel[model].m_nLoadState == LOADSTATE_LOADED; }
void LoadModelNow(int model) { CStreaming::RequestModel(model, GAME_REQUIRED); CStreaming::LoadAllRequestedModels(false); }
bool SceneWatched() { return CCutsceneMgr::ms_running; }
int32_t& Var(uint32_t off) { return *reinterpret_cast<int32_t*>(&CTheScripts::ScriptSpace[off]); }
bool Authority(playerid_t id) {
    if (CCoopMission::IsRunning()) return id == CCoopMission::GetOwnerId();
    auto p = CNetworkPlayerManager::GetPlayer(id);
    return p && p->m_bIsHost;
}
bool GuestEligible() {
    return CCoopLowrider::Enabled() && CNetwork::m_bConnected && !CCoopMission::IsScriptAuthority() &&
        !CMissionMode::IsIndividual() && !CCoopMission::HasLocalLeft() &&
        (!CCoopMission::IsActive() || CCoopMission::IsLocalParticipant()) && !COpCodeSync::IsLocalMissionRunning();
}
uint32_t g_session = 0;
playerid_t g_owner{};
struct Taking { int lastBeat = -1; bool done = false; };
std::map<uint64_t, Taking> g_took;
bool g_host = false, g_stopping = false, g_play = false;
uint64_t g_hold = 0, g_lastTime = 0, g_lastPacket = 0;
void Send(uint8_t action, int value = 0, int beat = 0) {
    PacketT p{}; p.owner = g_owner; p.session = g_session; p.action = action;
    p.value = value; p.beat = beat; GetPacketFactory().Send(p);
}
constexpr uint8_t KIND_LOWRIDER = 1, SCORE_JOIN = PacketT::JOIN, SCORE_POINTS = PacketT::POINTS, SCORE_DONE = PacketT::DONE;
constexpr int kLoanerModel = 567, kHydraulics = 1087, kGirlModel = 64;
constexpr uint32_t kBdisply = 0x1CDEA;
constexpr uintptr_t kStreamThread = 0xB606CC;
constexpr uint32_t kLowRunning = 4068, kLowScore = 4072, kLowHit = 4172, kBeatStop = 4176, kBeatShown = 4192;
class Cmd {
public:
    explicit Cmd(uint16_t opcode);

    Cmd& I(int32_t v);              // integer literal
    Cmd& F(float v);                // float literal
    Cmd& S(const char* str);        // variable-length string literal (type 0x0E)
    Cmd& S8(const char* str);       // fixed 8-byte string literal (type 0x09), e.g. GXT keys
    Cmd& Out(int32_t* result);      // output into a local variable, copied back after Run()
    Cmd& Out(float* result);
    Cmd& End();                     // end of variadic arguments (START_NEW_SCRIPT etc.)

    // Executes the command. Returns the condition result for conditional opcodes.
    bool Run();

private:
    void Put(const void* p, size_t n);
    uint8_t m_code[256];
    size_t m_size = 0;
    struct OutVar {
        void* ptr;
        uint16_t index;
    };
    OutVar m_outs[16];
    int m_numOuts = 0;
    uint16_t m_nextLocal = 0;
    bool m_overflow = false;
};

Cmd::Cmd(uint16_t opcode) { Put(&opcode, 2); }

void Cmd::Put(const void* p, size_t n) {
    if (m_size + n > sizeof(m_code)) {
        m_overflow = true;
        return;
    }
    memcpy(m_code + m_size, p, n);
    m_size += n;
}

Cmd& Cmd::I(int32_t v) {
    uint8_t t = 1;  // SCRIPTPARAM_STATIC_INT_32BITS
    Put(&t, 1);
    Put(&v, 4);
    return *this;
}

Cmd& Cmd::F(float v) {
    uint8_t t = 6;  // SCRIPTPARAM_STATIC_FLOAT
    Put(&t, 1);
    Put(&v, 4);
    return *this;
}

Cmd& Cmd::S(const char* str) {
    uint8_t t = 0x0E;  // SCRIPTPARAM_STATIC_PASCAL_STRING
    // CRunningScript::ReadTextLabelFromScript copies it unbounded into the handler's stack buffer (24 bytes for an
    // animation name, 16 for its block) and takes a length of 128 or more as negative
    size_t n = strlen(str);
    if (n > 23) {
        
        n = 23;
    }
    uint8_t len = (uint8_t)n;
    Put(&t, 1);
    Put(&len, 1);
    Put(str, n);
    return *this;
}

Cmd& Cmd::S8(const char* str) {
    uint8_t t = 0x09;  // SCRIPTPARAM_STATIC_SHORT_STRING
    char buf[8] = {};
    strncpy(buf, str, 7);
    Put(&t, 1);
    Put(buf, 8);
    return *this;
}

Cmd& Cmd::Out(int32_t* result) {
    if (m_numOuts >= 16 || m_nextLocal >= 32) {
        m_overflow = true;
        return *this;
    }
    uint8_t t = 3;  // SCRIPTPARAM_LOCAL_NUMBER_VARIABLE
    Put(&t, 1);
    Put(&m_nextLocal, 2);
    m_outs[m_numOuts++] = {result, m_nextLocal++};
    return *this;
}

Cmd& Cmd::Out(float* result) { return Out(reinterpret_cast<int32_t*>(result)); }

Cmd& Cmd::End() {
    uint8_t t = 0;
    Put(&t, 1);
    return *this;
}

bool Cmd::Run() {
    if (m_overflow) return false;
    int savedParams[32];
    memcpy(savedParams, ScriptParams, sizeof(savedParams));
    CRunningScript script{};
    script.Init();
    script.m_pBaseIP = script.m_pCurrentIP = m_code;
    memcpy(script.m_szName, "t53cmd", 7);
    script.ProcessOneCommand();
    for (int i=0; i<m_numOuts; ++i)
        memcpy(m_outs[i].ptr, &script.m_aLocalVars[m_outs[i].index], 4);
    memcpy(ScriptParams, savedParams, sizeof(savedParams));
    return script.m_bCondResult;
}
const CVector kMeetCar{1793.496f, -1907.423f, 12.399f};
const CVector kOpenCam{1788.516f, -1910.318f, 14.890f}, kOpenLook{1789.355f, -1909.843f, 14.627f};
const CVector kMeetCams[5] = {{1803.978f, -1909.369f, 15.067f}, {1795.369f, -1900.999f, 14.179f},
                              {1788.793f, -1902.990f, 13.720f}, {1789.377f, -1912.195f, 14.067f},
                              {1796.409f, -1912.231f, 13.832f}};

struct Partner {
    uint8_t kind = 0;
    int stage = 0;  // 1 getting ready, 2 playing
    int16_t track = 0;
    CVector at;
    float heading = 0.0f;
    int car = -1, doorLock = 1;
    bool loaner = false, play = false;
    int32_t lastBeat = -1;
    int combo = 0, perfect = 0, good = 0, bad = 0, judged = 0;  // LOWGAME's $4120, $4108, $4112, $4116
    uint64_t since = 0;
    // the lowrider contest's own: the girl on our door and her mood (LOWGAME's $4080: -1 idle, 0..5), its counts of good
    // and bad beats (30@, 31@), our last move (19@), a missed beat (23@); the move she plays before her level's loop
    int girl = -1;
    bool girlOnCar = false, ifp = false;
    int level = -1, shown = -1, cheers = 0, boos = 0, lastMove = 0;
    bool slip = false;
    const char* once = nullptr;
    const char* loop = nullptr;
    uint64_t onceMs = 0;
    // the meet's cameras on our car: -2 none, -1 the opening shot, 0..4 the round
    CVector hostCar, camShift;
    int cam = -2;
    uint64_t camMs = 0, placedMs = 0;
    bool camButton = false;
} g_part;

struct Preparation {
    PacketT state{};
    int car = -1, girl = -1, blip = -1;
    bool delivered = false, garageManaged = false, garageAllowed = true;
    uint64_t heard = 0, sent = 0, betSeen = 0;
} g_prep;
int GuestSlot(playerid_t owner) {
    std::vector<uint64_t> ids{CNetworkPlayerManager::m_localPlayerId.value()};
    for (auto p : CNetworkPlayerManager::m_pPlayers)
        if (p->m_playerId != owner && CCoopMission::ShouldIncludeNetworkPlayer(p)) ids.push_back(p->m_playerId.value());
    std::sort(ids.begin(), ids.end());
    return int(std::find(ids.begin(), ids.end(), CNetworkPlayerManager::m_localPlayerId.value()) - ids.begin());
}
bool FreeCarSpot(const PacketT& p, CVector& at, CVehicle* ignore = nullptr) {
    const int slot = GuestSlot(p.owner);
    for (int attempt = 0; attempt < 24; ++attempt) {
        const float side = (slot % 2 ? -1.f : 1.f) * (4.8f + (slot / 2) * 5.0f);
        const float back = float(attempt / 3) * 5.f;
        at = p.pos + CVector(cosf(p.heading) * side - sinf(p.heading) * back,
                             sinf(p.heading) * side + cosf(p.heading) * back, 0);
        bool floor = false;
        float z = plugin::CallAndReturn<float, 0x5696C0>(at.x, at.y, p.pos.z + 2.f, &floor, (CEntity**)nullptr);
        if (!floor || !std::isfinite(z) || fabsf(z - p.pos.z) > 2.f) continue;
        at.z = z + 0.8f;
        // Two spheres cover the chassis without intersecting the road itself.
        bool blocked = false;
        for (float along : {-1.3f, 1.3f}) {
            CVector body = at + CVector(-sinf(p.heading) * along, cosf(p.heading) * along, 0.5f);
            if (CWorld::TestSphereAgainstWorld(body, 1.1f, ignore, true, true, true, true, true, false)) blocked = true;
        }
        if (!blocked) return true;
    }
    return false;
}
// Guest's lowrider: keep it like the host keeps his (no longer needed), else delete it
void ReleaseCar(int car) {
    auto veh = GetVehicle(car);
    if (CConfigLoader::ms_bLowriderKeepCar && veh && veh->m_fHealth > 0) Cmd(0x01C3).I(car).Run();  // MARK_CAR_AS_NO_LONGER_NEEDED
    else Cmd(0x00A6).I(car).Run();
}
void ClearPreparation(bool restarted) {
    if (!restarted) {
        if (g_prep.blip >= 0) Cmd(0x0164).I(g_prep.blip).Run();
        if (GetPed(g_prep.girl)) Cmd(0x009B).I(g_prep.girl).Run();
        if (auto car = GetVehicle(g_prep.car)) {
            auto me = FindPlayerPed(0);
            if (me && me->m_nPedFlags.bInVehicle && me->m_pVehicle == car && !CConfigLoader::ms_bLowriderKeepCar) {
                auto at = me->GetPosition();
                Cmd(0x0362).I(GetPedRef(me)).F(at.x + 3.f).F(at.y).F(at.z + 1.f).Run();
            }
            ReleaseCar(g_prep.car);
        }
        if (g_prep.garageManaged && !g_prep.garageAllowed) Cmd(0x0299).S8("MODLAST").Run();
    }
    g_prep = {};
}
void DrawWager() {
    if (!(g_prep.state.beat & 2) || NowMs() - g_prep.heard > 1000) return;
    Cmd(0x03F0).I(1).Run();
    Cmd(0x0937).F(29).F(220).F(173).F(409).S8("BJ_TITL").I(2).Run();
    const char* labels[] = {"WOF_04", "BJ_02", "BJ_03"};
    for (int i = 0; i < 3; ++i) {
        Cmd(0x03E4).I(0).Run(); Cmd(0x0341).I(0).Run(); Cmd(0x0342).I(0).Run();
        Cmd(0x0343).F(640).Run(); Cmd(0x0348).I(1).Run(); Cmd(0x0345).I(0).Run();
        Cmd(0x033F).F(0.4714f).F(2.5077f).Run();
        Cmd(0x0340).I(134).I(155).I(184).I(255).Run();
        Cmd(0x033E).F(36).F(240.f + i * 50.f).S8(labels[i]).Run();
        Cmd(0x0340).I(180).I(180).I(180).I(255).Run();
        Cmd(0x033F).F(0.6253f).F(2.7876f).Run();
        Cmd(0x045A).F(36).F(260.f + i * 50.f).S8("DOLLAR").I(g_prep.state.wager[i]).Run();
    }
}
void DeliverCar() {
    if (g_prep.delivered || !(g_prep.state.beat & 1)) return;
    auto me = FindPlayerPed(0);
    if (!me || me->m_nAreaCode != 0 || (me->GetPosition() - g_prep.state.pos).Magnitude() > 100.f) return;
    CVector at{};
    if (!FreeCarSpot(g_prep.state, at)) return;
    LoadModelNow(kLoanerModel); LoadModelNow(kHydraulics); LoadModelNow(kGirlModel);
    if (!IsModelLoaded(kLoanerModel) || !IsModelLoaded(kHydraulics) || !IsModelLoaded(kGirlModel)) return;
    int car = -1;
    Cmd(0x00A5).I(kLoanerModel).F(at.x).F(at.y).F(at.z).Out(&car).Run();
    if (!GetVehicle(car)) return;
    g_prep.car = car; g_prep.delivered = true;
    GetVehicle(car)->m_nPrimaryColor = g_prep.state.colors[0];
    GetVehicle(car)->m_nSecondaryColor = g_prep.state.colors[1];
    Cmd(0x0175).I(car).F(g_prep.state.heading * 57.29578f).Run();
    int mod = -1; Cmd(0x06E7).I(car).I(kHydraulics).Out(&mod).Run();
    Cmd(0x0186).I(car).Out(&g_prep.blip).Run();
    Cmd(0x009A).I(24).I(kGirlModel).F(at.x).F(at.y).F(at.z + 2.f).Out(&g_prep.girl).Run();
    if (GetPed(g_prep.girl)) Cmd(0x0430).I(g_prep.girl).I(car).I(0).Run();
    for (int model : {kLoanerModel, kHydraulics, kGirlModel}) CStreaming::SetModelIsDeletable(model);
    CHud::SetHelpMessage("Subi a tu lowrider (icono azul) para competir con Cesar.", true, false, false);
    CMissionMirror::LogEventf("lowrider-delivery car=%d girl=%d slot=%d pos=%.2f,%.2f,%.2f", car, g_prep.girl,
        GuestSlot(g_prep.state.owner), at.x, at.y, at.z);
}

// The host's playback position of the contest's track, our clock's offset from it. Our own track starts as late as the
// host's word of its start comes, as do these: our arrows and judging run on them and stay with our music, and a stall
// of either game's stream no more moves the two apart nor holds the arrows still.
struct BeatClock {
    bool have = false;
    int64_t offset = 0;
    uint64_t recvMs = 0;
} g_clock;
void HearBeatTime(int32_t t) {
    int64_t sample = (int64_t)t - (int64_t)NowMs();
    if (!g_clock.have || std::abs(sample - g_clock.offset) > 250) g_clock.offset = sample;
    else g_clock.offset += (sample - g_clock.offset) / 8;  // the network's jitter smoothed out
    g_clock.have = true, g_clock.recvMs = NowMs();
}
// CAEAudioHardware::GetBeatInfo's read of the track's position: what GET_BEAT_PROXIMITY tells BDISPLY and our judge
using PlayTimeFn = int32_t(__fastcall*)(void*, void*);
PlayTimeFn g_origPlayTime = nullptr;
int32_t __fastcall HookBeatPlayTime(void* thread, void* edx) {
    int32_t own = g_origPlayTime(thread, edx);
    if (!CCoopLowrider::Enabled() || g_part.kind != KIND_LOWRIDER || g_part.stage != 2 || !g_clock.have || NowMs() - g_clock.recvMs > 2000)
        return own;
    return (int32_t)std::max<int64_t>(0, (int64_t)NowMs() + g_clock.offset);
}


void SendScore(uint8_t op, int beat, int points) { Send(op, points, std::max(0, beat)); }
// The lowrider's input: the right stick, past 64, in eight directions as LOWGAME reads it (0x1C934): 13 up, 11 up-right,
// 15 up-left, 10 right, 9 left, 16 down-right, 12 down-left, 14 down
int Direction(int x, int y) {
    if (x * x + y * y <= 64 * 64) return 0;
    float a = std::atan2((float)x, (float)-y) * 57.29578f;  // 0 up, positive to the right
    float m = std::fabs(a);
    if (m < 15.0f) return 13;
    if (m < 75.0f) return a > 0 ? 11 : 15;
    if (m < 105.0f) return a > 0 ? 10 : 9;
    if (m < 165.0f) return a > 0 ? 16 : 12;
    return 14;
}

// LOWGAME's grade of a beat (0x1A5AD): t ms from it, key the beat's, dir the stick's; -1 not yet. 0 perfect, 1 good,
// 2 bad, 3 too late or missed, 4 too early, 5 the wrong way
int Grade(int t, int key, int dir) {
    if (!dir) return t < -200 ? 3 : -1;
    if (dir != key) return 5;
    int a = std::abs(t);
    return a < 35 ? 0 : a < 50 ? 1 : a < 100 ? 2 : t < 0 ? 3 : 4;
}

// ---- the girl on our door: Cesar's mission seats one beside CJ, on the door of a Savanna (0x9A517), as a passenger
// in any other car; LOWGAME moves her (0x1ABAB..0x1B947)
void GirlAnim(const char* name, bool loop) {  // TASK_PLAY_ANIM_NON_INTERRUPTABLE, a move held on its last frame
    Cmd(0x0812).I(g_part.girl).S(name).S("LOWRIDER").F(4.0f).I(loop).I(0).I(0).I(loop ? 0 : 1).I(-1).Run();
}
void GirlPlay(const char* once, const char* loop) {  // a move (if any), then the loop
    GirlAnim(once ? once : loop, !once);
    g_part.once = once, g_part.loop = loop, g_part.onceMs = NowMs();
}
// of her move, 1 once it is over (the task starts its animation a frame or two after it is given)
float GirlProgress() {
    if (!g_part.once) return 1.0f;
    float t = NowMs() - g_part.onceMs < 250 ? 0.0f : 1.0f;
    if (Cmd(0x0611).I(g_part.girl).S(g_part.once).Run()) Cmd(0x0613).I(g_part.girl).S(g_part.once).Out(&t).Run();  // IS_CHAR_PLAYING_ANIM
    return t;
}
void SpawnGirl() {
    if (!IsModelLoaded(kGirlModel)) LoadModelNow(kGirlModel);  // behind the host's black screen
    int girl = g_prep.girl;
    g_prep.girl = -1;
    if (!GetPed(girl)) Cmd(0x009A).I(24).I(kGirlModel).F(g_part.at.x).F(g_part.at.y).F(g_part.at.z + 2.0f).Out(&girl).Run();  // CREATE_CHAR
    if (girl < 0) return;
    g_part.girl = girl;
    Cmd(0x0430).I(girl).I(g_part.car).I(0).Run();                  // WARP_CHAR_INTO_CAR_AS_PASSENGER
    if (!Cmd(0x0137).I(g_part.car).I(kLoanerModel).Run()) return;  // IS_CAR_MODEL
    Cmd(0x0792).I(girl).Run();                                     // CLEAR_CHAR_TASKS_IMMEDIATELY
    Cmd(0x0464).I(girl).I(g_part.car).F(0.62f).F(0.31f).F(-0.167f).I(0).F(0.0f).I(0).Run();  // ATTACH_CHAR_TO_CAR
    g_part.girlOnCar = true;
    if (!Cmd(0x04EE).S("LOWRIDER").Run()) Cmd(0x04ED).S("LOWRIDER").Run(), g_part.ifp = true;  // REQUEST_ANIMATION
}
// her mood rises a level for six good beats and falls one for two bad ones (from the second up), she bounces at each new
// move of ours and slips back to the first after a missed beat
void MoveGirl(int move) {
    int& level = g_part.level;
    if (!g_part.girlOnCar || !GetPed(g_part.girl) || !Cmd(0x04EE).S("LOWRIDER").Run()) return;  // HAS_ANIMATION_LOADED
    static const char* const kLoop[] = {"LRGIRL_L0_LOOP", "LRGIRL_L1_LOOP", "LRGIRL_L2_LOOP",
                                        "LRGIRL_L3_LOOP", "LRGIRL_L4_LOOP", "LRGIRL_L5_LOOP"};
    static const char* const kUp[] = {"LRGIRL_L0_TO_L1", "LRGIRL_L1_TO_L2", "LRGIRL_L2_TO_L3", "LRGIRL_L3_TO_L4", "LRGIRL_L4_TO_L5"};
    static const char* const kBounce[] = {"LRGIRL_L0_BNCE", "LRGIRL_L1_BNCE", "LRGIRL_L2_BNCE",
                                          "LRGIRL_L3_BNCE", "LRGIRL_L4_BNCE", "LRGIRL_L5_BNCE"};
    if (g_part.shown == -1 && !g_part.loop) GirlPlay(nullptr, "LRGIRL_IDLELOOP");
    if (level == -1 && move) level = 0;
    if (level == 0 && g_part.cheers > 5) level = 1, g_part.cheers = 0;
    if (level == 1 && g_part.cheers > 5) level = 2, g_part.cheers = g_part.boos = 0;
    for (int n = 2; n <= 4; ++n)
        if (level == n) {
            if (g_part.cheers > 5) level = n + 1, g_part.cheers = g_part.boos = 0;
            if (g_part.boos > 1) level = n - 1, g_part.boos = 0;
        }
    if (level == 5 && g_part.boos > 1) level = 4, g_part.cheers = g_part.boos = 0;
    if (level != g_part.shown) {
        int from = g_part.shown;
        if (from == -1 && level == 0) GirlPlay("LRGIRL_IDLE_TO_L0", kLoop[0]);
        else if (from >= 0 && level == from + 1) GirlPlay(kUp[from], kLoop[level]);
        else if (from > 1 && level == 1) GirlPlay("LRGIRL_L345_TO_L1", kLoop[1]);
        else if (from == 1 && level == 0) GirlPlay("LRGIRL_L12_TO_L0", kLoop[0]);
        g_part.shown = level;
    }
    // a new move: a bounce, unless she is still in the most of a change of level
    if (move && move != g_part.lastMove) {
        g_part.lastMove = move;
        if (level >= 0 && (!g_part.once || strstr(g_part.once, "_BNCE") || GirlProgress() >= 0.9f))
            GirlPlay(kBounce[level], g_part.loop ? g_part.loop : kLoop[level]);
    }
    if (g_part.slip) {
        g_part.slip = false;
        if (!move && level > 0) {
            if (!Cmd(0x0611).I(g_part.girl).S("LRGIRL_BDBNCE").Run()) GirlPlay("LRGIRL_BDBNCE", kLoop[1]);
            level = g_part.shown = 1;
        }
    }
    if (g_part.once && GirlProgress() >= 0.99f) {
        g_part.once = nullptr;
        GirlAnim(g_part.loop, true);
    }
}

// ---- SWEET6's cameras on our car (at that meet only): the opening shot, then round the five every 5 s or on the camera
// button, as the host's go round its own
void PlaceCamera() {
    CVector src = (g_part.cam < 0 ? kOpenCam : kMeetCams[g_part.cam]) + g_part.camShift, look = kOpenLook + g_part.camShift;
    if (CVehicle* v = g_part.cam >= 0 ? GetVehicle(g_part.car) : nullptr) look = v->GetPosition();
    Cmd(0x015F).F(src.x).F(src.y).F(src.z).F(0.0f).F(0.0f).F(0.0f).Run();  // SET_FIXED_CAMERA_POSITION
    Cmd(0x0160).F(look.x).F(look.y).F(look.z).I(2).Run();                 // POINT_CAMERA_AT_POINT, a jump cut
    g_part.placedMs = NowMs();
}
void StartCamera() {
    if ((g_part.hostCar - kMeetCar).Magnitude() > 30.0f) return;  // another place: our own camera
    g_part.camShift = g_part.at - g_part.hostCar;
    g_part.cam = -1, g_part.camMs = NowMs();
    Cmd(0x0826).I(0).Run();  // DISPLAY_HUD: off, as the host's
    Cmd(0x0581).I(0).Run();  // DISPLAY_RADAR
    PlaceCamera();
}
void TurnCamera() {
    if (g_part.cam == -2 || SceneWatched()) return;
    bool button = Cmd(0x00E1).I(0).I(13).Run();  // IS_BUTTON_PRESSED: the camera button
    if ((button && !g_part.camButton) || NowMs() - g_part.camMs > 5000) {
        g_part.cam = (g_part.cam + 1) % 5, g_part.camMs = NowMs();
        PlaceCamera();
    } else if (*reinterpret_cast<uint8_t*>(0xB6F028 + 0x2B) && NowMs() - g_part.placedMs > 500) {  // m_bLookingAtPlayer: ours was taken back
        PlaceCamera();
    }
    g_part.camButton = button;
}

void FinishLowrider() {
    Var(kBeatStop) = 1;  // BDISPLY takes its arrows away and ends
    int status = 0;
    Cmd(0x0953).Out(&status).Run();       // GET_BEAT_TRACK_STATUS
    if (status == 3) Cmd(0x0955).Run();  // STOP_BEAT_TRACK
    Cmd(0x0221).I(0).I(0).Run();         // APPLY_BRAKES_TO_PLAYERS_CAR off
    Cmd(0x09EE).I(0).Run();              // FORCE_BIG_MESSAGE_AND_COUNTER off
    if (g_part.cam != -2) {
        if (!SceneWatched()) {  // the host's scene after the contest has the camera already
            Cmd(0x02EB).Run();  // RESTORE_CAMERA_JUMPCUT
            Cmd(0x0373).Run();  // SET_CAMERA_BEHIND_PLAYER
        }
        Cmd(0x0826).I(1).Run();  // DISPLAY_HUD
        Cmd(0x0581).I(1).Run();  // DISPLAY_RADAR
    }
    if (g_part.girl >= 0 && GetPed(g_part.girl)) Cmd(0x009B).I(g_part.girl).Run();  // DELETE_CHAR
    if (g_part.ifp) Cmd(0x04EF).S("LOWRIDER").Run();                             // REMOVE_ANIMATION
    if (GetVehicle(g_part.car)) {
        Cmd(0x020A).I(g_part.car).I(g_part.doorLock).Run();
        if (g_part.loaner) {
            auto me = FindPlayerPed(0);
            if (me && me->m_nPedFlags.bInVehicle && me->m_pVehicle == GetVehicle(g_part.car) &&
                !CConfigLoader::ms_bLowriderKeepCar) {
                auto pos = me->GetPosition();
                Cmd(0x0362).I(GetPedRef(me)).F(pos.x + 3).F(pos.y).F(pos.z + 1).Run();
            }
            ReleaseCar(g_part.car);
        }
    }
    SendScore(SCORE_DONE, 0, 0);
    
    g_part = {};
    g_clock = {};
    g_lastPacket = 0;
}

// LOWGAME's word on every four beats, given ahead of the next one's points (0x1C5BF): four perfect ones are a combo
// (DNC_002 once one runs: ten times the points), more good than bad keep it going (DNC_004), else it ends (DNC_003)
void Combo() {
    if (g_part.judged < 4) return;
    const char* text;
    if (g_part.perfect == 4) {
        text = g_part.combo >= 1 ? "DNC_002" : "DNC_004";
        g_part.combo = g_part.combo >= 1 ? 2 : 1;
    } else if (g_part.good > g_part.bad || g_part.combo == 2) {
        text = "DNC_004";
        g_part.combo = 1;
    } else {
        text = "DNC_003";
        g_part.combo = 0;
    }
    Cmd(0x00BA).S8(text).I(50000).I(7).Run();  // PRINT_BIG: up till the next word
    g_part.judged = g_part.perfect = g_part.good = g_part.bad = 0;
}

void JudgeBeat(int dir) {
    int t = 0, key = 0, n = 0;
    Cmd(0x07B1).I(0).Out(&t).Out(&key).Out(&n).Run();  // GET_BEAT_PROXIMITY: the nearest beat
    if (n == g_part.lastBeat) return;
    int grade = Grade(t, key, dir);
    if (grade < 0) return;
    g_part.lastBeat = n;
    Combo();
    int points = (grade == 0 ? 50 : grade == 1 ? 20 : grade == 2 ? 10 : 0) * (g_part.combo == 2 ? 10 : 1);
    ++g_part.judged;
    if (grade == 0) ++g_part.perfect;
    else if (grade <= 2) ++g_part.good;  // LOWGAME counts a bad hit with the good ones ($4112)
    else ++g_part.bad;
    if (grade <= 1) ++g_part.cheers;
    else ++g_part.boos;
    g_part.slip = grade >= 3;
    Var(kLowHit) = grade == 0 ? 3 : grade == 1 ? 2 : 1;  // BDISPLY's flash of the ring
    Cmd(0x015B).I(0).I(200).I(grade == 0 ? 255 : grade == 1 ? 200 : grade == 2 ? 110 : 50).Run();  // SHAKE_PAD
    SendScore(SCORE_POINTS, n, points);
}

void PlayLowrider() {
    int t = 0, key = 0, n = 0;
    Cmd(0x07B1).I(-1).Out(&t).Out(&key).Out(&n).Run();  // GET_BEAT_PROXIMITY: the last beat
    if (key == 33) return FinishLowrider();             // the song's end
    int lx = 0, ly = 0, rx = 0, ry = 0;
    Cmd(0x0494).I(0).Out(&lx).Out(&ly).Out(&rx).Out(&ry).Run();  // GET_POSITION_OF_ANALOGUE_STICKS
    int dir = CTestMode::IsGameWindowFocused() ? Direction(rx, ry) : 0;
    JudgeBeat(dir);
    MoveGirl(dir);
    TurnCamera();
}


bool StartLowrider(const PacketT& packet) {
    if (!std::isfinite(packet.pos.x) || !std::isfinite(packet.pos.y) || !std::isfinite(packet.pos.z)) return false;
    if (!g_origPlayTime) { CMissionMirror::LogEvent("lowrider-clock-hook-unavailable"); return false; }
    auto me = FindPlayerPed(0);
    auto host = CNetworkPlayerManager::GetPlayer(packet.owner);
    if (!me || me->m_fHealth <= 0 || !host || !host->m_pPed ||
        me->m_nAreaCode != host->m_pPed->m_nAreaCode || me->m_nAreaCode != 0 ||
        (me->GetPosition() - packet.pos).Magnitude() > 80.0f || !std::isfinite(packet.heading)) return false;
    const int slot = GuestSlot(packet.owner);
    CVector at{};
    if (!FreeCarSpot(packet, at, GetVehicle(g_prep.car))) {
        CMissionMirror::LogEvent("lowrider-no-free-slot"); return false;
    }
    int car = GetVehicle(g_prep.car) ? g_prep.car : -1;
    bool loaner = false;
    if (car < 0 && me->m_nPedFlags.bInVehicle && me->m_pVehicle && me->m_pVehicle->m_pDriver == me && me->m_pVehicle->m_nNumPassengers == 0 &&
        Cmd(0x0803).I(GetVehicleRef(me->m_pVehicle)).Run()) car = GetVehicleRef(me->m_pVehicle);
    if (g_prep.delivered && (car < 0 || GetVehicle(car)->m_fHealth <= 0)) {
        CMissionMirror::LogEvent("lowrider-loaner-lost"); return false;
    }
    if (car < 0) {
        LoadModelNow(kLoanerModel); LoadModelNow(kHydraulics);
        if (!IsModelLoaded(kLoanerModel) || !IsModelLoaded(kHydraulics)) return false;
        Cmd(0x00A5).I(kLoanerModel).F(at.x).F(at.y).F(at.z).Out(&car).Run();
        if (!GetVehicle(car)) return false;
        int mod = -1;
        Cmd(0x06E7).I(car).I(kHydraulics).Out(&mod).Run();
        loaner = true;
    }
    g_part = {}; g_clock = {};
    g_owner = packet.owner; g_session = packet.session; g_lastPacket = NowMs();
    g_part.kind = KIND_LOWRIDER; g_part.stage = 1; g_part.car = car; g_part.loaner = loaner;
    g_part.doorLock = int(GetVehicle(car)->m_eDoorLock);
    g_part.at = at; g_part.hostCar = packet.pos; g_part.heading = packet.heading; g_part.since = NowMs();
    const int h = GetPedRef(me);
    if (me->m_nPedFlags.bInVehicle && me->m_pVehicle && GetVehicleRef(me->m_pVehicle) != car) {
        auto pos = me->GetPosition();
        Cmd(0x0362).I(h).F(pos.x).F(pos.y).F(pos.z + 1).Run();
    }
    Cmd(0x036A).I(h).I(car).Run();
    Cmd(0x00AB).I(car).F(at.x).F(at.y).F(at.z).Run();
    Cmd(0x0175).I(car).F(packet.heading * 57.29578f).Run();
    Cmd(0x020A).I(car).I(4).Run();
    Cmd(0x0221).I(0).I(1).Run();
    Cmd(0x09EE).I(1).Run();
    Cmd(0x0952).I(packet.value).Run();
    Var(kBeatStop) = 0; Var(kBeatShown) = 0; Var(kLowHit) = 0;
    SpawnGirl();
    CStreaming::SetModelIsDeletable(kGirlModel);
    if (loaner) { CStreaming::SetModelIsDeletable(kLoanerModel); CStreaming::SetModelIsDeletable(kHydraulics); }
    Send(PacketT::JOIN);
    CMissionMirror::LogEventf("lowrider-join session=%u slot=%d", g_session, slot);
    return true;
}
CRunningScript* SweetThread() {
    for (auto s = CTheScripts::pActiveScripts; s; s = s->m_pNext)
        if (s->m_bIsMission && !_strnicmp(s->m_szName, "SWEET6", 8)) return s;
    return nullptr;
}
bool HostRunning() {
    if (!CCoopMission::IsScriptAuthority() || CMissionMode::IsPlayingAlone()) return false;
    for (auto s = CTheScripts::pActiveScripts; s; s = s->m_pNext)
        if (s->m_bIsMission && _strnicmp(s->m_szName, "SWEET6", 8) == 0) return true;
    return false;
}
void ProcessPreparation() {
    auto me = FindPlayerPed(0);
    if (!CCoopLowrider::Enabled() || !CNetwork::m_bConnected || !me || me->m_fHealth <= 0 ||
        CMissionMode::IsIndividual() || CCoopMission::HasLocalLeft()) {
        if (g_prep.state.session) ClearPreparation(false);
        return;
    }
    if (HostRunning()) {
        if (NowMs() - g_prep.sent < 200) return;
        g_prep.sent = NowMs();
        auto& p = g_prep.state;
        if (!p.session) p.session = uint32_t(NowMs()) | 1u;
        p.owner = CNetworkPlayerManager::m_localPlayerId;
        p.kind = 3; p.action = PacketT::PRELOAD; p.value = kLoanerModel;
        if (auto car = GetVehicle(CTheScripts::LocalVariablesForCurrentMission[71].iParam)) {
            p.pos = car->GetPosition(); p.heading = car->GetHeading();
            p.colors[0] = car->m_nPrimaryColor; p.colors[1] = car->m_nSecondaryColor;
        }
        p.beat = g_prep.delivered ? 1 : 0;
        if (g_prep.betSeen && NowMs() - g_prep.betSeen < 500) {
            p.beat |= 2;
            const int vars[] = {276, 274, 275};
            for (int i = 0; i < 3; ++i) p.wager[i] = std::clamp(CTheScripts::LocalVariablesForCurrentMission[vars[i]].iParam, 0, 10000000);
        }
        const CVector garage(2644.252f, -2028.246f, 12.5547f);
        std::vector<std::pair<playerid_t, CVector>> nearby;
        if (me->m_nAreaCode == 0) nearby.emplace_back(p.owner, me->GetPosition());
        for (auto other : CNetworkPlayerManager::m_pPlayers)
            if (CCoopMission::ShouldIncludeNetworkPlayer(other) && other->m_pPed && other->m_pPed->m_nAreaCode == 0 &&
                other->m_pPed->m_fHealth > 0) nearby.emplace_back(other->m_playerId, other->m_pPed->GetPosition());
        bool keep = false;
        // a guest already inside keeps the garage (the host waits outside his turn too)
        if (p.garageOwner != p.owner)
            for (auto& entry : nearby)
                if (entry.first == p.garageOwner && (entry.second - garage).Magnitude() < 28.f) keep = true;
        // the owner's mission sends him into the garage: he goes first whenever he is at its door
        if (!keep && me->m_nAreaCode == 0 && (me->GetPosition() - garage).Magnitude() < 23.f) {
            p.garageOwner = p.owner; keep = true;
        }
        for (auto& entry : nearby)
            if (!keep && entry.first == p.garageOwner && (entry.second - garage).Magnitude() < 28.f) keep = true;
        if (!keep) {
            std::sort(nearby.begin(), nearby.end(), [](auto& a, auto& b) { return a.first.value() < b.first.value(); });
            for (auto& entry : nearby) if ((entry.second - garage).Magnitude() < 23.f) {
                p.garageOwner = entry.first; keep = true; break;
            }
        }
        if (keep) p.beat |= 4;
        GetPacketFactory().Send(p);
    } else {
        if (!GuestEligible() || !g_prep.heard || !Authority(g_prep.state.owner) || NowMs() - g_prep.heard > 8000) {
            // 8 s: a paused or alt-tabbed host must not delete the guest's lowrider (it is also the contest car)
            if (g_prep.state.session) ClearPreparation(false);
            return;
        }
        DeliverCar();
        if (g_prep.delivered && (!GetVehicle(g_prep.car) || GetVehicle(g_prep.car)->m_fHealth <= 0)) {
            // Do not replace a destroyed mission car every frame.
            if (g_part.kind) FinishLowrider();
        }
    }
    if (!(g_prep.state.beat & 1)) return;
    const bool allowed = (g_prep.state.beat & 4) && g_prep.state.garageOwner == CNetworkPlayerManager::m_localPlayerId;
    if (!g_prep.garageManaged || allowed != g_prep.garageAllowed) {
        Cmd(allowed ? 0x0299 : 0x02B9).S8("MODLAST").Run();
        g_prep.garageManaged = true; g_prep.garageAllowed = allowed;
    }
    if (!allowed && me->m_nAreaCode == 0 &&
        (me->GetPosition() - CVector(2644.252f, -2028.246f, 12.5547f)).Magnitude() < 23.f)
        CHud::SetHelpMessage("Garaje ocupado. Espera afuera tu turno.", true, false, false);
}
void ReleaseHold() {
    if (g_hold) { Var(kLowRunning) = 0; g_hold = 0; }
}

// MUSIC1 only. Reuses T53's command builder, packet routing and mission lifecycle.
namespace Dance {
constexpr uint32_t Flags = 21180, Score = 21184, Quit = 21192, Cams = 21260;
bool Enabled() { return CConfigLoader::ms_bCoopDance && CConfigLoader::ms_bVanillaScripts; }
bool host = false, guest = false, started = false, saw = false, stopping = false;
PacketT run{};
uint64_t since = 0, hold = 0, heartbeat = 0, heard = 0;
int sent = 0;
struct ScoreState { int total = 0; bool done = false; };
std::map<uint64_t, ScoreState> scores;
bool teamScore = false;
bool resultFinalized = false;
bool resultPassed = false;
int teamShown = 0, teamGoal = 0;  // guest: last team total/goal heard from the host
uint64_t teamAt = 0;
// Host: own score plus every included guest that joined; goal is 2500 per dancer.
void Tally(int& total, int& participants) {
    total = std::max(0, Var(Score)); participants = 1;
    for (const auto& entry : scores) {
        auto* player = CNetworkPlayerManager::GetPlayer(playerid_t(entry.first));
        if (!player || !CCoopMission::ShouldIncludeNetworkPlayer(player)) continue;
        total += entry.second.total; ++participants;
    }
}
bool Music() {
    if (!CCoopMission::IsScriptAuthority() || CMissionMode::IsPlayingAlone()) return false;
    for (auto s = CTheScripts::pActiveScripts; s; s = s->m_pNext)
        if (s->m_bIsMission && !_strnicmp(s->m_szName, "MUSIC1", 8)) return true;
    return false;
}
CRunningScript* Thread() {
    for (auto s = CTheScripts::pActiveScripts; s; s = s->m_pNext)
        if (!_strnicmp(s->m_szName, "DANCE", 8)) return s;
    return nullptr;
}
// Story missions never open a CCoopMission (only side missions do), so IsLocalParticipant was always false there and
// every guest skipped the dance as "not-participant" (T97). Same test as the lowrider's GuestEligible.
bool Participant() {
    if (CConfigLoader::ms_bDanceGuestStory)
        return !CCoopMission::HasLocalLeft() && (!CCoopMission::IsActive() || CCoopMission::IsLocalParticipant());
    return CCoopMission::IsLocalParticipant();
}
void Send(uint8_t action, int value = 0) {
    auto p = run; p.action = action; p.value = value; GetPacketFactory().Send(p);
}
void Release() { if (hold) Var(Flags) &= 0x7fffffff; hold = 0; }
void Reset(bool restarted) {
    if (!restarted) {
        Release();
        if (guest) {
            if (started) Var(Quit) = 1;
            else Cmd(0x090F).I(35).Run();
            Send(PacketT::DONE);
            CCameraMirror::ArmFadeGuard();
        }
    }
    host = guest = started = saw = stopping = false; hold = 0; scores.clear(); run = {};
}
void After(CRunningScript* script, uint16_t opcode) {
    if (!script || COpCodeSync::bProcessingNetworkOpcode) return;
    if (guest && started && !_strnicmp(script->m_szName, "DANCE", 8) && Var(Score) > sent) {
        sent = Var(Score); Send(PacketT::POINTS, sent);
    }
    if (!Enabled() || !Music()) return;
    if (opcode == 0x0913 && !_strnicmp(script->m_szName, "MUSIC1", 8) ) {
        auto dance = CTheScripts::pActiveScripts;
        if (!dance || (_strnicmp(dance->m_szName, "noname", 8) && _strnicmp(dance->m_szName, "dance", 8))) return;
        // StartNewStreamedScript (gta-reversed 0x470890) puts the external thread at the active head.
        const auto code = dance->m_pBaseIP;
        if (!code || code[0] != 0xa4 || code[1] != 3 || code[2] != 9 ||
            _strnicmp(reinterpret_cast<const char*>(code + 3), "DANCE", 5) ||
            dance->m_aLocalVars[5].iParam != -1) return;
        run = {}; run.kind = 2; run.owner = CNetworkPlayerManager::m_localPlayerId;
        run.session = uint32_t(NowMs()) | 1u;
        memcpy(&run.pos.x, &dance->m_aLocalVars[0], 4);
        memcpy(&run.pos.y, &dance->m_aLocalVars[1], 4);
        memcpy(&run.pos.z, &dance->m_aLocalVars[2], 4);
        memcpy(&run.heading, &dance->m_aLocalVars[3], 4);
        run.value = dance->m_aLocalVars[4].iParam;
        if (run.value < 0 || run.value >= 14) return;
        memcpy(run.cameras, &Var(Cams), sizeof run.cameras);
        scores.clear(); host = true; stopping = false; hold = 0;
        teamScore = CConfigLoader::ms_bDanceTeamScore;
        resultFinalized = false;
        Send(PacketT::PRELOAD, run.value);
        CMissionMirror::LogEventf("dance-start session=%u", run.session);
    }
    if (host && !stopping && opcode == 0x08C0 && !_strnicmp(script->m_szName, "DANCE", 8) &&
        !(uint32_t(Var(Flags)) & 0x80000000u)) {
        stopping = true;
        if (!scores.empty()) { Var(Flags) |= int32_t(0x80000000u); hold = NowMs() + 3000; }
    }
}
// A spot beside the host's: across the view of the first fixed camera (MUSIC1 sets the five to 531.18,-1893.8 looking at
// 533.94,-1891.82, 3.4 m from the dancer: the old 3 m left the other one out of the shot), 1.25 m per pair of slots,
// the other side or 1.0/1.5 m when a ped, a car (the van) or a wall is there. Without fixed cameras (camX[0] <= 0 the
// script films each dancer from offsets of its own CJ) the side of the heading. z stays -100: DANCE finds the ground.
CVector SideBySide(const PacketT& p, CPed* host, CPed* me, int slot) {
    const float angle = p.heading * 0.0174533f;
    CVector2D side(cosf(angle), sinf(angle));
    if (p.cameras[0] > 0.f) {
        CVector2D view(p.cameras[15] - p.cameras[0], p.cameras[20] - p.cameras[5]);
        const float len = view.Magnitude();
        if (len > 0.1f) side = CVector2D(-view.y / len, view.x / len);
    }
    const float pref = slot % 2 ? -1.f : 1.f;
    const float base = 1.25f * (1 + slot / 2);
    // the dance spot (the copy of the host may still be on its way there), one metre over its ground
    bool hostGround = false;
    CVector origin(p.pos.x, p.pos.y, host->GetPosition().z);
    const float spotZ = CWorld::FindGroundZFor3DCoord(origin.x, origin.y, origin.z + 2.f, &hostGround, nullptr);
    if (hostGround) origin.z = spotZ + 1.f;
    const float sides[2] = {pref, -pref};
    const float extra[3] = {0.f, 0.25f, -0.25f};
    for (float s : sides) for (float e : extra) {
        const float d = (base + e) * s;
        CVector at(p.pos.x + side.x * d, p.pos.y + side.y * d, origin.z);
        bool found = false;
        const float ground = CWorld::FindGroundZFor3DCoord(at.x, at.y, origin.z + 1.5f, &found, nullptr);
        if (!found || fabsf(ground - (origin.z - 1.0f)) > 1.2f) continue;
        at.z = ground;
        if (!CWorld::GetIsLineOfSightClear(origin, at + CVector(0, 0, 1.f), true, true, false, true, false, false, false)) continue;
        bool blocked = false;
        for (float h : {0.6f, 1.3f})
            if (auto hit = CWorld::TestSphereAgainstWorld(at + CVector(0, 0, h), 0.45f, me, true, true, true, true, false, false);
                hit && hit != host) blocked = true;
        auto& vehicles = *CPools::ms_pVehiclePool;
        for (int i = 0; !blocked && i < vehicles.m_nSize; ++i)
            if (auto car = vehicles.GetAt(i); car && (car->GetPosition() - at).Magnitude2D() < 2.2f) blocked = true;
        if (blocked) continue;
        CMissionMirror::LogEventf("dance-spot slot=%d side=%.0f dist=%.2f", slot, s, base + e);
        return CVector(side.x * d, side.y * d, 0.f);
    }
    CMissionMirror::LogEventf("dance-spot slot=%d side=%.0f dist=%.2f reason=all-blocked", slot, pref, base);
    return CVector(side.x * base * pref, side.y * base * pref, 0.f);
}
void Packet(const PacketT& p) {
    if (!Enabled()) return;
    if (p.action >= PacketT::JOIN) {
        auto remote = CNetworkPlayerManager::GetPlayer(p.sender);
        if (!host || (teamScore && resultFinalized) || (stopping && !hold) || !Music() || p.owner != run.owner || p.session != run.session || !remote ||
            !CCoopMission::ShouldIncludeNetworkPlayer(remote)) return;
        if (p.action == PacketT::JOIN) {
            if (teamScore && stopping) return;
            scores.emplace(p.sender.value(), ScoreState{});
            CMissionMirror::LogEventf("dance-guest-joined session=%u", run.session);
            if (teamScore)
                CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Baile en equipo: meta %d puntos entre %d bailarines (2500 cada uno).",
                    int(scores.size() + 1) * 2500, int(scores.size() + 1));
            return;
        }
        auto it = scores.find(p.sender.value());
        if (it == scores.end() || it->second.done) return;
        auto& score = it->second;
        // Team mode: totals are cumulative, so one lost or skipped packet must not freeze a guest's score forever.
        if (p.action == PacketT::POINTS && p.value >= score.total && p.value <= 1000000 &&
            (teamScore || p.value - score.total <= 500)) {
            if (!teamScore)
                Var(Score) = std::min(10000000, Var(Score) + p.value - score.total);
            score.total = p.value;
        } else if (p.action == PacketT::POINTS && p.value >= score.total && p.value <= 1000000) {
            // a jump after a lost packet: resync without counting it, or every later packet would be rejected too
            score.total = p.value;
        } else if (p.action == PacketT::DONE) score.done = true;
        return;
    }
    if (p.sender != p.owner || !Authority(p.owner)) {
        if (p.action == PacketT::PRELOAD) CMissionMirror::LogEvent("dance-skip reason=not-authority");
        return;
    }
    if (guest && p.owner == run.owner && p.session == run.session) {
        heard = NowMs();
        if (p.action == PacketT::STOP) Var(Quit) = 1;
        else if (p.action == PacketT::TIME && CConfigLoader::ms_bDanceTeamScore && p.beat > 0) {
            teamShown = p.value; teamGoal = p.beat; teamAt = NowMs();
        }
        return;
    }
    if (p.action != PacketT::PRELOAD || guest) return;
    // Not joining: the host's MUSIC1 already faded our screen out (mirrored) and its DANCE thread, which is not synced, does the
    // fade-in only for itself, so give the picture back and watch (T89)
    auto skip = [](const char* why) {
        CMissionMirror::LogEventf("dance-skip reason=%s", why);
        CCameraMirror::RestoreFade("dance-skip");
    };
    if (CCoopLowrider::OwnContest()) return;
    if (!CNetwork::m_bConnected) return skip("offline");
    if (CCoopMission::IsScriptAuthority()) return skip("authority");
    if (CMissionMode::IsIndividual()) return skip("individual");
    if (CCoopMission::HasLocalLeft()) return skip("left");
    if (!Participant()) return skip("not-participant");
    if (COpCodeSync::IsLocalMissionRunning()) return skip("local-mission");
    if (Thread()) return skip("dance-thread");
    auto me = FindPlayerPed(0); auto owner = CNetworkPlayerManager::GetPlayer(p.owner);
    if (!me || me->m_fHealth <= 0) return skip("dead");
    if (!owner || !owner->m_pPed) return skip("no-owner");
    if (me->m_nAreaCode != 0 || me->m_nAreaCode != owner->m_pPed->m_nAreaCode) return skip("area");
    if (!std::isfinite(p.heading) || !std::isfinite(p.pos.x) || !std::isfinite(p.pos.y) || p.value < 0 || p.value >= 14) return skip("bad-packet");
    const auto delta = me->GetPosition() - p.pos;
    if (delta.x * delta.x + delta.y * delta.y > 1600) return skip("far");
    for (float c : p.cameras) if (!std::isfinite(c)) return skip("bad-cameras");
    std::vector<uint64_t> ids{CNetworkPlayerManager::m_localPlayerId.value()};
    for (auto other : CNetworkPlayerManager::m_pPlayers)
        if (other->m_playerId != p.owner && CCoopMission::ShouldIncludeNetworkPlayer(other)) ids.push_back(other->m_playerId.value());
    std::sort(ids.begin(), ids.end());
    int slot = int(std::find(ids.begin(), ids.end(), CNetworkPlayerManager::m_localPlayerId.value()) - ids.begin());
    const float offset = (slot % 2 ? -1.f : 1.f) * (3.f + (slot / 2) * 3.f);
    const float angle = p.heading * 0.0174533f;
    CVector shift(cosf(angle) * offset, sinf(angle) * offset, 0);
    float camShare = 1.f;
    if (CConfigLoader::ms_bDanceSideBySide) {
        shift = SideBySide(p, owner->m_pPed, me, slot);
        camShare = 0.5f;
    }
    run = p; run.pos += shift;
    // full shift: our own shot (old); half: the shot between the host and us, both in it
    for (int i = 0; i < 30; ++i) run.cameras[i] += camShare * (i / 5 % 3 == 0 ? shift.x : i / 5 % 3 == 1 ? shift.y : 0.f);
    if (me->m_nPedFlags.bInVehicle) {
        auto at = me->GetPosition(); Cmd(0x0362).I(GetPedRef(me)).F(at.x).F(at.y).F(at.z + 1).Run();
    }
    guest = true; started = saw = false; since = heard = NowMs(); sent = 0;
    Cmd(0x08A9).I(35).Run();
    CMissionMirror::LogEventf("dance-join session=%u slot=%d", run.session, slot);
}
void Process() {
    if (host) {
        if (!Enabled() || !CNetwork::m_bConnected || !Music()) { Send(PacketT::STOP); Release(); host = false; scores.clear(); }
        else {
            for (auto it = scores.begin(); it != scores.end();) {
                auto p = CNetworkPlayerManager::GetPlayer(playerid_t(it->first));
                if (!p || !CCoopMission::ShouldIncludeNetworkPlayer(p)) it = scores.erase(it); else ++it;
            }
            if (hold && (NowMs() >= hold || std::all_of(scores.begin(), scores.end(), [](auto& v) { return v.second.done; }))) Release();
            if (NowMs() - heartbeat >= 500) {
                heartbeat = NowMs();
                auto p = run; p.action = PacketT::TIME; p.value = 0; p.beat = 0;
                int total = 0, participants = 1;
                if (teamScore) { Tally(total, participants); p.value = std::min(total, 3600000); p.beat = participants * 2500; }
                GetPacketFactory().Send(p);
            }
        }
    }
    if (!guest) return;
    auto me = FindPlayerPed(0);
    bool cancel = !Enabled() || !CNetwork::m_bConnected || !Authority(run.owner) ||
        CCoopMission::HasLocalLeft() || !Participant() || CMissionMode::IsIndividual() ||
        !me || me->m_fHealth <= 0 || NowMs() - heard > 5000;
    if (!started) {
        if (cancel || NowMs() - since > 10000) { Reset(false); return; }
        if (!Cmd(0x08AB).I(35).Run()) return;
        memcpy(&Var(Cams), run.cameras, sizeof run.cameras); Var(Quit) = 0; Var(Score) = 0;
        Cmd(0x0913).I(35).F(run.pos.x).F(run.pos.y).F(-100.f).F(run.heading).I(run.value).I(-1).End().Run();
        started = true; since = NowMs(); Send(PacketT::JOIN);
        CMissionMirror::LogEventf("dance-guest-started session=%u", run.session);
        return;
    }
    if (cancel) Var(Quit) = 1;
    if (Thread()) { saw = true; return; }
    // DANCE's last score change was reported by After before the thread ended.
    // The global is shared with other scripts and is no longer ours to report.
    if (!saw && NowMs() - since < 5000) return;
    Send(PacketT::DONE); Cmd(0x090F).I(35).Run();
    CMissionMirror::LogEventf("dance-guest-done score=%d saw=%d", sent, saw ? 1 : 0);
    guest = started = saw = false;
    CCameraMirror::ArmFadeGuard();
}
} // namespace Dance
} // namespace

bool CCoopLowrider::Enabled() {
    return CConfigLoader::ms_bCoopLowrider && CConfigLoader::ms_bVanillaScripts;
}
bool CCoopLowrider::OwnContest() { return g_part.kind != 0 || Dance::guest; }
void CCoopLowrider::Reset(bool gameRestarted) {
    Dance::Reset(gameRestarted);
    if (!gameRestarted) {
        if (g_part.kind) FinishLowrider();
        ReleaseHold();
    }
    ClearPreparation(gameRestarted);
    g_part = {}; g_clock = {}; g_took.clear();
    g_host = g_stopping = g_play = false; g_hold = 0; g_session = 0; g_owner = {};
}

void CCoopLowrider::DrawDanceTeam() {
    using namespace Dance;
    if (!CConfigLoader::ms_bDanceTeamScore || !Enabled()) return;
    int total = 0, goal = 0;
    if (host && teamScore && !resultFinalized && Thread()) {
        int participants = 1; Tally(total, participants);
        if (participants < 2) return;
        goal = participants * 2500;
    } else if (guest && started && Thread() && NowMs() - teamAt < 3000) {
        total = teamShown; goal = teamGoal;
    } else return;
    char text[48];
    sprintf_s(text, "EQUIPO %d / %d", total, goal);
    CFont::SetBackground(false, false);
    CFont::SetProportional(true);
    CFont::SetJustify(false);
    CFont::SetRightJustifyWrap(0.0f);
    CFont::SetOrientation(ALIGN_RIGHT);
    CFont::SetFontStyle(FONT_PRICEDOWN);
    CFont::SetEdge(1);
    CFont::SetDropColor(CRGBA(0, 0, 0, 255));
    CFont::SetColor(total >= goal ? CRGBA(80, 220, 110, 255) : CRGBA(225, 225, 225, 255));
    CFont::SetScale(RsGlobal.maximumWidth * 0.00055f, RsGlobal.maximumHeight * 0.0017f);
    CFont::PrintString(RsGlobal.maximumWidth - RsGlobal.maximumWidth * 0.04f, RsGlobal.maximumHeight * 0.20f, text);
}

bool CCoopLowrider::FilterDanceResult(CRunningScript* script, uint16_t opcode, bool state)
{
    if (!Dance::host || !Dance::teamScore || !Dance::Enabled() ||
        opcode != 0x0028 || _strnicmp(script->m_szName, "MUSIC1", 8) != 0 ||
        COpCodeSync::bProcessingNetworkOpcode)
        return state;

    // Vanilla MUSIC1's sole comparison of $iDanceScore against 2500.
    constexpr uint8_t compare[] = {0x28, 0x00, 0x02, 0xC0, 0x52, 0x05, 0xC4, 0x09};
    if (!script->m_pBaseIP || !script->m_pCurrentIP ||
        reinterpret_cast<uintptr_t>(script->m_pCurrentIP) < reinterpret_cast<uintptr_t>(script->m_pBaseIP) + sizeof(compare) ||
        memcmp(script->m_pCurrentIP - sizeof(compare), compare, sizeof(compare)) != 0)
        return state;

    if (!Dance::resultFinalized) {
        int total = 0, participants = 1;
        Dance::Tally(total, participants);
        const int required = participants * 2500;
        Dance::resultPassed = total >= required;
        Dance::resultFinalized = true;
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Baile en equipo: %d de %d puntos (%d bailarines).", total, required, participants);
        CMissionMirror::LogEventf("dance-team-result participants=%d total=%d required=%d", participants, total, required);
    }
    return Dance::resultPassed;
}

void CCoopLowrider::AfterCommand(CRunningScript* script, uint16_t opcode) {
    Dance::After(script, opcode);
    if (!Enabled() || !script || COpCodeSync::bProcessingNetworkOpcode || !HostRunning()) return;
    const bool sweet = _strnicmp(script->m_szName, "SWEET6", 8) == 0;
    if (sweet && opcode == 0x01B4 && ScriptParams[1] == 1) {
        auto me = FindPlayerPed(0);
        if (me && me->m_nPedFlags.bInVehicle && me->m_pVehicle && me->m_pVehicle->m_nModelIndex == kLoanerModel &&
            (me->GetPosition() - CVector(2644.252f, -2028.246f, 12.5547f)).Magnitude() < 40.f)
            g_prep.delivered = true;
    }
    if (sweet && opcode == 0x0937) g_prep.betSeen = NowMs();
    if (opcode == 0x00BC && !_strnicmp(script->m_szName, "AUDIOL", 8)) {
        char key[8]{};
        memcpy(key, &script->m_aLocalVars[5], 7);
        if (!strncmp(key, "LOWR_", 5)) {
            for (auto remote : CNetworkPlayerManager::m_pPlayers) if (CCoopMission::ShouldIncludeNetworkPlayer(remote)) {
                Packets::Scripts::AddMessageGXT text;
                text.forWhoPlayerId = remote->m_playerId;
                text.type = Packets::Scripts::AddMessageGXT::sync_COMMAND_PRINT_NOW;
                text.time = 10000; text.flag = 1; memcpy(text.gxt, key, 8);
                GetPacketFactory().Send(text);
            }
            CMissionMirror::LogEventf("lowrider-subtitle key=%.7s", key);
        }
    }
    if (opcode == 0x0952 && sweet) {
        const int track = ScriptParams[0];
        auto me = FindPlayerPed(0);
        if (!me || !me->m_nPedFlags.bInVehicle || !me->m_pVehicle || track < 0 || track >= 14) return;
        g_session = uint32_t(NowMs()) | 1u; g_owner = CNetworkPlayerManager::m_localPlayerId;
        g_host = true; g_play = g_stopping = false; g_hold = 0; g_took.clear();
        PacketT p{}; p.action = PacketT::PRELOAD; p.owner = g_owner; p.session = g_session;
        p.value = track; p.pos = me->m_pVehicle->GetPosition(); p.heading = me->m_pVehicle->GetHeading();
        GetPacketFactory().Send(p);
    } else if (g_host && opcode == 0x0954 && sweet) {
        g_play = true; Send(PacketT::PLAY);
    } else if (g_host && opcode == 0x0955 && (sweet || _strnicmp(script->m_szName, "LOWGAME", 8) == 0)) {
        // STOP follows the final judgement; keep the result pending for reliable final scores.
        if (!g_stopping) { g_stopping = true; g_hold = NowMs() + 3000; }
        Send(PacketT::STOP);
    }
    if (g_host && g_stopping && g_hold && _strnicmp(script->m_szName, "LOWGAME", 8) == 0 && Var(kLowRunning) == 0)
        Var(kLowRunning) = 1;
}

void CCoopLowrider::OnPacket(const PacketT& p) {
    if (p.kind == 3) {
        if (p.action != PacketT::PRELOAD || !GuestEligible() || p.sender != p.owner || !Authority(p.owner) ||
            p.beat > 7 || !std::isfinite(p.pos.x) || !std::isfinite(p.pos.y) || !std::isfinite(p.pos.z) || !std::isfinite(p.heading)) return;
        g_prep.state = p; g_prep.heard = NowMs();
        return;
    }
    if (p.kind == 2) { Dance::Packet(p); return; }
    if (!Enabled()) return;
    if (p.action >= PacketT::JOIN) {
        auto remote = CNetworkPlayerManager::GetPlayer(p.sender);
        if (!g_host || !HostRunning() || p.owner != g_owner || p.session != g_session || !remote ||
            !CCoopMission::ShouldIncludeNetworkPlayer(remote)) return;
        auto it = g_took.find(p.sender.value());
        if (p.action == PacketT::JOIN) { g_took.emplace(p.sender.value(), Taking{}); return; }
        if (it == g_took.end() || it->second.done) return;
        auto& t = it->second;
        if (p.action == PacketT::POINTS && p.beat > t.lastBeat && p.value >= 0 && p.value <= 500) {
            t.lastBeat = p.beat;
            Var(kLowScore) = std::min(10000000, Var(kLowScore) + p.value);
        } else if (p.action == PacketT::DONE) t.done = true;
        return;
    }
    if (!GuestEligible() || p.sender != p.owner || !Authority(p.owner)) return;
    if (p.action == PacketT::PRELOAD) {
        if (!OwnContest()) StartLowrider(p);
        if (OwnContest() && p.owner == g_owner && p.session == g_session) g_lastPacket = NowMs();
        return;
    }
    if (!OwnContest() || p.session != g_session || p.owner != g_owner) return;
    g_lastPacket = NowMs();
    if (p.action == PacketT::PLAY) g_part.play = true;
    else if (p.action == PacketT::TIME) HearBeatTime(p.value);
    else if (p.action == PacketT::STOP) FinishLowrider();
}

void CCoopLowrider::AfterScripts() {
    // CTheScripts::Process clears IntroTextLines before executing the scripts.
    if (GuestEligible() && g_prep.heard && Authority(g_prep.state.owner)) DrawWager();
}
void CCoopLowrider::Process() {
    Dance::Process();
    ProcessPreparation();
    static bool hooked = false;
    if (Enabled() && !hooked) {
        // Only replace the game's own call site; do not overwrite another plugin's hook.
        const auto target = injector::GetBranchDestination(0x4D8FEF).as_int();
        if (*reinterpret_cast<uint8_t*>(0x4D8FEF) == 0xE8 && target == 0x4F1530) {
            g_origPlayTime = reinterpret_cast<PlayTimeFn>(target);
            patch::RedirectCall(0x4D8FEF, HookBeatPlayTime);
        }
        if (!g_origPlayTime) CMissionMirror::LogEvent("lowrider-clock-hook-skipped");
        hooked = true;
    }
    if (g_host) {
        if (!Enabled() || !CNetwork::m_bConnected || !HostRunning()) {
            ReleaseHold(); g_host = false; g_took.clear();
        } else {
            for (auto it = g_took.begin(); it != g_took.end();) {
                auto p = CNetworkPlayerManager::GetPlayer(playerid_t(it->first));
                if (!p || !CCoopMission::ShouldIncludeNetworkPlayer(p)) it = g_took.erase(it);
                else ++it;
            }
            const bool allDone = std::all_of(g_took.begin(), g_took.end(), [](const auto& v) { return v.second.done; });
            if (g_hold && (NowMs() >= g_hold || allDone)) ReleaseHold();
            if (!g_stopping && NowMs() - g_lastTime >= 100) {
                g_lastTime = NowMs();
                const int t = plugin::CallMethodAndReturn<int, 0x4F1530>(reinterpret_cast<void*>(kStreamThread));
                Send(PacketT::TIME, g_play && t >= 0 ? t : 0);
            }
        }
    }
    if (!g_part.kind) return;
    if (!GuestEligible() || !Authority(g_owner) || !GetVehicle(g_part.car) ||
        !FindPlayerPed(0) || FindPlayerPed(0)->m_fHealth <= 0 || (NowMs() - g_lastPacket > 5000)) {
        FinishLowrider(); return;
    }
    if (g_part.stage == 1) {
        int status = 0; Cmd(0x0953).Out(&status).Run(); MoveGirl(0);
        if (g_part.play && status == 2) {
            // Validate BDISPLY's SCRIPT_NAME opcode before starting at the vanilla MAIN offset.
            const auto code = reinterpret_cast<const uint8_t*>(&CTheScripts::ScriptSpace[kBdisply]);
            if (code[0] != 0xA4 || code[1] != 3 || code[2] != 9 || _strnicmp(reinterpret_cast<const char*>(code+3), "BDISPLY", 7)) {
                CMissionMirror::LogEvent("lowrider-bdisply-layout-mismatch"); FinishLowrider(); return;
            }
            Cmd(0x0954).Run(); Var(kBeatShown) = 0;
            Cmd(0x004F).I(kBdisply).End().Run();
            g_part.stage = 2; StartCamera();
            CMissionMirror::LogEventf("lowrider-playing car=%d girl=%d status=%d", g_part.car, g_part.girl, status);
        } else if (NowMs() - g_lastPacket > 5000) {
            CMissionMirror::LogEventf("lowrider-preload-timeout status=%d play=%d", status, g_part.play);
            FinishLowrider();
        }
    } else {
        auto me = FindPlayerPed(0);
        if (!me->m_nPedFlags.bInVehicle || me->m_pVehicle != GetVehicle(g_part.car)) FinishLowrider();
        else PlayLowrider();
    }
}
