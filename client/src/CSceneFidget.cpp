// Adapted from Karimov-Akbar/sa-coop (MIT), cutscene-partner; see NOTICE.md.
#include "SceneCompat/SceneFidget.h"
#include "SceneCompat/SceneGeometry.h"
#include "SceneCompat/SceneCameraPath.h"
#include "SceneCompat/SceneMocap.h"
#include "SceneCompat/Anim.h"
#include "SceneCompat/Script.h"
#include "SceneCompat/Support.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

namespace coop::scene::fidget {
using namespace scene_game;
using scene_game::script::Cmd;
using namespace geometry;
using namespace camera_path;

// The IFP block of an act, requested when it is not in memory yet (PED is always there).
bool BlockReady(const char* name) {
    if (!strcmp(name, "PED")) return true;
    if (!strcmp(name, kMocapBlock)) return MocapCount() > 0;  // ours: made in memory, never streamed
    int i = scene_game::Call<0x4D3990, int>(name);  // CAnimManager::GetAnimationBlockIndex
    if (i < 0) return false;
    if (anim::BlockLoaded(i)) return true;
    RequestModel(anim::kIfpBase + i, 2);
    return false;
}

// ---- scene animation owns the head; the ped's facial task still moves the face
constexpr uintptr_t kIkChainMan = 0xC15448;  // g_ikChainMan
void StopLooking(scene_game::CPed* ped) {
    if (scene_game::CallMethod<0x6181A0, bool>((void*)kIkChainMan, ped)) scene_game::CallMethod<0x618280, void>((void*)kIkChainMan, ped, 0u);  // IsLooking, AbortLookAt
}

void SetFace(scene_game::CPed* ped, Face f, int ms) {          // CTaskComplexFacial::SetRequest on the ped's facial task
    void* intel = ped->Intelligence();
    void* task = intel ? scene_game::Field<void*>(intel, 0x04 + 0x14 + 3 * 4) : nullptr;  // CTaskManager::m_aSecondaryTasks[FACIAL_COMPLEX]
    int type = task ? scene_game::CallVirtual<4, int>(task) : -1;
    if (type == 305) scene_game::CallMethod<0x691230, void>(task, (int)f, ms, -1, 0);  // TASK_COMPLEX_FACIAL
    LOGD("Cutscene partner: face %d for %d ms%s", (int)f, ms, type == 305 ? "" : SceneFormat(" refused (facial task %d)", type).c_str());
}
uint32_t Mix(uint32_t a, uint32_t b) {  // a hash: the same draws in both games
    uint32_t h = a ^ (b * 0x9E3779B9u);
    h ^= h >> 16, h *= 0x85EBCA6Bu, h ^= h >> 13, h *= 0xC2B2AE35u, h ^= h >> 16;
    return h;
}

// ---- who of the scene talks: the jaw moves against the head (bone 5) while a line is said. Cutscene faces have a
// rig of their own (bones 5001-5022, "jaw1" is 5015), other people's jaw is bone 8.
namespace {
constexpr float kTalking = 0.05f;  // jaw movement (axes, fading over a third of a second): 0.1-0.5 saying a line, ~0 silent
struct Talk {
    float jaw[6] = {};   // the jaw's up and forward axes in the head's frame, last frame
    float activity = 0;  // how much it moved lately: fades in a third of a second
    scene_game::CVector head;        // where the head is (world)
    bool primed = false, seen = false;
};
std::map<scene_game::CEntity*, Talk> g_talk;
uint64_t g_talkAt = 0;

}

bool HeadAndJaw(scene_game::CEntity* e, scene_game::CVector& head, float jaw[6]) {
    auto* clump = (uint8_t*)e->RwObject();
    void* hier = clump && clump[0] == 2 ? scene_game::Call<0x734A40, void*>(clump) : nullptr;  // GetAnimHierarchyFromSkinClump
    auto* m = hier ? scene_game::Call<0x7C5120, uint8_t*>(hier) : nullptr;                     // RpHAnimHierarchyGetMatrixArray
    int h = m ? scene_game::Call<0x7C51A0, int>(hier, 5) : -1, j = m ? scene_game::Call<0x7C51A0, int>(hier, 5015) : -1;  // RpHAnimIDGetIndex
    if (h < 0) return false;
    if (j < 0) j = scene_game::Call<0x7C51A0, int>(hier, 8);
    auto* H = (const float*)(m + (size_t)h * 0x40);  // RwMatrix rows: right, up, at, pos
    head = {H[12], H[13], H[14]};
    std::fill(jaw, jaw + 6, 0.0f);
    if (j < 0) return true;  // a face that never talks
    auto* J = (const float*)(m + (size_t)j * 0x40);
    for (int a = 0; a < 2; ++a)
        for (int b = 0; b < 3; ++b)
            jaw[a * 3 + b] = J[(1 + a) * 4] * H[b * 4] + J[(1 + a) * 4 + 1] * H[b * 4 + 1] + J[(1 + a) * 4 + 2] * H[b * 4 + 2];
    return true;
}

// every drawn frame: the bones are where the frame shows them
void UpdateTalk() {
    uint64_t now = NowMs();
    float keep = std::exp(-std::clamp((float)(now - g_talkAt), 1.0f, 200.0f) / 350.0f);
    g_talkAt = now;
    for (auto& [e, t] : g_talk) t.seen = false;
    for (int i = 0; i < NumCutsceneObjects(); ++i) {
        scene_game::CEntity* o = CutsceneObject(i);
        float jaw[6];
        scene_game::CVector head;
        if (!o || !(o->Flags() & 0x80) || !IsPerson(o) || !HeadAndJaw(o, head, jaw)) continue;
        Talk& t = g_talk[o];
        if (!t.primed) LOGD("Cutscene person model %d: %s", o->ModelIndex(), std::count(jaw, jaw + 6, 0.0f) == 6 ? "no jaw" : "jaw");
        float moved = 0;
        for (int k = 0; k < 6; ++k) moved += std::fabs(jaw[k] - t.jaw[k]);
        t.activity = t.activity * keep + (t.primed ? std::min(moved, 0.1f) : 0.0f);  // a cut makes the jaw jump
        std::copy(jaw, jaw + 6, t.jaw);
        t.head = head;
        t.primed = t.seen = true;
    }
    for (auto it = g_talk.begin(); it != g_talk.end();) it = it->second.seen ? std::next(it) : g_talk.erase(it);
}

// ---- peds held in the scene




// Whom the partner looks at: who talks in front of it, and a while after the line; otherwise CJ or the nearest one
// in front. Now and then a glance at someone else.
scene_game::CEntity* Attention(Held& h, scene_game::CPed* ped, uint64_t now, float clock) {
    scene_game::CVector me = ped->GetPosition(), face = Forward(ped->CurrentRotation());
    std::vector<scene_game::CEntity*> front;
    for (auto& [e, t] : g_talk) {
        scene_game::CVector d = t.head - me;
        float l = d.Length2D();
        if (l > 0.5f && l < 15.0f && (d.x * face.x + d.y * face.y) > -0.2f * l) front.push_back(e);  // ~100 degrees
    }
    if (front.empty()) return nullptr;
    std::sort(front.begin(), front.end(), [](scene_game::CEntity* a, scene_game::CEntity* b) {  // the same order in both games
        return a->ModelIndex() != b->ModelIndex() ? a->ModelIndex() < b->ModelIndex() : g_talk[a].head.x < g_talk[b].head.x;
    });
    scene_game::CEntity* target = nullptr;
    float best = kTalking;
    for (scene_game::CEntity* e : front)
        if (g_talk[e].activity > best) target = e, best = g_talk[e].activity;
    // the one listened to keeps the look while still talking, unless someone else is clearly louder
    bool held = h.lastTalker && h.lastTalker != target && std::count(front.begin(), front.end(), h.lastTalker);
    if (held && g_talk[h.lastTalker].activity > kTalking * 0.6f && best < g_talk[h.lastTalker].activity * 1.5f) target = h.lastTalker;
    if (target) {
        h.lastTalker = target, h.talkSeenAt = now;
    } else if (h.lastTalker && now - h.talkSeenAt < 2500 && std::count(front.begin(), front.end(), h.lastTalker)) {
        target = h.lastTalker;
    } else if (scene_game::CEntity* cj = CutsceneCj(); cj && std::count(front.begin(), front.end(), cj)) {
        target = cj;
    } else {
        for (scene_game::CEntity* e : front)
            if (!target || (g_talk[e].head - me).Length2D() < (g_talk[target].head - me).Length2D()) target = e;
    }
    if (clock >= 0 && front.size() > 1) {  // a glance of a second at someone else, in some of every 6 s
        uint32_t slot = (uint32_t)(clock / 6000.0f), r = Mix(h.seed ^ 0x2545F491u, slot);
        float at = slot * 6000.0f + 1500.0f + (float)((r >> 8) % 3000);
        if (r % 100 < 30 && clock >= at && clock < at + 1200.0f) {
            scene_game::CEntity* other = front[(r >> 20) % front.size()];
            if (other != target) return other;
        }
    }
    return target;
}

void StartFidget(Held& h, const Fidget& f, uint64_t now) {
    if (!BlockReady(f.block)) return;
    Cmd(0x0605).I(h.handle).S(f.anim).S(f.block).F(4.0f).I(0).I(0).I(0).I(0).I(-1).Run();  // TASK_PLAY_ANIM, once
    h.stage = 3, h.fidget = &f, h.fidgetSince = now;
    LOGD("Cutscene partner: %s/%s", f.block, f.anim);
}

// The clips start and end in poses of their own: a quarter of a second between them was a jerk every 5 s (06.11)

// the clips act: the next one drawn from the spot (the same in both games), never the one that ends
void NextClip(Held& h, float blend) {
    size_t n = MocapCount();
    uint32_t r = Mix(h.seed, ++h.clips);
    const char* c = MocapClip(r % n);
    if (c == h.clip) c = MocapClip(r % n + 1);
    h.clip = c;
    // TASK_PLAY_ANIM, once, holding its last frame until the next one takes over
    Cmd(0x0605).I(h.handle).S(c).S(kMocapBlock).F(blend).I(0).I(0).I(0).I(1).I(-1).Run();
    h.checkedAt = NowMs();
    LOGD("Cutscene partner: clip %s", c);
}
// what the act plays now
const char* AnimNow(const Held& h, const Act* a) { return a->anim ? a->anim : h.clip ? h.clip : "-"; }

// Every frame while the loop or a variation plays: now and then a variation (the same moments in both games: the
// cutscene's clock), a nod when a line ends, the head to the people of the scene, a stick of gum.
void Live(scene_game::CPed* ped, Held& h, const Act* a) {
    uint64_t now = NowMs();
    float clock = cam::Mode() == kModeFlyBy ? scene_game::Ref<float>(cam::Active() + 0x90) : -1.0f;
    bool talking = h.lastTalker && g_talk.count(h.lastTalker) && g_talk[h.lastTalker].activity > kTalking;
    bool settled = h.loopClock >= 0 && clock > h.loopClock + 1500.0f;
    if (h.wasTalking && !talking && h.stage == 2 && settled && Mix(h.seed, ++h.turns) % 100 < 40)  // the line listened to ends
        for (size_t i = 0; i < a->fidgetCount; ++i)
            if (!strcmp(a->fidgets[i].anim, "Coplook_nod")) {
                StartFidget(h, a->fidgets[i], now);
                break;
            }
    h.wasTalking = talking;
    if (!a->anim) {  // the next clip blends in over the end of this one
        void* as = h.stage == 2 && h.clip && !h.walking && !h.turning && !h.act ? scene_game::Call<0x4D6870, void*>(ped->RwObject(), h.clip) : nullptr;  // RpAnimBlendClumpGetAssociation
        if (as && anim::CurrentTime(as) >= anim::TotalTime(anim::Hierarchy(as)) - kClipBlend) NextClip(h, 1.0f / kClipBlend);
    } else if (h.stage == 3) {  // back to the loop just before the variation ends
        void* as = scene_game::Call<0x4D6870, void*>(ped->RwObject(), h.fidget->anim);  // RpAnimBlendClumpGetAssociation
        if (as ? anim::CurrentTime(as) >= anim::TotalTime(anim::Hierarchy(as)) - 0.3f : now - h.fidgetSince > 500) {
            Cmd(0x0605).I(h.handle).S(a->anim).S(a->block).F(4.0f).I(1).I(0).I(0).I(0).I(-1).Run();  // TASK_PLAY_ANIM, looped
            h.stage = 2, h.checkedAt = now, h.fidget = nullptr;
        }
    } else if (h.stage == 2 && a->fidgetCount && clock >= 0) {
        float phase = (float)(h.seed % 6000);
        uint32_t slot = (uint32_t)((clock + phase) / 6000.0f);
        if (slot != h.slot) {  // in seven of ten stretches of 6 s, one variation
            uint32_t r = Mix(h.seed, slot);
            h.slot = slot;
            h.fidgetAt = r % 100 < 70 ? slot * 6000.0f - phase + 300.0f + (float)((r >> 8) % 3000) : -1.0f;
            h.pick = (uint8_t)((r >> 20) % a->fidgetCount);
        }
        if (h.loopClock < 0) h.loopClock = clock;
        if (h.fidgetAt >= 0 && clock >= h.fidgetAt) {
            bool afterLoop = h.fidgetAt >= h.loopClock + 1500.0f;  // the loop's pose first, a moment after a move
            h.fidgetAt = -1;
            if (afterLoop) StartFidget(h, a->fidgets[h.pick], now);
        }
    }
    if (clock >= 0) {  // gum now and then: in a third of every 15 s
        uint32_t slot = (uint32_t)(clock / 15000.0f);
        if (slot != h.faceSlot) {
            uint32_t r = Mix(h.seed ^ 0x5F3759DFu, slot);
            h.faceSlot = slot;
            h.faceAt = r % 100 < 35 ? slot * 15000.0f + 2000.0f + (float)((r >> 8) % 8000) : -1.0f;
        }
        if (h.faceAt >= 0 && clock >= h.faceAt) h.faceAt = -1, SetFace(ped, FACE_CHEWING, 3500);
    }
}


void ClearTalk() { g_talk.clear(); }

}  // namespace coop::scene::fidget
