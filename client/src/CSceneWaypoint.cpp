// Adapted from Karimov-Akbar/sa-coop (MIT), cutscene-partner; see NOTICE.md.
#include "SceneCompat/SceneWaypoint.h"
#include "SceneCompat/SceneGeometry.h"
#include "SceneCompat/Support.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>

namespace coop::scene::waypoint {
using namespace scene_game;
using namespace geometry;
namespace {
constexpr uintptr_t kClumpOffset = 0xB5F878;
}

// Both tables remain embedded in this DLL; moving this function does not change the module handle.
std::string_view Embedded(const wchar_t* name) {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&Embedded, &self);
    HRSRC res = self ? FindResourceW(self, name, (LPCWSTR)RT_RCDATA) : nullptr;
    HGLOBAL data = res ? LoadResource(self, res) : nullptr;
    return data ? std::string_view((const char*)LockResource(data), SizeofResource(self, res)) : std::string_view();
}

// A person of the cutscene moves by its animation's root track (CCutsceneObject::ProcessControl adds the root's
// motion to its position), so the track tells where it will be.
bool TrackOf(scene_game::CEntity* e, Track& tr) {
    auto* clump = e ? (uint8_t*)e->RwObject() : nullptr;
    if (!clump || clump[0] != 2) return false;
    auto* data = scene_game::Ref<uint8_t*>((uintptr_t)clump + scene_game::Ref<uint32_t>(kClumpOffset));  // CAnimBlendClumpData
    auto* link = data ? scene_game::Field<uint8_t*>(data, 0x00) : nullptr;                    // the first association's link (+4)
    auto* hier = link ? scene_game::Field<uint8_t*>(link - 4, 0x14) : nullptr;                 // CAnimBlendAssociation::m_BlendHier
    auto* seqs = hier ? scene_game::Field<uint8_t*>(hier, 0x04) : nullptr;
    if (!seqs || !scene_game::Field<uint16_t>(hier, 0x08) || !(scene_game::Field<uint16_t>(seqs, 0x04) & 2)) return false;  // the root moves
    tr.seq = seqs;
    tr.time = scene_game::Field<float>(link - 4, 0x20);  // m_CurrentTime
    return true;
}
scene_game::CVector RootAt(const uint8_t* seq, float t) {  // the root's translation at t (s); key frames hold time steps
    int frames = scene_game::Field<uint16_t>(seq, 0x06);
    auto* f = scene_game::Field<const uint8_t*>(seq, 0x08);
    bool packed = (scene_game::Field<uint16_t>(seq, 0x04) & 4) != 0;
    scene_game::CVector prev{};
    float at = 0, prevAt = 0;
    for (int i = 0; f && i < frames; ++i) {
        scene_game::CVector p;
        if (packed) {  // KeyFrameTransCompressed: rotation, step (1/60 s), translation (1/1024 m)
            auto* k = (const int16_t*)(f + i * 16);
            at += k[4] / 60.0f;
            p = {k[5] / 1024.0f, k[6] / 1024.0f, k[7] / 1024.0f};
        } else {  // KeyFrameTrans
            auto* k = (const float*)(f + i * 32);
            at += k[4];
            p = {k[5], k[6], k[7]};
        }
        if (at >= t) return i == 0 || at <= prevAt ? p : prev + (p - prev) * ((t - prevAt) / (at - prevAt));
        prev = p;
        prevAt = at;
    }
    return prev;
}
scene_game::CVector FeetLater(const Track& tr, const scene_game::CVector& feet, float dt) {
    if (!tr.seq) return feet;
    scene_game::CVector d = RootAt(tr.seq, tr.time + dt) - RootAt(tr.seq, tr.time);
    return feet + scene_game::CVector{d.x, d.y, 0};
}
// The way a person of the cutscene faces at t (s). CCutsceneObject::ProcessControl only moves its matrix, which faces north
// from first to last, and the root of the animation turns the body. By that matrix "at CJ's side" was 1.5 m along the map's x
// wherever he looked: 37 s in his face in a talk of INTRO1A, 64 of 343 s in front of him in five scenes (05.11). The root's
// rotation takes x to where he faces: over 883 stretches of all the cutscenes it goes the way he walks (0.9 on average).
scene_game::CVector FacingAt(const uint8_t* seq, float t) {
    int frames = scene_game::Field<uint16_t>(seq, 0x06);
    auto* f = scene_game::Field<const uint8_t*>(seq, 0x08);
    bool packed = (scene_game::Field<uint16_t>(seq, 0x04) & 4) != 0;
    float x = 0, y = 0, z = 0, w = 1, at = 0;
    for (int i = 0; f && i < frames; ++i) {
        if (packed) {  // KeyFrameTransCompressed: rotation (1/4096), step (1/60 s)
            auto* k = (const int16_t*)(f + i * 16);
            x = k[0] / 4096.0f, y = k[1] / 4096.0f, z = k[2] / 4096.0f, w = k[3] / 4096.0f;
            at += k[4] / 60.0f;
        } else {  // KeyFrameTrans
            auto* k = (const float*)(f + i * 32);
            x = k[0], y = k[1], z = k[2], w = k[3];
            at += k[4];
        }
        if (at >= t) break;
    }
    return Flat({1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y + w * z), 0.0f});
}
scene_game::CVector CjFacing() {
    Track tr;
    return TrackOf(CutsceneCj(), tr) ? FacingAt(tr.seq, tr.time) : scene_game::CVector{0, 1, 0};
}
bool NextLine(std::string_view& text, char* line, size_t size) {
    if (text.empty()) return false;
    size_t end = std::min(text.find('\n'), text.size());
    size_t n = std::min(end, size - 1);
    memcpy(line, text.data(), n);
    line[n] = 0;
    text.remove_prefix(std::min(end + 1, text.size()));
    return true;
}

