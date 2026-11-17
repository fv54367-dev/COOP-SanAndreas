// Adapted from Karimov-Akbar/sa-coop (MIT), cutscene-partner; see NOTICE.md.
#pragma once
#include "SceneCompat/ScenePartner.h"
#include <cstddef>

namespace coop::scene::fidget {

struct Fidget {
    const char* block;
    const char* anim;
    bool head;  // it moves the head itself: the look at the people of the scene lets go meanwhile
};

struct Act {
    const char* block;
    const char* anim;  // none: the clips of CJ listening in the cutscenes, one after another (SceneMocap)
    bool cigarette;
    float back;  // how far the animation's root track moves the body behind the ped's position
    const Fidget* fidgets = nullptr;
    size_t fidgetCount = 0;
};

enum Face { FACE_CURIOUS = 2, FACE_CHEWING = 8 };  // eFacialExpression

struct SceneAct {
    const char* scene;
    float from, to;
    const char* block;
    const char* anim;
    bool loop;
};

struct Held {
    int handle = -1;
    Spot spot;
    bool placed = false;
    int stage = 0;  // 0 loading the animation, 1 the cigarette is in the hand, 2 the loop plays, 3 a variation plays
    int prop = -1;  // the cigarette
    uint64_t checkedAt = 0;
    // the living extra: its draws come from the spot and the cutscene's clock, so both games show the same
    uint32_t seed = 0;
    uint32_t slot = ~0u, faceSlot = ~0u, turns = 0;
    float fidgetAt = -1, faceAt = -1;  // the cutscene clock (ms) when the drawn variation, the gum, begin
    float loopClock = -1;              // and when the loop first played at this spot
    uint8_t pick = 0;
    const Fidget* fidget = nullptr;  // the variation playing
    const char* clip = nullptr;      // the cutscene clip playing, and how many came before it at this spot
    uint32_t clips = 0;
    const SceneAct* act = nullptr;   // the scene's own act instead of the clips (kSceneActs)
    bool walking = false;            // walking with CJ: the walk plays (else the clips)
    bool running = false;            // ...and CJ runs: the game's run instead of the walk (kRunClip)
    int turning = 0;                 // turning on the spot: the game's small steps play (Walker::turning)
    const char* walk = nullptr;
    bool going = false;              // on foot to the spot (Spot::walk), by the cutscene's clock
    float lit = -1;                  // its own game::CPhysical::m_fContactSurfaceBrightness before the scene
    scene_game::CVector goFrom, goTo, goVia;  // goVia: the turn of a way round something (goViaSet)
    bool goViaSet = false;
    float goStart = 0, goMs = 0;
    float walkSpeed = 1, heading = 0, turnRate = 0;  // turnRate: rad/s his heading goes round at now
    uint64_t speedAt = 0;
    float pace = 0;                  // the way's speed, eased (cutscene-partner-smooth)
    uint64_t gaitAt = 0;             // when the walk / run last changed
    bool fresh = false;              // just put here: the first clip comes in fast
    uint64_t fidgetSince = 0, lookedAt = 0, talkSeenAt = 0;
    int weapon = -1;                 // what it held as the scene took it (-1: not looked yet)
    scene_game::CEntity* looking = nullptr;
    scene_game::CEntity* lastTalker = nullptr;
    bool lookOn = false, wasTalking = false;
};

constexpr float kClipBlend = 0.8f;
bool BlockReady(const char* name);
void StopLooking(scene_game::CPed* ped);
void SetFace(scene_game::CPed* ped, Face f, int ms);
uint32_t Mix(uint32_t a, uint32_t b);
void UpdateTalk();
void ClearTalk();
scene_game::CEntity* Attention(Held& h, scene_game::CPed* ped, uint64_t now, float clock);
void StartFidget(Held& h, const Fidget& f, uint64_t now);
void NextClip(Held& h, float blend);
const char* AnimNow(const Held& h, const Act* a);
void Live(scene_game::CPed* ped, Held& h, const Act* a);

}  // namespace coop::scene::fidget
