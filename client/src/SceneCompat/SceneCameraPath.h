// Adapted from Karimov-Akbar/sa-coop (MIT), cutscene-partner; see NOTICE.md.
#pragma once
#include "SceneCompat/Game.h"

namespace coop::scene::camera_path {

constexpr uint16_t kModeFlyBy = 17;

struct Spline {
    const float* d = nullptr;
    int n = 0, dims = 0, stride = 0;
    bool Load(uintptr_t at, int dimensions);
    const float* Key(int k) const;
    float Time(int k) const;
    void At(float t, float* out) const;
    bool NextCut(float t, float jump, float& from, float& to) const;
};

struct View {
    scene_game::CVector pos, fwd, right, up;
    float kx = 0, ky = 0, cx = 0, cy = 0;
};

struct CameraPath {
    Spline fov, roll, pos, target;
    bool Load();
    void At(float t, View& v, float& f) const;
};

bool ToScreen(const View& v, const scene_game::CVector& p, float& sx, float& sy, float& depth);
bool Calibrate(View& v);

}  // namespace coop::scene::camera_path