// ---- walking with CJ. A shot where CJ walks takes the partner along, a couple of metres behind him and to one
// side, on the path of CJ's own root track (CJ went there: there is floor and room), with CJ's walk from the
// cutscenes. Both games work the path out from their own CJ, so they agree.
struct Path {
    const uint8_t* seq = nullptr;  // CJ's root sequence it was read from
    std::vector<scene_game::CVector> at;       // the root's translation every kPathStep s of the animation
} g_path;

// CJ's root track as samples, read once per cutscene (the key frames hold time steps, as RootAt reads them)
const std::vector<scene_game::CVector>& PathOf(const Track& tr) {
    if (g_path.seq == tr.seq) return g_path.at;
    g_path.seq = tr.seq;
    g_path.at.clear();
    int frames = scene_game::Field<uint16_t>(tr.seq, 0x06);
    auto* f = scene_game::Field<const uint8_t*>(tr.seq, 0x08);
    bool packed = (scene_game::Field<uint16_t>(tr.seq, 0x04) & 4) != 0;
    scene_game::CVector prev{};
    float at = 0, prevAt = 0;
    for (int i = 0; f && i < frames; ++i) {
        scene_game::CVector p;
        if (packed) {
            auto* k = (const int16_t*)(f + i * 16);
            at += k[4] / 60.0f;
            p = {k[5] / 1024.0f, k[6] / 1024.0f, k[7] / 1024.0f};
        } else {
            auto* k = (const float*)(f + i * 32);
            at += k[4];
            p = {k[5], k[6], k[7]};
        }
        if (i == 0) prev = p, prevAt = at;
        for (float t; (t = (float)g_path.at.size() * kPathStep) <= at;)
            g_path.at.push_back(at > prevAt ? prev + (p - prev) * std::clamp((t - prevAt) / (at - prevAt), 0.0f, 1.0f) : p);
        prev = p, prevAt = at;
    }
    return g_path.at;
}

// CJ's feet at the animation's time t, from where they are at its time now (the track is relative)
scene_game::CVector CjAt(const std::vector<scene_game::CVector>& path, float now, const scene_game::CVector& feetNow, float t) {
    auto root = [&](float s) {
        float x = std::clamp(s / kPathStep, 0.0f, (float)(path.size() - 1));
        size_t i = (size_t)x;
        return i + 1 < path.size() ? path[i] + (path[i + 1] - path[i]) * (x - (float)i) : path[i];
    };
    return feetNow + (root(t) - root(now));
}
scene_game::CVector CjFeet(scene_game::CEntity* cj) {  // as BuildScene finds the floor under CJ
    scene_game::CVector p = cj->GetPosition();
    float z;
    p.z = Floor(p.x, p.y, p.z + 0.2f, z) && p.z - z < 2.0f ? z : p.z - 1.0f;
    return p;
}

// The way CJ walks off from his track's time s: along his first half metre, his forward if he never goes that far. What
// he has come so far is no way yet: a few centimetres of shifting his weight point anywhere, and turning on the spot swings
// his forward. Behind him by either, the partner went round him at 5 m/s (RYDER2A, 04.11).
scene_game::CVector WalkOff(const std::vector<scene_game::CVector>& path, float now, const scene_game::CVector& feetNow, const scene_game::CVector& cjFwd, float s) {
    scene_game::CVector from = CjAt(path, now, feetNow, s), last = from;
    for (float u = s + kPathStep, end = (float)(path.size() - 1) * kPathStep; u <= end; u += kPathStep) {
        scene_game::CVector q = CjAt(path, now, feetNow, u);
        if ((q - last).Length2D() > kPathJump) break;
        if ((q - from).Length2D() >= 0.5f) return Flat(q - from);
        last = q;
    }
    return Flat(cjFwd);
}

