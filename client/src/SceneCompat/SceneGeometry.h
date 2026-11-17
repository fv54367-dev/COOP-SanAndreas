// Adapted from Karimov-Akbar/sa-coop (MIT), cutscene-partner; see NOTICE.md.
#pragma once
#include "SceneCompat/Game.h"

namespace coop::scene::geometry {

bool Seen(const scene_game::CVector& a, const scene_game::CVector& b);
bool Floor(float x, float y, float fromZ, float& z);
bool Free(const scene_game::CVector& c, float r);
scene_game::CVector Flat(scene_game::CVector v);
float HeadingOf(const scene_game::CVector& dir);
scene_game::CVector Forward(float heading);
scene_game::CVector Turn(const scene_game::CVector& v, float a);
scene_game::CVector Transform(scene_game::CEntity* e, const scene_game::CVector& v, bool point);
scene_game::CEntity* CutsceneObject(int i);
int NumCutsceneObjects();
scene_game::CEntity* CutsceneCj();
bool IsPerson(scene_game::CEntity* e);

using TriangleFn = void (*)(const scene_game::CVector&, const scene_game::CVector&, const scene_game::CVector&, void*);
void ForEachTriangle(scene_game::CEntity* e, TriangleFn fn, void* user);

struct Bounds {
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
};
bool IsCar(scene_game::CEntity* e, Bounds& b);

constexpr float kCell = 0.1f, kBumpLow = 0.15f, kBumpHigh = 1.95f;
constexpr int kCells = 240;  // 24 m
struct Grid {
    float x0 = 0, y0 = 0, floor = -1000;
    uint8_t top[kCells * kCells];   // 2 cm steps, 0 nothing
    uint8_t seat[kCells * kCells];  // 1 cm steps
    uint8_t ground[kCells * kCells];
    uint8_t faces[kCells * kCells];  // which way the walls there are seen from: 8 directions, all for furniture tops

    bool Index(float x, float y, int& i) const {
        int cx = (int)std::floor((x - x0) / kCell), cy = (int)std::floor((y - y0) / kCell);
        if (cx < 0 || cy < 0 || cx >= kCells || cy >= kCells) return false;
        i = cy * kCells + cx;
        return true;
    }
    float Top(float x, float y) const {
        int i;
        return Index(x, y, i) ? top[i] * 0.02f : 0.0f;
    }
    float Seat(float x, float y) const {
        int i;
        return Index(x, y, i) ? seat[i] * 0.01f : 0.0f;
    }
    bool Ground(float x, float y) const {
        int i;
        return Index(x, y, i) && ground[i];
    }
    bool Clear(const scene_game::CVector& p, float r) const {  // nothing to bump into within r of p
        for (float dy = -r; dy <= r + 1e-3f; dy += kCell)
            for (float dx = -r; dx <= r + 1e-3f; dx += kCell)
                if (dx * dx + dy * dy <= r * r + 1e-4f && Top(p.x + dx, p.y + dy) > 0) return false;
        return true;
    }
    float Hit(const scene_game::CVector& p, const scene_game::CVector& dir, float range, float height) const {
        for (int k = 1; k * 0.05f <= range + 1e-3f; ++k)
            if (Top(p.x + dir.x * k * 0.05f, p.y + dir.y * k * 0.05f) >= height) return k * 0.05f;
        return -1;
    }
    void Add(const scene_game::CVector& a, const scene_game::CVector& b, const scene_game::CVector& c);
};
extern Grid g_grid;  // one instance, owned by CSceneGeometry.cpp

float SegDist2(float px, float py, const scene_game::CVector& a, const scene_game::CVector& b);
void BuildGrid(const scene_game::CVector& cj, float floor);
bool GridSeen(const scene_game::CVector& from, const scene_game::CVector& to);
void BuildReach(const scene_game::CVector& from, int margin = 2);
bool Reachable(const scene_game::CVector& p);

}  // namespace coop::scene::geometry
