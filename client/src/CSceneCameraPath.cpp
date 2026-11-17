// Adapted from Karimov-Akbar/sa-coop (MIT), cutscene-partner; see NOTICE.md.
#include "SceneCompat/SceneCameraPath.h"
#include <algorithm>
#include <cmath>

namespace coop::scene::camera_path {
using namespace scene_game;
namespace {
constexpr uintptr_t kPathFov = 0xB6F988, kPathRoll = 0xB6F98C, kPathPos = 0xB6F990, kPathTarget = 0xB6F994;
constexpr float kCutKeys = 32.0f;  // keys closer than this (ms) are a cut (0x8D0F80)
}

bool Spline::Load(uintptr_t at, int dimensions) {
    d = scene_game::Ref<const float*>(at);
    dims = dimensions;
    stride = 1 + dims * 3;
    n = d ? (int)d[0] : 0;
    return n >= 2 && n < 4000;
}
const float* Spline::Key(int k) const { return d + 1 + k * stride; }
float Spline::Time(int k) const { return (Key(k)[0] - d[1]) * 1000.0f; }

// Cubic Bezier, or a straight line when out tangents lie on the values (0x5B2090).
void Spline::At(float t, float* out) const {
    int k = 0;
    while (k < n - 2 && Time(k + 1) <= t) ++k;
    if (k < n - 2 && Time(k + 1) - Time(k) <= kCutKeys) ++k;  // a cut: the camera is on the next key at once
    const float *a = Key(k), *b = Key(k + 1);
    float span = Time(k + 1) - Time(k);
    float u = span > 0 ? std::clamp((t - Time(k)) / span, 0.0f, 1.0f) : 1.0f, v = 1 - u;
    bool line = true;  // the key's out tangents on its values in every dimension (0x5B2090, 1D 0x5B2330)
    for (int i = 0; i < dims; ++i) line = line && a[1 + 2 * dims + i] == a[1 + i];
    for (int i = 0; i < dims; ++i) {
        float p0 = a[1 + i], c0 = a[1 + 2 * dims + i], c1 = b[1 + dims + i], p1 = b[1 + i];
        out[i] = line ? p0 + (p1 - p0) * u : v * v * v * p0 + 3 * v * v * u * c0 + 3 * v * u * u * c1 + u * u * u * p1;
    }
}

bool Spline::NextCut(float t, float jump, float& from, float& to) const {
    for (int k = 1; k < n; ++k) {
        if (Time(k) <= t || Time(k) - Time(k - 1) > kCutKeys) continue;
        float d2 = 0;
        for (int i = 1; i <= dims; ++i) d2 += (Key(k)[i] - Key(k - 1)[i]) * (Key(k)[i] - Key(k - 1)[i]);
        if (d2 > jump * jump) {
            from = Time(k - 1), to = Time(k);
            return true;
        }
    }
    return false;
}

bool ToScreen(const View& v, const scene_game::CVector& p, float& sx, float& sy, float& depth) {
    scene_game::CVector d = p - v.pos;
    depth = d.Dot(v.fwd);
    if (depth < 0.3f) return false;
    sx = v.cx + d.Dot(v.right) / depth * v.kx;
    sy = v.cy - d.Dot(v.up) / depth * v.ky;
    return true;
}

bool CameraPath::Load() { return fov.Load(kPathFov, 1) && roll.Load(kPathRoll, 1) && pos.Load(kPathPos, 3) && target.Load(kPathTarget, 3); }
void CameraPath::At(float t, View& v, float& f) const {
    float p[3], q[3], r = 0;
    pos.At(t, p);
    target.At(t, q);
    fov.At(t, &f);
    roll.At(t, &r);
    // the keys are relative to the camera's copy of the cutscene's offset: the cutscene's own one is lifted by
    // 1 m once the scene is set up (CCutsceneMgr::SetupCutsceneToStart)
    scene_game::CVector o = scene_game::Ref<scene_game::CVector>(addr::TheCamera + 0x884);  // CCamera::m_vecCutSceneOffset
    v.pos = o + scene_game::CVector{p[0], p[1], p[2]};
    v.fwd = (scene_game::CVector{q[0], q[1], q[2]} - scene_game::CVector{p[0], p[1], p[2]}).Normalized();
    float a = r * 0.0174533f + 1.5707963f;  // the roll tilts the up vector
    scene_game::CVector left = scene_game::CVector{std::cos(a), 0, std::sin(a)}.Cross(v.fwd).Normalized();
    v.up = v.fwd.Cross(left);
    v.right = left * -1.0f;
}

bool Calibrate(View& v) {
    float x0, y0, d0, x1, y1, d1, x2, y2, d2;
    scene_game::CVector c = v.pos + v.fwd * 10.0f;
    if (!WorldToScreen(c, x0, y0, d0) || !WorldToScreen(c + v.right, x1, y1, d1) || !WorldToScreen(c + v.up, x2, y2, d2))
        return false;
    v.cx = x0, v.cy = y0, v.kx = (x1 - x0) * 10.0f, v.ky = (y0 - y2) * 10.0f;
    return std::fabs(v.kx) > 1.0f && std::fabs(v.ky) > 1.0f;
}

}  // namespace coop::scene::camera_path