// The partner walking with CJ at the animation's time t: kWalkGap back along CJ's path from where CJ is then
// (behind where CJ started, if CJ has not come that far), kWalkSide to one side (-1 left, 1 right, 0 right behind).
scene_game::CVector WalkPoint(const std::vector<scene_game::CVector>& path, float now, const scene_game::CVector& feetNow, const scene_game::CVector& cjFwd, float t, int side) {
    scene_game::CVector cj = CjAt(path, now, feetNow, t), p = cj;
    float left = kWalkGap, s = t;
    for (int k = 0; k < 600 && left > 0 && s > 0; ++k) {  // 30 s back at most
        s = std::max(0.0f, s - kPathStep);
        scene_game::CVector q = CjAt(path, now, feetNow, s);
        float d = (q - p).Length2D();
        if (d > kPathJump) break;  // CJ came here by a cut: behind his start here
        if (d >= left) p = p + (q - p) * (left / d), left = 0;
        else p = q, left -= d;
    }
    scene_game::CVector way = left > 0.0f || (cj - p).Length2D() < 0.05f ? WalkOff(path, now, feetNow, cjFwd, s) : Flat(cj - p);
    p = p - way * left;
    return p + scene_game::CVector{way.y, -way.x, 0} * ((float)side * kWalkSide);
}
Walker WalkerAt(const std::vector<scene_game::CVector>& path, float now, const scene_game::CVector& feetNow, const scene_game::CVector& cjFwd, float t, int side) {
    Walker w;
    scene_game::CVector a = WalkPoint(path, now, feetNow, cjFwd, t, side), way = a - WalkPoint(path, now, feetNow, cjFwd, t - 0.15f, side);
    if (way.Length2D() > kPathJump) way = WalkPoint(path, now, feetNow, cjFwd, t + 0.15f, side) - a;  // a cut just before
    w.feet = a;
    w.speed = way.Length2D() / 0.15f;
    w.heading = HeadingOf(w.speed > 0.3f ? Flat(way) : Flat(CjAt(path, now, feetNow, t) - a));  // standing: to CJ
    return w;
}

// The way of a whole scene, worked out ahead. Chosen at each cut from what is drawn around CJ just then, the partner knew no
// map and no more than the shot: he walked 8 m in a 2 s shot, stood 10 m off for three shots, and his first place of
// INTRO1A hung 0.4 m over the floor ("в воздухе оказался", 05.11). The game's own files hold it all: the place (what the IPLs
// put there), CJ's and everybody's tracks, the camera. tests: cspath.py works out from them where he stands, when he goes
// and which way round the furniture, starting and stopping gently at an easy pace, and writes it to cutscene_ways.txt (in coop.asi).
// All the games read the same text by their own CJ's clock, so nothing of it goes over the network.
const Way* WayOf(const char* cutscene, int rank) {
    static std::map<std::string, Way> table;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        std::string_view text = Embedded(L"CUTSCENE_WAYS");
        char line[128], name[16];
        Way* way = nullptr;
        while (NextLine(text, line, sizeof line)) {
            WayKey k;
            if (isalpha((uint8_t)line[0]) && sscanf(line, "%15s", name) == 1) way = &table[name];
            else if (way && sscanf(line, "%f %f %f %f %f %d", &k.t, &k.at.x, &k.at.y, &k.at.z, &k.heading, &k.mark) == 6) way->push_back(k);
        }
        LOGI("Cutscene ways: %zu cutscenes", table.size());
    }
    std::string key(cutscene, strnlen(cutscene, 8));
    for (char& c : key) c = (char)toupper((uint8_t)c);
    if (rank) key += SceneFormat("#%d", rank);  // the next partners' ways, worked out around the first one's
    auto it = table.find(key);
    return it == table.end() || it->second.size() < 2 ? nullptr : &it->second;
}
// where the way has him at the animation's time t; false: it knows nothing of that moment
bool WayAt(const Way& way, float t, Walker& w) {
    auto next = std::upper_bound(way.begin(), way.end(), t, [](float v, const WayKey& k) { return v < k.t; });
    if (next == way.begin()) return false;
    const WayKey& a = *(next - 1);
    if (a.mark == 2) return false;
    w.feet = a.at, w.heading = a.heading, w.speed = 0;
    if (next == way.end() || next->mark) return true;
    float span = std::max(next->t - a.t, 0.01f), f = std::clamp((t - a.t) / span, 0.0f, 1.0f);
    w.feet = a.at + (next->at - a.at) * f;
    float turn = std::remainder(next->heading - a.heading, 6.2831853f);
    w.heading = a.heading + turn * f;
    w.speed = (next->at - a.at).Length2D() / span;
    if (w.speed < 0.05f && std::fabs(turn) / span > 0.3f) w.turning = turn > 0.0f ? 1 : -1;
    return true;
}

}  // namespace coop::scene::waypoint
