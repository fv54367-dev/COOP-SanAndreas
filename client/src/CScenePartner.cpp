// Adapted from Karimov-Akbar/sa-coop (MIT), features/story/cutscene-partner/ScenePartner.cpp; see NOTICE.md.
#include "SceneCompat/ScenePartner.h"
#include "SceneCompat/SceneCameraPath.h"
#include "SceneCompat/SceneGeometry.h"
#include "SceneCompat/SceneWaypoint.h"
#include "SceneCompat/SceneFidget.h"
#include "CConfigLoader.h"

#include <windows.h>
#undef near  // windows.h's macros, and names here
#undef far
#undef STRICT

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "SceneCompat/Support.h"
#include "SceneCompat/Support.h"
#include "SceneCompat/SceneMocap.h"
#include "SceneCompat/Anim.h"
#include "SceneCompat/Script.h"

namespace coop::scene {

namespace {
using namespace scene_game;
using namespace camera_path;
using namespace geometry;
using namespace waypoint;
using namespace fidget;
using scene_game::script::Cmd;

constexpr uintptr_t kCutsceneObjects = 0xBC3F18, kNumCutsceneObjects = 0xBC3FE4;  // CCutsceneMgr::ms_pCutsceneObjects
constexpr uintptr_t kCutsceneName = 0xBC3F0C;                                         // CCutsceneMgr::ms_cutsceneName
constexpr uintptr_t kCameraMatrix = addr::TheCamera + 0x14;                         // CPlaceable::m_matrix
constexpr uintptr_t kCurrArea = 0xB72914;                                             // CGame::currArea
constexpr int kCigarette = 1485;                                                      // CJ_CIGGY
constexpr int8_t kAttractorSeat = 1, kAttractorStep = 9;                              // C2dEffectPedAttractor types
constexpr float kRejected = -1e9f;

// A variation played now and then instead of the act's loop: the person nods, thinks, shifts the weight, looks at
// the watch. Each starts and ends in its loop's pose (their root tracks stay within 6 cm of it), so the blends show
// no jump.

const Fidget kListen[] = {{"COP_AMBIENT", "Coplook_nod", true},   {"COP_AMBIENT", "Coplook_think", true},
                          {"PLAYIDLES", "shift", false},          {"PLAYIDLES", "shldr", false},
                          {"COP_AMBIENT", "Coplook_nod", true},   {"PLAYIDLES", "strleg", false},
                          {"PED", "XPRESSscratch", true},         {"PLAYIDLES", "time", true},
                          {"COP_AMBIENT", "Coplook_shake", true}, {"PED", "roadcross_gang", true}};
const Fidget kDealer[] = {{"DEALER", "DEALER_IDLE_01", false}, {"DEALER", "DEALER_IDLE_02", false},
                          {"DEALER", "DEALER_IDLE_03", false}, {"COP_AMBIENT", "Coplook_nod", true},
                          {"PLAYIDLES", "shift", false},       {"PED", "XPRESSscratch", true}};
const Fidget kSmoke[] = {{"SMOKING", "M_smk_drag", true}, {"SMOKING", "M_smk_tap", false}};
const Fidget kSeated[] = {{"MISC", "SEAT_watch", true}};


// standing about, leaning on a wall, on a seat, on steps
const Act kStand[] = {{"DEALER", "DEALER_IDLE", false, 0, kDealer, std::size(kDealer)},
                      {"PED", "Idle_Gang1", false, 0, kListen, std::size(kListen)},
                      {"COP_AMBIENT", "Coplook_loop", false, 0, kListen, std::size(kListen)},
                      {"SMOKING", "M_smkstnd_loop", true, 0, kSmoke, std::size(kSmoke)}};
const Act kClips = {kMocapBlock, nullptr, false, 0};  // standing as CJ does in the cutscenes: in place of all but smoking
const Act kLean[] = {{"GANGS", "leanIDLE", false, 0.5f}, {"SMOKING", "M_smklean_loop", true, 0.24f}};
// SEAT_idle also moves the body 0.54 m down: the hips 0.46 m over the feet
const Act kSit[] = {{"PED", "SEAT_idle", false, 0.515f, kSeated, std::size(kSeated)}};
const Act kStep[] = {{"Attractors", "Stepsit_loop", false, 0.3f}};

template <size_t N>
const Act* Pick(const Act (&list)[N], uint8_t i) {
    return &list[i % N];
}

const Act* ActOf(const Spot& s) {
    switch (s.pose) {
        case POSE_STAND: return s.act % std::size(kStand) != 3 && MocapCount() > 1 ? &kClips : Pick(kStand, s.act);
        case POSE_WALK:
        case POSE_WAY: return MocapCount() > 1 ? &kClips : Pick(kStand, 1);  // what he does when CJ stops
        case POSE_LEAN: return Pick(kLean, s.act);
        case POSE_SIT: return Pick(kSit, s.act);
        case POSE_STEP: return Pick(kStep, s.act);
        default: return nullptr;
    }
}



#include "CScenePartner_Geometry.inc"
#include "CScenePartner_Camera.inc"
#include "CScenePartner_Selection.inc"
#include "CScenePartner_Waypoints.inc"
std::map<scene_game::CPed*, Held> g_held;
}  // namespace

// The map's animated objects need their IFP, and the game streams no IFP while a cutscene runs (CStreaming::RequestModelStream). Played
// far from where the game has been, a scene shows such a building's LOD all through: the Burger Shot of STRAP1A in /cs on both screens,
// in one of 147 scenes, as the request goes through right after it (06.11). The same for a partner far from the host in the real mission.
const struct {
    const char* scene;
    const char* block;
} kMapBlocks[] = {
    {"STRAP1A", "LAW"}, {"STRAP4A", "LAW"}, {"SYND_3A", "SFSE"}, {"TRUTH_2", "COUNXREF"}, {"CRASV2A", "COUNXREF"}, {"CRASV2B", "COUNXREF"},
};

void Prepare(const char* cutscene) {
    for (const auto& b : kMapBlocks)
        if (!_stricmp(cutscene, b.scene)) BlockReady(b.block);
    for (const auto& list : {std::vector<Act>(std::begin(kStand), std::end(kStand)), std::vector<Act>(std::begin(kLean), std::end(kLean)),
                             std::vector<Act>(std::begin(kSit), std::end(kSit)), std::vector<Act>(std::begin(kStep), std::end(kStep))})
        for (const Act& a : list) {
            BlockReady(a.block);
            for (size_t i = 0; i < a.fidgetCount; ++i) BlockReady(a.fidgets[i].block);
        }
    RequestModel(kCigarette);
    LoadAllRequestedModels(false);
}

// The view matrix is rebuilt only when the world is drawn: on a black screen (a fade) it still holds an old camera,
// and nothing projects right. A point 10 m ahead of the camera must come out in the middle of the screen.
bool ViewIsCurrent(const Scene& sc) {
    float x = 0, y = 0, d = 0;
    bool ok = WorldToScreen(sc.cam + sc.camFwd * 10.0f, x, y, d) && std::fabs(d - 10.0f) < 0.5f &&
              std::fabs(x - sc.w * 0.5f) < sc.w * 0.05f && std::fabs(y - sc.h * 0.5f) < sc.h * 0.05f;
    static uint64_t logged = 0;
    if (!ok && NowMs() - logged > 2000) {
        logged = NowMs();
        LOGD("Cutscene view not drawn yet (10 m ahead shows at %.0f,%.0f, depth %.1f)", x, y, d);
    }
    return ok;
}

void Analyse() {
    UpdateTalk();
    Scene sc;
    g_live = BuildScene(sc);
    if (!g_live || !ViewIsCurrent(sc)) return;
    const char* name = (const char*)kCutsceneName;
    if (strncmp(name, g_cutscene, 8) != 0) {  // another cutscene: its own look for the partner
        strncpy(g_cutscene, name, 8);
        g_seed = 2166136261u;
        for (const char* c = g_cutscene; *c; ++c) g_seed = (g_seed ^ (uint8_t)*c) * 16777619u;
        g_haveSpot = g_haveSeat = g_seenBefore = false;
        g_side = 0;
        g_viewMoved = -1e9f;
        g_plan.clear();
    }
    // The first keys of CJ's track fly him in from far off in 12 of the 148 scenes (INTRO1A: 17 m in four frames). A place
    // chosen meanwhile was by the stairs, 6 m over the room he landed in (05.11): the first one waits till he is down.
    Track cjTrack;
    if (!g_haveSpot && TrackOf(CutsceneCj(), cjTrack) && cjTrack.time < 0.2f &&
        (RootAt(cjTrack.seq, cjTrack.time + 0.2f) - RootAt(cjTrack.seq, cjTrack.time)).Length() > 1.0f)
        return;
    float clock = scene_game::Ref<float>(cam::Active() + 0x90);
    if (g_check.armed && clock >= g_check.at) {
        g_check.armed = false;
        LOGD("Cutscene %s: CJ's root track said %.2f,%.2f, CJ is at %.2f,%.2f (%.2f m off)", g_cutscene, g_check.cj.x,
             g_check.cj.y, sc.cj.x, sc.cj.y, (g_check.cj - sc.cj).Length2D());
    }
    // a new shot: the spline's next cut is passed (some cuts barely move the camera), or the camera or CJ jumped
    bool onSpline = cam::Mode() == kModeFlyBy && !g_ahead.empty();
    if (onSpline && clock >= g_shotFrom && clock < g_shotTo) return;  // the frames between two keys of a cut
    // CJ gets out in the middle of a shot: he gets out with him. Left for the next cut he sat on in the empty car (BCRAS2, 05.11)
    bool cut = !g_haveSpot || (sc.cam - g_lastCam).Length() > 2.0f || (sc.cj - g_lastCj).Length() > 2.0f ||
               (onSpline && clock >= g_shotTo) || (g_spot.pose == POSE_CAR && CjGotOut(g_spot));
    g_lastCam = sc.cam;
    g_lastCj = sc.cj;
    if (!cut) return;
    float start = onSpline && clock >= g_shotTo ? g_shotTo : g_haveSpot ? clock : 0.0f;  // where this shot begins
    auto t0 = std::chrono::steady_clock::now();
    BuildGrid(sc.cj, sc.floor);  // every shot: props move between shots
    BuildAhead(sc);
    std::fill(std::begin(g_rejects), std::end(g_rejects), 0);
    std::fill(std::begin(g_attractors), std::end(g_attractors), 0);
    g_tried.clear();
    bool keptOut = g_spotOut;
    g_clock = clock;
    Spot spot = Choose(sc);
    // unseen by CJ in a shot about people elsewhere: among them (at CJ's side he stays by CJ, seen or not: the players'
    // choice of 03.11)
    Scene around;
    const Scene* scene = &sc;
    if (spot.pose != POSE_CAR && strncmp(g_how, " (beside", 8) && SeenShare(sc, spot) <= 0.0f && Elsewhere(sc, around)) {
        const char* how = g_how;
        BuildGrid(around.cj, around.floor);
        BuildAhead(around);
        if (ChooseAmong(around, spot)) {
            scene = &around;
        } else {
            g_how = how;
            BuildGrid(sc.cj, sc.floor);
            BuildAhead(sc);
        }
    }
    spot.walk = g_haveSpot && !g_forView && WalkWay(g_spot, spot, *scene);
    g_spot = spot;
    g_spotOut = !strcmp(g_how, " (out of the picture)") || (keptOut && !strcmp(g_how, " (stays)"));
    g_decided = true, g_decidedAt = start;
    g_lastScene = *scene;
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::string kinds;
    for (int t = 0; t < 16; ++t)
        if (g_attractors[t]) kinds += SceneFormat(" %d:%d", t, g_attractors[t]);
    if (!kinds.empty()) LOGD("  ped attractors around (type:count):%s", kinds.c_str());
    g_haveSpot = true;
    ++g_shot;
    float seen = SeenShare(*scene, g_spot);
    g_seenBefore = seen > 0.0f;
    LOGI("Cutscene %s shot %d (%.1f s ahead%s): %s%s%s%s in %.1f ms, seen %.0f%% (rejected: distance %d, crowd %d, frame %d, "
         "covers CJ %d, covers actor %d, solid %d, floor %d, not seen %d, in the way %d, later in the shot %d)",
         g_cutscene, g_shot, g_ahead.empty() ? 0.0f : g_ahead.back().dt, sc.cjInCar ? ", CJ in a car" : "",
         Describe(g_spot), g_how, *g_staysNo ? ", not staying: " : "", g_staysNo, ms, seen * 100.0f, g_rejects[R_DISTANCE],
         g_rejects[R_CROWD], g_rejects[R_FRAME], g_rejects[R_COVERS_CJ], g_rejects[R_COVERS_ACTOR], g_rejects[R_SOLID],
         g_rejects[R_FLOOR], g_rejects[R_HIDDEN], g_rejects[R_FRONT], g_rejects[R_LATER]);
    // the people's root tracks are read from the engine's own data: CJ's is checked against where CJ gets to
    if (!g_ahead.empty() && scene == &sc) {
        const Sample& mid = g_ahead[g_ahead.size() / 2];
        g_check = {mid.cj, clock + mid.dt * 1000.0f, true};
    }
}

bool TakeDecision(Spot& s, float& at) {
    if (!g_decided) return false;
    g_decided = false;
    s = g_spot, at = g_decidedAt;
    return true;
}

void FollowSpot(const Spot& s, float at) { g_follow.push_back({s, at}); }

void Follow() {
    UpdateTalk();
    g_live = CutsceneCj() != nullptr;
    if (!g_live || cam::Mode() != kModeFlyBy) return;
    float clock = scene_game::Ref<float>(cam::Active() + 0x90);
    bool due = false;
    for (; !g_follow.empty() && g_follow.front().at <= clock + 30.0f; g_follow.pop_front()) g_spot = g_follow.front().spot, due = true;
    if (!due) return;
    g_haveSpot = true;
    ++g_shot;
    LOGD("Cutscene shot %d from the host: %s", g_shot, Describe(g_spot));
}

// Before the first drawn frame (Analyse needs one) and the host's word (the partner's game): where the scene's way has him,
// from the frame the scene starts. He came in a moment after the picture, in plain view (06.11).
bool EarlySpot(Spot& out) {
    const char* name = (const char*)kCutsceneName;
    const Places* own = PlacesOf(name);
    if ((own && (own->hidden || !own->spots.empty())) || !MocapWalkCount() || MocapCount() < 2) return false;
    const Way* way = WayOf(name);
    Track tr;
    Walker w;
    if (!way || !TrackOf(CutsceneCj(), tr) || !WayAt(*way, tr.time, w)) return false;
    out = Spot{w.feet, w.heading, POSE_WAY, 0};
    return true;
}

bool CurrentSpot(Spot& out) {
    if (!g_live || !g_haveSpot) return EarlySpot(out);
    out = g_spot;
    return true;
}

int ShotNumber() { return g_shot; }

float ShotLeftMs() {
    if (!g_live || g_ahead.empty() || cam::Mode() != kModeFlyBy) return -1;
    return g_shotFrom - scene_game::Ref<float>(cam::Active() + 0x90);
}

const char* Describe(const Spot& s) {
    static char buf[96];
    static const char* names[] = {"stand", "lean", "sit", "steps", "hidden", "walk", "rides", "way"};
    const Act* a = ActOf(s);
    snprintf(buf, sizeof(buf), "%s %s/%s at %.1f,%.1f,%.1f", names[s.pose], a ? a->block : "-", a && a->anim ? a->anim : "-",
             s.ground.x, s.ground.y, s.ground.z);
    return buf;
}

// One gait for him: a clip drawn at each walk changed it within a scene, and the three brisk ones crept at the way's pace
// (05.11). This one goes at 1.06 m/s by itself, the pace of the ways.
constexpr size_t kGait = 2;
// CJ running in a scene (SMOKE3: CJ and Smoke run, 12.11): the walk at 1.8x still looked like walking. Over kRunFrom m/s the
// game's own run, back to the walk under kRunTo; kRunSpeed is about the pace run_player covers by itself.
constexpr const char* kRunClip = "run_player";
constexpr float kRunFrom = 2.6f, kRunTo = 2.0f, kRunSpeed = 4.5f;

// What he does at a moment of a scene, the players' word of 06.11: instead of the clips, while he stands (animation's time, s)
const SceneAct kSceneActs[] = {
    {"CAT_3", 11.8f, 14.2f, "PED", "handscower", false},          // Catalina at the door: he flinches with CJ
    {"W2_ALT", 0.0f, 1e6f, "COP_AMBIENT", "Coplook_loop", true},  // CJ and Woozie at the game: he watches, his arms crossed
};
const SceneAct* SceneActAt(float t) {
    const char* name = (const char*)kCutsceneName;
    for (const SceneAct& a : kSceneActs)
        if (!strncmp(name, a.scene, 8) && t >= a.from && t < a.to) return &a;
    return nullptr;
}

// How he goes round. A heading following the way's keys started and stopped each turn at once, up to 5 rad/s ("резкие", 06.11):
// it eases in and out now, within `ease` seconds of the way (a critically damped spring), and no faster than a person turns.
constexpr float kMostTurn = 2.5f;   // rad/s
constexpr float kEaseWalk = 0.15f;  // s: on the go he must keep to where he walks
constexpr float kEaseStand = 0.3f;
#include "CScenePartner_Hold.inc"
#include "CScenePartner_Review.inc"
}  // namespace coop::scene
