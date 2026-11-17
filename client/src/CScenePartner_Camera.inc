// ---- the rest of the shot. The camera runs on the cutscene's spline (TheCamera.m_aPathArray, read the way
// CCam::Process_FlyBy 0x5B25F0 does) and the people walk their animations' root tracks, so both are known up to the
// next cut: the spot must suit the whole shot, not only its first frame (cameras pan, people walk about).

View g_now;  // the frame being analysed
bool g_nowOk = false;
bool ToScreenNow(const scene_game::CVector& p, float& sx, float& sy, float& depth) {
    return g_nowOk ? ToScreen(g_now, p, sx, sy, depth) : WorldToScreen(p, sx, sy, depth);
}

// In front of CJ: within 50 degrees of his facing, this near. He talks that way, and nobody stands there.
constexpr float kFrontCos = 0.64f, kFrontNear = 4.0f;
bool InFrontOf(const scene_game::CVector& who, const scene_game::CVector& facing, const scene_game::CVector& feet) {
    scene_game::CVector d = feet - who;
    float len = d.Length2D();
    return len < kFrontNear && d.x * facing.x + d.y * facing.y > kFrontCos * len;
}

struct Sample {  // a moment later in the shot
    float dt;    // seconds after the frame analysed
    View view;
    scene_game::CVector cj;
    scene_game::CVector cjFwd{};              // the way he faces (the plan's moments only)
    std::vector<scene_game::CVector> actors;  // as Scene::actors
    std::vector<scene_game::CVector> cars;    // as Scene::cars
};
std::vector<Sample> g_ahead;
float g_shotFrom = 0, g_shotTo = 0;  // the camera's clock (ms) at the next cut: this shot ends, the next one starts

// The host's game decides and the partner's follows: each spot goes with the moment (the camera's clock) its
// shot begins, and the partner's copy of the cutscene takes it on when it gets there (it runs a little behind).
struct Decision {
    Spot spot;
    float at;
};
std::deque<Decision> g_follow;  // partner: the host's choices still to come
bool g_decided = false;         // host: a choice not sent yet
float g_decidedAt = 0;
struct {
    scene_game::CVector cj;
    float at;
    bool armed;
} g_check{};  // where CJ's track says CJ will be (review logs)

struct Car {  // a vehicle of the cutscene (a prop: no collision, it may drive through the partner)
    int index = -1;  // its cutscene object
    scene_game::CVector pos;
    Bounds b;  // its footprint around pos
    Track track;
    bool Covers(const scene_game::CVector& p, const scene_game::CVector& at, float margin) const {  // p on it when the car is at `at`
        scene_game::CVector d = p - at;
        return d.x > b.x0 - margin && d.x < b.x1 + margin && d.y > b.y0 - margin && d.y < b.y1 + margin;
    }
};

struct Scene {
    scene_game::CVector cam, cj;
    scene_game::CEntity* who = nullptr;  // at cj: CJ, or the person of the scene a shot without CJ is about (Elsewhere)
    scene_game::CVector camFwd;
    float fov = 70;
    float floor = 0;  // under CJ
    Body cjBody{};
    bool cjOnScreen = false;
    bool cjInCar = false;  // CJ within a car's footprint (InCar tells whether he sits in it)
    std::vector<Actor> actors;
    std::vector<Car> cars;
    float w = 0, h = 0;
};

