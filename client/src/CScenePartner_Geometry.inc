// where the spot's animation puts the body: leaning and sitting move it back from the ped's position
scene_game::CVector BodyOf(const Spot& s) {
    const Act* a = ActOf(s);
    return a ? s.ground - Forward(s.heading) * a->back : s.ground;
}

// ---- one shot of the cutscene
struct Body {  // a person on the screen
    float x, top, bottom, half, depth;
};

bool Project(const scene_game::CVector& feet, float height, Body& b) {
    float hx, hy, hd, fx, fy, fd;
    if (!WorldToScreen(feet + scene_game::CVector{0, 0, height}, hx, hy, hd) || !WorldToScreen(feet + scene_game::CVector{0, 0, 0.05f}, fx, fy, fd))
        return false;
    b.x = (hx + fx) * 0.5f;
    b.top = hy;
    b.bottom = fy;
    b.half = std::fabs(fy - hy) * 0.2f;  // a person with the arms out is about 40% as wide as tall
    b.depth = std::min(hd, fd);
    return true;
}

// Someone of the scene: close to the camera the feet (or even the head) fall outside the projection, yet the
// body still covers a large part of the picture.
bool ProjectActor(const scene_game::CVector& feet, Body& b) {
    float hx, hy, hd, cx, cy, cd, fx, fy, fd;
    bool head = WorldToScreen(feet + scene_game::CVector{0, 0, 1.75f}, hx, hy, hd);
    bool chest = WorldToScreen(feet + scene_game::CVector{0, 0, 1.1f}, cx, cy, cd);
    bool foot = WorldToScreen(feet + scene_game::CVector{0, 0, 0.05f}, fx, fy, fd);
    if (!head && !chest) return false;
    float w = (float)ScreenWidth(), focal = w * 0.71f;  // about a 70 degree view
    float depth = chest ? cd : hd;
    b.x = chest ? cx : hx;
    b.top = head ? hy : -1e6f;
    b.bottom = foot ? fy : 1e6f;
    b.half = 0.3f * focal / std::max(depth, 0.3f);  // half a shoulder width
    b.depth = depth;
    return true;
}

bool ToScreenNow(const scene_game::CVector& p, float& sx, float& sy, float& depth);

// The screen box around the skeleton of a cutscene actor: every bone counts, so arms out or a bent back do too
// (and a head right in front of the lens: the engine's own projection drops what is nearer than 1 m).
bool ProjectSkeleton(scene_game::CEntity* e, Body& b) {
    auto* clump = (uint8_t*)e->RwObject();
    if (!clump || clump[0] != 2) return false;  // not an RpClump (props are atomics)
    void* hier = scene_game::Call<0x734A40, void*>(clump);  // GetAnimHierarchyFromSkinClump
    if (!hier) return false;
    int n = scene_game::Field<int32_t>(hier, 0x04);         // numNodes
    auto* m = scene_game::Call<0x7C5120, uint8_t*>(hier);   // RpHAnimHierarchyGetMatrixArray: world matrices
    if (!m || n <= 0 || n > 128) return false;
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f, depth = 1e9f;
    for (int i = 0; i < n; ++i) {
        float sx, sy, d;
        if (!ToScreenNow(*(scene_game::CVector*)(m + (size_t)i * 0x40 + 0x30), sx, sy, d)) continue;
        x0 = std::min(x0, sx), x1 = std::max(x1, sx), y0 = std::min(y0, sy), y1 = std::max(y1, sy);
        depth = std::min(depth, d);
    }
    if (x1 < x0) return false;
    float pad = std::max(8.0f, (y1 - y0) * 0.08f);  // flesh and the top of the head around the bones
    b.x = (x0 + x1) * 0.5f;
    b.half = (x1 - x0) * 0.5f + pad;
    b.top = y0 - pad * 1.5f;
    b.bottom = y1 + pad;
    b.depth = depth;
    return true;
}

bool Overlap(const Body& a, const Body& b, float share) {  // more than share of the narrower box
    float w = std::min(a.x + a.half, b.x + b.half) - std::max(a.x - a.half, b.x - b.half);
    float h = std::min(a.bottom, b.bottom) - std::max(a.top, b.top);
    return w > share * 2 * std::min(a.half, b.half) && h > 0;
}

struct Actor {  // another person of the scene near CJ
    scene_game::CEntity* e;
    scene_game::CVector feet;
    Body body;
    bool onScreen;
};

