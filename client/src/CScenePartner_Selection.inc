float PoseHeight(Pose p) { return p == POSE_SIT || p == POSE_STEP ? 1.35f : 1.8f; }
float PoseBonus(Pose p) { return p == POSE_SIT ? 1.6f : p == POSE_STEP ? 1.3f : p == POSE_LEAN ? 1.0f : 0.0f; }

Spot g_spot;
bool g_haveSpot = false;
Spot g_seat;  // his seat in a car of this cutscene: a shot he keeps out of (the camera at it) gives it back after
bool g_haveSeat = false;
Scene g_lastScene;
bool g_live = false;  // a cutscene with CJ was analysed in the last frame
scene_game::CVector g_lastCam, g_lastCj;
int g_shot = 0;
uint32_t g_seed = 0;
char g_cutscene[9] = {};

// head and body in the picture (the legs may be cut off, as in a medium shot)
bool InFrame(const Scene& sc, const Body& b) {
    float waist = b.top + (b.bottom - b.top) * 0.55f;
    return b.depth >= 1.8f && b.x - b.half >= sc.w * 0.06f && b.x + b.half <= sc.w * 0.94f && b.top >= sc.h * 0.03f &&
           waist <= sc.h * 0.97f;
}
bool OnScreen(const Scene& sc, const Body& b) {  // any part of it in the picture
    return b.depth >= 0.5f && b.x + b.half > 0 && b.x - b.half < sc.w && b.bottom > 0 && b.top < sc.h;
}
// someone the picture shows, the middle of the body in it: not an arm at the edge of a shot over the shoulder
bool Shown(const Scene& sc, const Body& b) { return OnScreen(sc, b) && b.x > 0 && b.x < sc.w; }

// the camera being drawn on its spline: false if the spline does not give it, its clock or its keys are not what they seem
bool CameraNow(const Scene& sc, CameraPath& path, float& now, float& fovNow, View& cur) {
    if (cam::Mode() != kModeFlyBy || !path.Load()) return false;
    now = scene_game::Ref<float>(cam::Active() + 0x90);  // CCam::m_fTimeElapsedFloat: the spline's clock (ms)
    path.At(now, cur, fovNow);
    if ((cur.pos - sc.cam).Length() > 0.5f || cur.fwd.Dot(sc.camFwd) < 0.99f || !Calibrate(cur)) {
        LOGD("Cutscene camera off its spline (%.2f m, %.3f): the shot is judged by its first frame", (cur.pos - sc.cam).Length(),
             cur.fwd.Dot(sc.camFwd));
        return false;
    }
    return true;
}
// the shot from t on: it ends at `end`, the next one starts at `next`
void ShotEnd(const CameraPath& path, float t, float& end, float& next) {
    end = next = path.pos.Time(path.pos.n - 1);
    for (const Spline* s : {&path.pos, &path.target, &path.fov}) {
        float from, to;
        if (s->NextCut(t, s == &path.fov ? 1.0f : 0.3f, from, to) && from < end) end = from, next = to;
    }
}

// The camera and the people over the rest of the shot, about every quarter of a second up to the cut.
void BuildAhead(const Scene& sc) {
    g_ahead.clear();
    CameraPath path;
    float now = 0, fovNow = 0, end = 0, next = 0;
    View cur;
    if (!CameraNow(sc, path, now, fovNow, cur)) return;
    ShotEnd(path, now, end, next);
    g_shotFrom = end;
    g_shotTo = next;
    Track cjTrack;
    TrackOf(sc.who, cjTrack);
    std::vector<Track> tracks(sc.actors.size());
    for (size_t i = 0; i < sc.actors.size(); ++i) TrackOf(sc.actors[i].e, tracks[i]);
    scene_game::CVector cjFeet{sc.cj.x, sc.cj.y, sc.floor};
    int steps = std::clamp((int)((end - now) / 250.0f), 1, 16);
    for (int i = 1; i <= steps; ++i) {
        float t = now + (end - 30.0f - now) * (float)i / (float)steps;  // the last one just before the cut
        if (t <= now) break;
        Sample m;
        m.dt = (t - now) / 1000.0f;
        float f = fovNow;
        path.At(t, m.view, f);
        float zoom = std::tan(fovNow * 0.00872665f) / std::tan(std::clamp(f, 5.0f, 170.0f) * 0.00872665f);  // tan(fov/2)
        m.view.kx = cur.kx * zoom, m.view.ky = cur.ky * zoom, m.view.cx = cur.cx, m.view.cy = cur.cy;
        m.cj = FeetLater(cjTrack, cjFeet, m.dt);
        for (size_t k = 0; k < sc.actors.size(); ++k) m.actors.push_back(FeetLater(tracks[k], sc.actors[k].feet, m.dt));
        for (const Car& c : sc.cars) m.cars.push_back(FeetLater(c.track, c.pos, m.dt));
        g_ahead.push_back(std::move(m));
    }
}

bool ProjectAt(const View& v, const scene_game::CVector& feet, float height, Body& b) {
    float hx, hy, hd, fx, fy, fd;
    if (!ToScreen(v, feet + scene_game::CVector{0, 0, height}, hx, hy, hd) || !ToScreen(v, feet + scene_game::CVector{0, 0, 0.05f}, fx, fy, fd))
        return false;
    b.x = (hx + fx) * 0.5f;
    b.top = std::min(hy, fy), b.bottom = std::max(hy, fy);
    b.half = (b.bottom - b.top) * 0.2f;
    b.depth = std::min(hd, fd);
    return true;
}
bool PersonAt(const View& v, const scene_game::CVector& feet, Body& b) {  // someone of the scene later on: as wide as the shoulders
    if (!ProjectAt(v, feet, 1.75f, b)) return false;
    b.half = std::max(b.half, 0.3f * std::fabs(v.kx) / std::max(b.depth, 0.3f));
    return true;
}

enum Reject { R_DISTANCE, R_CROWD, R_FRAME, R_COVERS_CJ, R_COVERS_ACTOR, R_SOLID, R_HIDDEN, R_FLOOR, R_FRONT, R_LATER, R_COUNT };
// How a place is judged (Rate): STRICT in the picture as a whole and out of everybody's way, LOOSE cut by the frame's
// edge or half behind people, SEEN the last try before he is out of the picture.
enum Rules { STRICT, LOOSE, SEEN };

struct Later {  // the spot over the rest of the shot
    int in = 0, edge = 0, out = 0;
    int way = 0;  // moments in somebody's way: SEEN counts them against the place instead of refusing it
    bool bad = false;
};
// The camera in him or about to be (a camera following CJ goes where the partner walks behind him): the body
// fills the picture and projecting it says nothing.
bool AtCamera(const scene_game::CVector& cam, const scene_game::CVector& feet, float height) {
    return (cam - feet).Length2D() < 1.3f && cam.z > feet.z - 0.5f && cam.z < feet.z + height + 0.5f;
}

// In the picture of a later moment: he covers someone, or he is bigger than the people in it (the camera may come
// up close later).
float Covered(const Body& a, const Body& b);
bool InWayAt(const Scene& sc, const Sample& m, const Body& b, bool behindOk) {
    Body p;
    float tallest = 0, hidden = 0;
    auto blocks = [&](float share) {  // as InPeoplesWay: at the back of the group, mostly in sight
        if (!Overlap(b, p, share)) return false;
        if (!behindOk || b.depth < p.depth + 1.0f) return true;
        hidden += Covered(b, p);
        return hidden > 0.6f;
    };
    if (PersonAt(m.view, m.cj, p) && OnScreen(sc, p)) {
        if (blocks(0.0f)) return true;
        tallest = Shown(sc, p) ? p.bottom - p.top : 0;
    }
    for (const scene_game::CVector& a : m.actors)
        if (PersonAt(m.view, a, p) && OnScreen(sc, p)) {
            if (blocks(0.1f)) return true;
            tallest = std::max(tallest, Shown(sc, p) ? p.bottom - p.top : 0);
        }
    return b.bottom - b.top > std::max(tallest * 1.15f, sc.h * 0.3f);
}

// nobody walks into it, it covers nobody, and the camera keeps it in the picture or away from it
Later RateLater(const Scene& sc, const Spot& s, const scene_game::CVector& body, Rules rules = STRICT) {
    Later r;
    float height = PoseHeight(s.pose), room = s.pose == POSE_SIT || s.pose == POSE_STEP ? 0.55f : 0.75f;
    for (const Sample& m : g_ahead) {
        r.bad = (m.cj - body).Length2D() < 1.0f || AtCamera(m.view.pos, body, height);
        for (const scene_game::CVector& a : m.actors) r.bad = r.bad || (a - body).Length2D() < room;
        for (size_t k = 0; k < sc.cars.size(); ++k) r.bad = r.bad || sc.cars[k].Covers(body, m.cars[k], 0.6f);
        Body b;
        if (r.bad) return r;
        if (!ProjectAt(m.view, body, height, b) || !OnScreen(sc, b)) {
            ++r.out;
            continue;
        }
        if (InWayAt(sc, m, b, rules != STRICT)) {
            if (rules != SEEN) {
                r.bad = true;
                return r;
            }
            ++r.way;
            continue;
        }
        if (!GridSeen(m.view.pos, body + scene_game::CVector{0, 0, height * 0.6f})) ++r.out;  // behind a wall for a moment
        else if (InFrame(sc, b)) ++r.in;
        else ++r.edge;
    }
    return r;
}

// An extra keeps out of the scene's way: no bigger on the screen than its people (a third of the screen when none
// is in the picture), and not between two of them nearer to the camera than they are, where the talk goes on.
// (onScreen means only that the skeleton projects: CJ next to the camera, out of the picture but for an elbow, would
// let the partner fill the screen.)
// The last try (seen) lets him be half as big again as the tallest of them, and stand between two of them when he is
// not nearer to the camera than both.
bool InTheWay(const Scene& sc, const Body& b, bool seen = false) {
    std::vector<const Body*> people;
    if (sc.cjOnScreen && Shown(sc, sc.cjBody)) people.push_back(&sc.cjBody);
    for (const Actor& a : sc.actors)
        if (a.onScreen && Shown(sc, a.body)) people.push_back(&a.body);
    float tallest = 0;
    for (const Body* p : people) tallest = std::max(tallest, p->bottom - p->top);
    if (b.bottom - b.top > std::max(tallest * (seen ? 1.5f : 1.15f), sc.h * (seen ? 0.4f : 0.3f))) return true;
    for (const Body* p : people)
        for (const Body* q : people)
            if (p->x < b.x && b.x < q->x &&
                b.depth < (seen ? std::min(p->depth, q->depth) : std::max(p->depth, q->depth)) - 0.3f)
                return true;
    return false;
}
int g_rejects[R_COUNT];
int g_lastReject = -1;
float Rejected(Reject r) {
    ++g_rejects[r];
    g_lastReject = r;
    return kRejected;
}
int g_attractors[16];  // ped attractor types seen around CJ in the last analysis (logs)

// a floor at CJ's level under the spot, solid or only drawn (the partner is frozen there, so drawn is enough)
bool FloorAt(const Scene& sc, scene_game::CVector& g) {
    float z;
    if (Floor(g.x, g.y, sc.floor + 1.0f, z) && std::fabs(z - sc.floor) <= 0.45f) {
        g.z = z;
        return true;
    }
    g.z = sc.floor;
    return g_grid.Ground(g.x, g.y);
}

// The spot itself, whatever the camera: near CJ (within `far`), and nobody of the scene or a car of it there. -1 when so.
int Crowded(const Scene& sc, const Spot& s, float& clear, float far = 6.0f) {
    bool seated = s.pose == POSE_SIT || s.pose == POSE_STEP;
    scene_game::CVector body = BodyOf(s);  // where the animation puts it
    float d = (body - sc.cj).Length2D();
    if (d < 1.3f || d > far) return R_DISTANCE;
    clear = 9.0f;
    for (const Actor& a : sc.actors) clear = std::min(clear, (a.feet - body).Length2D());
    if (clear < (seated ? 0.55f : 0.75f)) return R_CROWD;
    for (const Car& c : sc.cars)
        if (c.Covers(body, c.pos, 0.6f)) return R_CROWD;
    return AtCamera(sc.cam, body, PoseHeight(s.pose)) ? R_FRAME : -1;
}
// How much of box a the box b covers (0..1)
float Covered(const Body& a, const Body& b) {
    float w = std::min(a.x + a.half, b.x + b.half) - std::max(a.x - a.half, b.x - b.half);
    float h = std::min(a.bottom, b.bottom) - std::max(a.top, b.top);
    return w > 0 && h > 0 ? w * h / std::max(1.0f, 2 * a.half * (a.bottom - a.top)) : 0.0f;
}

// In the picture: covering nobody of the scene and out of its way. -1 when so. Behind (a metre farther from the camera
// than whom he overlaps) he may stand at the back of the group, if most of him still shows: kept off every person of
// the shot, he was out of the picture through most medium shots of a group. The last try (SEEN) lets him overlap the
// side of somebody else than CJ by up to a third and stand three quarters behind people.
int InPeoplesWay(const Scene& sc, const Body& b, Rules rules = STRICT) {
    Body cj = sc.cjBody;  // in a close-up CJ bends and waves about more than his root track tells: keep off
    if (cj.depth < 2.0f) cj.half *= 1.5f;
    float hidden = 0, side = rules == SEEN ? 0.33f : 0.1f, most = rules == SEEN ? 0.75f : 0.6f;
    auto blocks = [&](const Body& p, float share) {
        if (!Overlap(b, p, share)) return false;
        if (rules == STRICT || b.depth < p.depth + 1.0f) return true;
        hidden += Covered(b, p);
        return hidden > most;
    };
    if (sc.cjOnScreen && blocks(cj, 0.0f)) return R_COVERS_CJ;
    for (const Actor& a : sc.actors)  // nobody of the scene behind or in front of the partner
        if (a.onScreen && blocks(a.body, side)) return R_COVERS_ACTOR;
    return InTheWay(sc, b, rules == SEEN) ? R_FRONT : -1;
}
// Room for the body: a wall to lean on with nothing between, free floor at CJ's level (its height goes into the
// spot), nothing where the hips and the head are. -1 when so.
int NoRoom(const Scene& sc, Spot& s) {
    scene_game::CVector& g = s.ground;
    bool seated = s.pose == POSE_SIT || s.pose == POSE_STEP;
    if (s.pose == POSE_LEAN) {  // the feet on free floor and nothing between the back and the wall
        if (!g_grid.Clear(g, 0.22f) || g_grid.Hit(g, Forward(s.heading) * -1.0f, ActOf(s)->back - 0.03f, kBumpLow) >= 0)
            return R_SOLID;
    } else if (!seated && !g_grid.Clear(g, 0.3f)) {
        return R_SOLID;
    }
    if (!seated && !FloorAt(sc, g)) return R_FLOOR;
    if (!seated && (!Free(g + scene_game::CVector{0, 0, 0.65f}, 0.28f) || !Free(g + scene_game::CVector{0, 0, 1.45f}, 0.28f)))  // the costly one
        return R_SOLID;
    return -1;
}

// For the log and the reviews: the share of the shot the partner stands in the picture (his middle within the frame,
// no wall in between), now and at the samples ahead. -1 riding or hidden. A place he stays at counted as seen before.
float SeenShare(const Scene& sc, const Spot& s) {
    if (s.pose == POSE_HIDDEN || s.pose == POSE_CAR) return -1.0f;
    float height = PoseHeight(s.pose);
    scene_game::CVector body = BodyOf(s), chest = body + scene_game::CVector{0, 0, height * 0.6f}, head = body + scene_game::CVector{0, 0, height - 0.15f};
    // walls by the world's collision, once: the obstacle map keeps a cell's highest point only, and a porch's roof
    // hid everything under it (CAT_2's cabin, Sweet's door)
    if (!Seen(sc.cam, chest) && !Seen(sc.cam, head)) return 0.0f;
    Body b;
    int seen = Project(body, height, b) && Shown(sc, b) ? 1 : 0;
    for (const Sample& m : g_ahead)
        if (ProjectAt(m.view, body, height, b) && Shown(sc, b)) ++seen;
    return (float)seen / (float)(1 + g_ahead.size());
}

// Where he stood before this shot, if that is in this scene. A new place costs a point a metre from there, so he goes
// to the nearest one that fits rather than across the room (31.10: he moved almost every shot; the players
// chose "seen in every shot, in short steps").
bool g_fromOk = false;
scene_game::CVector g_from;
// The shot before showed him (SeenShare as decided), and the last try of this one may squeeze through narrower gaps
// and be seen in less of the shot (Choose: after all else failed, when he was out of the picture before too).
bool g_seenBefore = false, g_lastChance = false;

// How good the spot is for this shot; kRejected when it does not fit at all. Cheap checks first. Loose: he need not
// be in the picture as a whole, nor seen (cut by its edge, half behind something), only out of the people's way.
// SEEN, the last try: up to 10 m from CJ, at somebody's side or mostly behind people, in somebody's way for a moment,
// as long as the shot shows him for a fifth of it; the more it shows him the better.
float Rate(const Scene& sc, Spot& s, Rules rules = STRICT) {
    bool loose = rules != STRICT;
    g_lastReject = -1;
    float clear;
    // the loose places go farther: in a crowd around CJ (CESAR1A, GROVE1B) the room the camera shows is behind it
    if (int r = Crowded(sc, s, clear, rules == SEEN ? 10.0f : loose ? 8.0f : 6.0f); r >= 0) return Rejected((Reject)r);
    if (rules == SEEN && !Reachable(s.ground)) return Rejected(R_SOLID);  // where the feet go: BuildReach first
    scene_game::CVector body = BodyOf(s);
    float d = (body - sc.cj).Length2D();
    Body b;
    float height = PoseHeight(s.pose);
    bool projected = Project(body, height, b), shown = projected && OnScreen(sc, b);
    if (loose ? shown && b.depth < 1.5f : !projected || !InFrame(sc, b)) return Rejected(R_FRAME);
    if (int r = shown ? InPeoplesWay(sc, b, rules) : -1; r >= 0) return Rejected((Reject)r);
    if (int r = NoRoom(sc, s); r >= 0) return Rejected((Reject)r);
    body.z = s.ground.z;
    scene_game::CVector head = body + scene_game::CVector{0, 0, height - 0.15f}, chest = body + scene_game::CVector{0, 0, height * 0.6f};
    if (!loose && (!GridSeen(sc.cam, head) || !GridSeen(sc.cam, chest) || !Seen(sc.cam, head) || !Seen(sc.cam, chest)))
        return Rejected(R_HIDDEN);
    // the rest of the shot: in the picture most of the time, seldom cut by its edge
    Later later = RateLater(sc, s, body, rules);
    int n = 1 + later.in + later.edge + later.out + later.way;
    if (later.bad || later.way * 2 > n || (!loose && ((1 + later.in) * 10 < n * 6 || later.edge * 4 > n)))
        return Rejected(R_LATER);
    // a loose place still in sight for a good part of the shot: one behind a wall or out at the frame's edge was taken
    // as well, and he stood unseen while the camera had room for him
    float seen = loose ? SeenShare(sc, s) : 1.0f;
    if (seen < (rules == SEEN ? (g_lastChance ? 0.05f : 0.2f) : 0.3f)) return Rejected(R_HIDDEN);
    // to one side, as an extra: a third of the way in from an edge. With the people: a place more than 2 m from the
    // nearest of them (CJ too) costs 0.7 a metre more, and room around him counts up to 1.5 m only; he stood aside, far
    // from everybody, as if he were not in the scene (01.11, the players' word after the sixth test build)
    float nearest = std::min(clear, d);
    float score = PoseBonus(s.pose) - std::fabs(d - 2.2f) * 0.5f + std::min(clear, 1.5f) * 0.3f -
                  std::max(0.0f, nearest - 2.0f) * 0.7f -
                  (shown ? std::fabs(std::fabs(b.x / sc.w - 0.5f) - 0.25f) * 0.8f : 0.4f) + 0.5f * (float)(1 + later.in) / (float)n;
    if (shown && b.bottom - b.top < sc.h * 0.12f) score -= 1.0f;  // too small to notice
    if (g_haveSpot && g_spot.pose == s.pose && (g_spot.ground - s.ground).Length2D() < 0.3f) score += 2.5f;  // stays put
    if (g_fromOk) score -= (s.ground - g_from).Length2D();
    if (loose) score += seen;  // the more of the shot he is seen, the better
    if (rules == SEEN) score += seen * 2.0f - 2.0f * (float)later.way / (float)n;
    return score;
}

struct Tried {  // for the review map
    Spot spot;
    float score;
    int reject;
};
std::vector<Tried> g_tried;

struct Best {
    Spot spot;
    float score = kRejected;
    Rules rules = STRICT;
    void Offer(const Scene& sc, Spot s) {
        float r = Rate(sc, s, rules);
        g_tried.push_back({s, r, g_lastReject});
        if (r > score) spot = s, score = r;
    }
};

// seats and steps: the ped attractors of benches, chairs and stairs around CJ
void OfferAttractors(const Scene& sc, Best& best) {
    scene_game::CEntity* ents[96];
    int16_t n = 0;
    scene_game::Call<0x564A20>(&sc.cj, 9.0f, false, &n, (int16_t)96, ents, true, false, false, true, true);  // CWorld::FindObjectsInRange
    for (int i = 0; i < n; ++i) {
        scene_game::CEntity* e = ents[i];
        void* mi = scene_game::Ref<void*>(addr::ModelInfoPtrs + (uintptr_t)e->ModelIndex() * 4);
        if (!mi) continue;
        int count = scene_game::Field<uint8_t>(mi, 0x0D);  // m_nNum2dEffects
        for (int k = 0; k < count; ++k) {
            auto* fx = scene_game::CallMethod<0x4C4C70, uint8_t*>(mi, k);  // CBaseModelInfo::Get2dEffect
            if (!fx || fx[0x0C] != 3) continue;                  // ped attractor
            int8_t type = (int8_t)fx[0x34];
            if (type >= 0 && type < 16) ++g_attractors[type];
            if (type != kAttractorSeat && type != kAttractorStep) continue;
            Spot s;
            s.pose = type == kAttractorSeat ? POSE_SIT : POSE_STEP;
            s.ground = Transform(e, *(scene_game::CVector*)fx, true);
            s.heading = HeadingOf(Flat(Transform(e, *(scene_game::CVector*)(fx + 0x1C), false)));  // m_vecUseDir, as the engine seats peds
            float z;
            if (!Floor(s.ground.x, s.ground.y, s.ground.z + 0.3f, z) || std::fabs(z - sc.floor) > 0.6f) continue;
            s.ground.z = z;
            best.Offer(sc, s);
        }
    }
}

// Chairs, sofas and benches as they are drawn, the cutscene's props among them: a flat seat at sitting height,
// a backrest behind it and room for the legs in front.
constexpr float kSeatHeight = 0.4f;  // the seat SEAT_idle is made for: its hips are 0.46 m over the feet
void OfferSeats(const Scene& sc, Best& best) {
    const Act* sit = Pick(kSit, 0);
    for (float dy = -4.6f; dy <= 4.6f; dy += 0.2f)
        for (float dx = -4.6f; dx <= 4.6f; dx += 0.2f) {
            scene_game::CVector p{sc.cj.x + dx, sc.cj.y + dy, sc.floor};
            float h = g_grid.Seat(p.x, p.y);
            if (h < 0.35f || h > 0.62f || std::fabs(g_grid.Top(p.x, p.y) - h) > 0.03f) continue;  // nothing over the seat
            bool wide = true;  // a seat, not the edge of something
            for (const scene_game::CVector& o : {scene_game::CVector{0.15f, 0, 0}, scene_game::CVector{-0.15f, 0, 0}, scene_game::CVector{0, 0.15f, 0}, scene_game::CVector{0, -0.15f, 0}})
                wide = wide && std::fabs(g_grid.Seat(p.x + o.x, p.y + o.y) - h) < 0.06f;
            if (!wide) continue;
            for (int k = 0; k < 8; ++k) {
                scene_game::CVector back{std::cos(k * 0.7854f), std::sin(k * 0.7854f), 0};
                float b = g_grid.Hit(p, back, 0.4f, h + 0.25f);  // the backrest, the probe where the hips go
                if (b < 0.15f) continue;
                scene_game::CVector face = back * -1.0f;
                float edge = -1;  // the front of the seat: deep sofas too, a bed or a table has none
                for (int e = 1; e <= 16 && edge < 0; ++e)
                    if (g_grid.Top(p.x + face.x * e * 0.05f, p.y + face.y * e * 0.05f) < 0.3f) edge = e * 0.05f;
                if (edge < 0) continue;
                // the hips 20 cm off the backrest, the thighs over the front edge, the feet where the animation wants them
                scene_game::CVector hips = p + back * b + face * 0.2f, feet = hips + face * sit->back;
                if (edge + b - 0.2f > 0.5f || std::fabs(g_grid.Seat(hips.x, hips.y) - h) > 0.06f) continue;
                if (g_grid.Top(feet.x, feet.y) > 0.3f) continue;  // a table or a wall where the legs go
                feet.z = sc.floor + std::clamp(h - kSeatHeight, -0.1f, 0.15f);
                best.Offer(sc, Spot{feet, HeadingOf(face), POSE_SIT, 0});
            }
        }
}

// A wall right behind p: the back against it, the face out of it. Two points 35 cm apart give its direction.
void OfferLean(const Scene& sc, const scene_game::CVector& p, const scene_game::CVector& face, Best& best) {
    scene_game::CVector back = face * -1.0f, side{-back.y, back.x, 0};
    float d0 = g_grid.Hit(p, back, 0.9f, 1.6f);
    if (d0 < 0) return;
    scene_game::CVector a = p + side * 0.35f, b = p - side * 0.35f;
    float da = g_grid.Hit(a, back, d0 + 0.4f, 1.6f), db = g_grid.Hit(b, back, d0 + 0.4f, 1.6f);
    if (da < 0 || db < 0) return;  // a post or a lamp, not a wall
    scene_game::CVector wa = a + back * da, wb = b + back * db;
    scene_game::CVector along = Flat(wb - wa), n{-along.y, along.x, 0};
    if (n.Dot(face) < 0) n = n * -1.0f;
    if (n.Dot(face) < 0.8f) return;  // the wall turns away here
    // the back 9 cm off the wall: the ped's position as far out as its animation moves the body back
    uint8_t act = (uint8_t)((g_seed / 4) % 2);
    scene_game::CVector spot = (wa + wb) * 0.5f + n * (Pick(kLean, act)->back + 0.09f);
    spot.z = sc.floor;
    best.Offer(sc, Spot{spot, HeadingOf(n), POSE_LEAN, act});
}

// Free floor: around CJ, and where the camera looks (in a close-up only the background is in the picture).
void OfferFloor(const Scene& sc, Best& best) {
    std::vector<scene_game::CVector> places;
    for (float r : {1.4f, 1.9f, 2.5f, 3.2f, 4.0f, 5.0f})
        for (int a = 0; a < 24; ++a) {
            float ang = (float)a * (6.2831853f / 24.0f);
            places.push_back(sc.cj + scene_game::CVector{std::cos(ang) * r, std::sin(ang) * r, 0.0f});
        }
    scene_game::CVector view = Flat(sc.camFwd);
    float far = best.rules == SEEN ? 10.0f : best.rules == LOOSE ? 8.0f : 6.0f;  // from CJ, as Rate takes them
    for (int k = -12; k <= 12; ++k)
        for (float d = 1.5f; d <= (best.rules == SEEN ? 18.0f : 12.0f); d += 0.5f) places.push_back(sc.cam + Turn(view, k * 0.0873f) * d);
    for (scene_game::CVector p : places) {
        float d = (p - sc.cj).Length2D();
        if (d < 1.3f || d > far) continue;
        p.z = sc.floor;
        // faces CJ, turned a little to the camera like an extra on a film set
        scene_game::CVector face = Flat(Flat(sc.cj - p) + Flat(sc.cam - p) * 0.6f);
        best.Offer(sc, Spot{p, HeadingOf(face), POSE_STAND, (uint8_t)(g_seed % 4)});
        // no wall to lean on in the last try: far from CJ, by a door, he stood there like a guard (31.10)
        if (best.rules != SEEN) OfferLean(sc, p, face, best);
    }
}

// Any part of the partner at the spot shows in this shot, now or later (a shoulder at the edge counts). With walls
// off, all that is in the frame counts, behind a wall too, with room for the arms: the drawn walls are only as
// tall as their highest point, so a porch roof hides what stands under it.
bool Shows(const Scene& sc, const Spot& s, bool walls = true) {
    Body b;
    float height = PoseHeight(s.pose);
    scene_game::CVector body = BodyOf(s), chest = body + scene_game::CVector{0, 0, height * 0.6f};
    auto in = [&](Body b, const scene_game::CVector& from) {
        if (!walls) b.half *= 1.5f;
        return OnScreen(sc, b) && (!walls || GridSeen(from, chest));
    };
    if (Project(body, height, b) && in(b, sc.cam)) return true;
    for (const Sample& m : g_ahead)
        if (ProjectAt(m.view, body, height, b) && in(b, m.view.pos)) return true;
    return false;
}

// A text of coop.rc. Each game reads a way by its own clock and nothing of it goes over the network, so they must all hold the
// same one: in coop.asi it can't be lost or left over from another version, and the protocol keeps versions apart (08.11).

// The next line of an embedded text, cut to fit and ended with a zero (the resource has none for sscanf); false at the end.


// Places chosen by hand for a cutscene (cutscenes.txt, in coop.asi): "NAME x y z heading pose [act]" (heading in degrees,
// pose stand, lean, sit or steps) or "NAME hidden" (the partner keeps out of the whole cutscene). A cutscene with
// places of its own uses only them, still checked shot by shot.
struct Places {
    bool hidden = false;
    std::vector<Spot> spots;
};
const Places* PlacesOf(const char* cutscene) {
    static std::map<std::string, Places> table;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        std::string_view text = Embedded(L"CUTSCENE_PLACES");
        char line[256], name[16], pose[16];
        for (int act = 0; NextLine(text, line, sizeof line);) {
            Spot sp;
            float deg = 0;
            int n = sscanf(line, "%15s %f %f %f %f %15s %d", name, &sp.ground.x, &sp.ground.y, &sp.ground.z, &deg, pose, &act);
            if (n < 1 || name[0] == '#') continue;
            for (char* c = name; *c; ++c) *c = (char)toupper((uint8_t)*c);
            Places& pl = table[name];
            if (strstr(line, " hidden")) {
                pl.hidden = true;
                continue;
            }
            if (n < 6) continue;
            static const char* names[] = {"stand", "lean", "sit", "steps"};
            int k = 0;
            while (k < 4 && strcmp(pose, names[k])) ++k;
            if (k == 4) continue;
            sp.pose = (Pose)k;
            sp.act = (uint8_t)(n == 7 ? act : 0);
            sp.heading = deg * 0.0174533f;
            pl.spots.push_back(sp);
        }
        LOGI("Cutscene places by hand: %zu cutscenes", table.size());
    }
    std::string key(cutscene, strnlen(cutscene, 8));
    for (char& c : key) c = (char)toupper((uint8_t)c);
    auto it = table.find(key);
    return it == table.end() ? nullptr : &it->second;
}

// May the partner stay where an earlier shot put him? People do not vanish between two shots: this one may show him
// anywhere, cut by the frame's edge or small at the back, as long as he covers nobody, keeps out of the scene's way
// and nobody walks into him, and the scene is still where he is. CJ walking off across the room is no reason to go
// (it was within 9 m: he went after CJ from shot to shot, 28.10: "нап не должен перемещаться из кадра в кадр"); CJ on
// another floor or far away is.
const char* g_staysNo = "";  // why he did not stay (logs)
bool Stays(const Scene& sc, const Spot& s) {
    bool seated = s.pose == POSE_SIT || s.pose == POSE_STEP;
    scene_game::CVector body = BodyOf(s);
    float d = (body - sc.cj).Length2D();
    if (d < 1.0f) return g_staysNo = "CJ at him", false;
    if (d > 25.0f || std::fabs(s.ground.z - sc.floor) > 1.0f) return g_staysNo = "the scene went elsewhere", false;
    for (const Actor& a : sc.actors)
        if ((a.feet - body).Length2D() < (seated ? 0.55f : 0.75f)) return g_staysNo = "a person on his spot", false;
    for (const Car& c : sc.cars)
        if (c.Covers(body, c.pos, 0.6f)) return g_staysNo = "a car on his spot", false;
    if (AtCamera(sc.cam, body, PoseHeight(s.pose))) return g_staysNo = "the camera at him", false;
    Body b;
    if (Project(body, PoseHeight(s.pose), b) && OnScreen(sc, b) && (b.depth < 1.5f || InPeoplesWay(sc, b, LOOSE) >= 0))
        return g_staysNo = "in front of the people", false;
    if (RateLater(sc, s, body, LOOSE).bad) return g_staysNo = "somebody walks into him later", false;
    if (!seated && InFrontOf(sc.cj, CjFacing(), body)) return g_staysNo = "in front of CJ", false;
    return true;
}

// No place in the picture and none to stay at: he stands by CJ where this shot does not look, so that a later shot
// looking that way finds him there (Stays) rather than him popping up.
bool OutOfPicture(const Scene& sc, Spot& out) {
    float best = kRejected, clear;
    for (float r : {1.9f, 2.5f, 3.2f, 1.4f, 4.0f})
        for (int a = 0; a < 24; ++a) {
            float ang = (float)a * (6.2831853f / 24.0f), score = -std::fabs(r - 2.2f);
            Spot s{sc.cj + scene_game::CVector{std::cos(ang) * r, std::sin(ang) * r, 0.0f}, 0, POSE_STAND, (uint8_t)(g_seed % 4)};
            s.ground.z = sc.floor;
            s.heading = HeadingOf(Flat(sc.cj - s.ground));
            if (score <= best || Crowded(sc, s, clear) >= 0 || Shows(sc, s, false) || NoRoom(sc, s) >= 0 ||
                RateLater(sc, s, BodyOf(s)).bad)
                continue;
            best = score, out = s;
        }
    return best > kRejected;
}

// The last place before none: next to CJ on his floor, where nobody of the scene stands and not at the camera, with room
// for him if the floor is known, in the picture if it can be. The first seconds of an interior had no floor yet (its
// buildings still streaming in): every place was refused, and he was hidden (SWEET3A, SWEET6A, INTRO1A).
// Half a metre farther than at first: 1.8 m was in CJ's face in Zero's shop ("чуть подальше надо", 31.10).
bool Beside(const Scene& sc, Spot& out) {
    float best = kRejected, clear;
    for (float r : {2.3f, 2.8f, 1.8f, 3.3f})
        for (int a = 0; a < 16; ++a) {
            float ang = (float)a * (6.2831853f / 16.0f);
            Spot s{sc.cj + scene_game::CVector{std::cos(ang) * r, std::sin(ang) * r, 0.0f}, 0, POSE_STAND, (uint8_t)(g_seed % 4)};
            s.ground.z = sc.floor;
            s.heading = HeadingOf(Flat(sc.cj - s.ground));
            if (Crowded(sc, s, clear) >= 0) continue;
            Spot t = s;
            bool room = NoRoom(sc, t) < 0;
            float score = -std::fabs(r - 2.3f) + (room ? 2.0f : 0.0f) + (Shows(sc, room ? t : s) ? 1.0f : 0.0f);
            if (score > best) best = score, out = room ? t : s;
        }
    return best > kRejected;
}

// ---- at CJ's side (the players' choice, 03.11: "рядом с хостом", no places drawn at random): abreast of him, a metre
// or a metre and a half to one side, the same side the whole scene, facing as he does; a little behind him where that
// is taken, the other side only where both are. Chosen by the picture, he stood by Sweet and Big Smoke in "Tagging Up
// Turf", 2-5 m off CJ, and in ZERO_2 he walked from place to place.
int g_side = 0;  // the scene's side of CJ: 1 his right, -1 his left; 0 not chosen yet

// strict: out of the people's way in the picture and over the rest of the shot too; else only room for the body, nobody
// there and not at the camera (a cramped room, Zero's shop: at CJ's side half in front of someone rather than across it)
bool BesideFits(const Scene& sc, Spot& s, bool strict) {
    float clear;
    if (Crowded(sc, s, clear) >= 0 || NoRoom(sc, s) >= 0) return false;
    scene_game::CVector body = BodyOf(s);
    if (AtCamera(sc.cam, body, PoseHeight(s.pose))) return false;
    if (!strict) return true;
    Body b;
    if (Project(body, PoseHeight(s.pose), b) && OnScreen(sc, b) && (b.depth < 1.5f || InPeoplesWay(sc, b, LOOSE) >= 0)) return false;
    return !RateLater(sc, s, body, LOOSE).bad;
}

// The share of the shot that shows CJ
float CjShare(const Scene& sc) {
    int shown = sc.cjOnScreen && Shown(sc, sc.cjBody) ? 1 : 0;
    Body p;
    for (const Sample& m : g_ahead)
        if (PersonAt(m.view, m.cj, p) && Shown(sc, p)) ++shown;
    return (float)shown / (float)(1 + g_ahead.size());
}

// A place by CJ's side or back, never in front of him: abreast of him at a metre and a half first, the scene's side and
// near where he stood (from) rather than across. A shot that shows CJ takes a place it shows too (seen), when there
// is one. Three fixed places were all taken in the garage at the start of "Deconstruction" (04.11), and the rules of the picture took him
// 8 m about the room from shot to shot; those places knew nothing of the shot, and 14 of 26 shots did not show him.
bool BesideCj(const Scene& sc, const Spot* from, bool seen, Spot& out) {
    scene_game::CEntity* cj = CutsceneCj();
    if (!cj || !cj->Matrix()) return false;
    scene_game::CVector fwd = CjFacing(), right{fwd.y, -fwd.x, 0.0f}, feet{sc.cj.x, sc.cj.y, sc.floor};
    auto at = [&](int side, float r, float deg) {  // deg from his front round to his back
        float a = deg * 0.0174533f;
        return Spot{feet + right * ((float)side * r * std::sin(a)) + fwd * (r * std::cos(a)), HeadingOf(fwd), POSE_STAND, (uint8_t)(g_seed % 4)};
    };
    int first = g_side;
    if (!first) {  // the side away from the scene's people: not between CJ and whom he talks to
        auto room = [&](int side) {
            float d = 99.0f;
            for (const Actor& a : sc.actors) d = std::min(d, (a.feet - at(side, 1.5f, 90.0f).ground).Length2D());
            return d;
        };
        first = room(-1) > room(1) + 0.3f ? -1 : 1;
    }
    float best = kRejected;
    int bestSide = 0;
    for (bool strict : {true, false}) {
        for (int side : {first, -first})
            for (float r : {1.5f, 2.0f, 2.6f})
                for (float deg : {90.0f, 120.0f, 150.0f, 180.0f}) {
                    Spot s = at(side, r, deg);
                    float score = -(r - 1.5f) * 0.5f - (deg - 90.0f) / 90.0f * 0.6f - (side != first ? 0.7f : 0.0f);
                    if (from) score -= (s.ground - from->ground).Length2D() * 0.3f;
                    if (score + 4.0f <= best || !BesideFits(sc, s, strict)) continue;
                    if (float share = seen && strict ? SeenShare(sc, s) : 0.0f; share >= 0.3f) score += 3.0f + share;
                    if (score > best) best = score, out = s, bestSide = side;
                }
        if (best > kRejected) break;
    }
    if (best <= kRejected) return false;
    if (!g_side) g_side = bestSide;
    return true;
}

// A bone's world position from the entity's skin (cutscene people and peds alike): BONE_PELVIS 2
bool BoneAt(scene_game::CEntity* e, int bone, scene_game::CVector& out) {
    auto* clump = e ? (uint8_t*)e->RwObject() : nullptr;
    void* hier = clump && clump[0] == 2 ? scene_game::Call<0x734A40, void*>(clump) : nullptr;  // GetAnimHierarchyFromSkinClump
    auto* m = hier ? scene_game::Call<0x7C5120, uint8_t*>(hier) : nullptr;                     // RpHAnimHierarchyGetMatrixArray
    int i = m ? scene_game::Call<0x7C51A0, int>(hier, bone) : -1;                            // RpHAnimIDGetIndex
    if (i < 0) return false;
    out = *(scene_game::CVector*)(m + (size_t)i * 0x40 + 0x30);
    return true;
}

// Somebody with a body: a person's skeleton has 30 bones and more, a skinned prop up to 15. The sack of BCRAS1 sat in the car as a
// fourth passenger, no seat was left, and he came into the scene 5 s after CJ ("спустя 4 секунды появляется", 06.11)
bool HasBody(scene_game::CEntity* e) {
    auto* rw = (uint8_t*)e->RwObject();
    void* hier = rw && rw[0] == 2 ? scene_game::Call<0x734A40, void*>(rw) : nullptr;  // GetAnimHierarchyFromSkinClump
    return hier && scene_game::Field<int32_t>(hier, 4) >= 20;                         // RpHAnimHierarchy::numNodes
}

// One of the scene's people's hips, else its root. The skeleton is updated only while it is drawn: one not drawn since
// the last shot is where it was then, metres from the car it rides in now.
scene_game::CVector HipsOf(scene_game::CEntity* e) {
    scene_game::CVector hips;
    return BoneAt(e, 2, hips) && (hips - e->GetPosition()).Length() < 1.5f ? hips : e->GetPosition();
}

// The game's vehicle a car of the cutscene stands for: the cutscene loads it as a model of its own, named "cs" and the
// vehicle's ("csgreenwood" greenwoo, "cscopcarla92" copcarla, "cszr350b" zr350), without the vehicle's seat dummies.
// -1 for anything else: any big prop counted as a car (the palms of CAS_3, the yard of STEAL_5), CJ by it rode, and the
// partner sat in the air.
int VehicleOf(scene_game::CEntity* e) {
    int model = e->ModelIndex();
    if (model >= 400 && model <= 611) return model;
    for (int i = 0; i < std::clamp(scene_game::Ref<int32_t>(0xBC3FE8), 0, 50); ++i) {  // CCutsceneMgr::ms_numLoadObjectNames
        if (scene_game::Ref<int32_t>(0xBC31C0 + i * 4) != model) continue;             // ms_iModelIndex
        const char* name = (const char*)(0xBC38C8 + i * 32);               // ms_cLoadObjectName
        if (_strnicmp(name, "cs", 2) != 0) return -1;
        std::string n(name + 2, strnlen(name + 2, 29));
        for (size_t len = n.size(); len >= 4; --len) {
            int id = -1;
            if (scene_game::Call<0x4C5940, void*>(n.substr(0, len).c_str(), &id) && id >= 400 && id <= 611) return id;  // CModelInfo::GetModelInfo
        }
        return -1;
    }
    return -1;
}

// The seats of that vehicle (its driver's too): cars and monster trucks only, as many as its doors give
// (CVehicleModelInfo::GetMaximumNumberOfPassengersFromNumberOfDoors). 0 for anything nobody sits in like that. The doors
// are counted as the vehicle's model loads (none known for Greenwood, Glendale, Washington and Bravura in their
// cutscenes): loaded for it once, then let go.
int SeatsOf(int vehicle) {
    static std::map<int, int> known;
    if (vehicle < 0) return 0;
    if (auto it = known.find(vehicle); it != known.end()) return it->second;
    void* mi = scene_game::Ref<void*>(addr::ModelInfoPtrs + (uintptr_t)vehicle * 4);
    int type = mi ? scene_game::Field<int32_t>(mi, 0x3C) : -1;  // m_nVehicleType: 0 car, 1 monster truck (bikes, boats, aircraft: no)
    if (type != 0 && type != 1) return known[vehicle] = 0;
    bool had = IsModelLoaded(vehicle);
    if (!had) RequestModel(vehicle, 2 | 0x10), LoadAllRequestedModels(true);  // GAME_REQUIRED | PRIORITY_REQUEST
    if (!IsModelLoaded(vehicle)) return 0;  // the next shot tries again
    int seats = scene_game::Field<uint8_t>(mi, 0x4C) ? 1 + scene_game::Call<0x4C89B0, int>(vehicle) : 0;  // m_nNumDoors (none: seat dummies are read)
    if (!had) MarkModelNotNeeded(vehicle);
    return known[vehicle] = seats;
}

// The room inside a car of the scene, in its own space: what is drawn of it less a margin. Hips there sit in it,
// not by a door, at the boot or on the bonnet (CJ behind the car in BCESA5W rode, the partner in the air behind it).
struct Cabin {
    const scene_game::CMatrix* m;
    scene_game::CVector lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
    float roof0 = 1e9f, roof1 = -1e9f;  // the roof's length: what is drawn within 15 cm of the top
    scene_game::CVector Local(const scene_game::CVector& w) const {
        scene_game::CVector d = w - m->pos;
        return {d.Dot(m->right), d.Dot(m->forward), d.Dot(m->up)};
    }
    scene_game::CVector World(const scene_game::CVector& l) const { return m->pos + m->right * l.x + m->forward * l.y + m->up * l.z; }
    // no seat is a metre off the car's middle, and an open door widens what is drawn: CJ held against the bonnet rode, the
    // partner sat in the air by the other wing (PROLOG3, 07.11)
    bool Holds(const scene_game::CVector& p) const {
        return std::fabs(p.x) < 1.0f && p.x > lo.x + 0.3f && p.x < hi.x - 0.3f && p.y > lo.y + 0.6f && p.y < hi.y - 0.6f && p.z > lo.z && p.z < hi.z;
    }
};
void ExtendCabin(const scene_game::CVector& a, const scene_game::CVector& b, const scene_game::CVector& c, void* user) {
    auto* r = (Cabin*)user;
    for (const scene_game::CVector* v : {&a, &b, &c}) {
        scene_game::CVector l = r->Local(*v);
        r->lo = {std::min(r->lo.x, l.x), std::min(r->lo.y, l.y), std::min(r->lo.z, l.z)};
        r->hi = {std::max(r->hi.x, l.x), std::max(r->hi.y, l.y), std::max(r->hi.z, l.z)};
    }
}
void ExtendRoof(const scene_game::CVector& a, const scene_game::CVector& b, const scene_game::CVector& c, void* user) {
    auto* r = (Cabin*)user;
    for (const scene_game::CVector* v : {&a, &b, &c})
        if (scene_game::CVector l = r->Local(*v); l.z > r->hi.z - 0.15f) r->roof0 = std::min(r->roof0, l.y), r->roof1 = std::max(r->roof1, l.y);
}

struct Ride {
    bool seated = false;  // CJ sits in a car of the scene
    bool drives = false;  // and it drives off within the shot
};

// CJ rides in a car of the scene: the partner rides along in a seat nobody of the scene sits in, the other one of CJ's
// row first. Hips within a seat's width (0.55 m) of a seat take it. As many in it as the car has seats (four in the
// four-door car of SWEET2B): no ride, he sat in somebody.
bool InCar(const Scene& sc, Spot& out, Ride& ride) {
    scene_game::CEntity* cjEntity = CutsceneCj();
    for (size_t k = 0; k < sc.cars.size(); ++k) {
        const Car& c = sc.cars[k];
        scene_game::CEntity* e = c.index >= 0 ? CutsceneObject(c.index) : nullptr;
        if (!e || !e->Matrix() || !cjEntity || !c.Covers(sc.cj, c.pos, 0.0f)) continue;
        int vehicle = VehicleOf(e), seats = SeatsOf(vehicle);
        if (!seats) {
            LOGD("Cutscene object %d (model %d) is no car with seats (vehicle %d)", c.index, (int)e->ModelIndex(), vehicle);
            continue;
        }
        Cabin cab{e->Matrix()};
        ForEachTriangle(e, &ExtendCabin, &cab);
        ForEachTriangle(e, &ExtendRoof, &cab);
        // the hips, not the root: CJ's root wanders as he turns in his seat to talk
        scene_game::CVector cj = cab.Local(HipsOf(cjEntity));
        if (!cab.Holds(cj)) {
            LOGD("Cutscene car %d: CJ by it, not in it (%.2f,%.2f,%.2f of %.2f..%.2f, %.2f..%.2f)", vehicle, cj.x, cj.y, cj.z,
                 cab.lo.x, cab.hi.x, cab.lo.y, cab.hi.y);
            continue;
        }
        ride.seated = true;
        for (const Sample& s : g_ahead) ride.drives = ride.drives || (s.cars[k] - c.pos).Length2D() > 2.0f;
        std::vector<scene_game::CVector> people{cj};
        for (const Actor& a : sc.actors)
            if (scene_game::CVector p = cab.Local(HipsOf(a.e)); HasBody(a.e) && cab.Holds(p) && std::fabs(p.z - cj.z) < 1.0f) people.push_back(p);
        LOGD("Cutscene car %d: %d seats, %d people in it", vehicle, seats, (int)people.size());
        if ((int)people.size() >= seats) return false;
        auto taken = [&](const scene_game::CVector& s) {
            for (const scene_game::CVector& p : people)
                if (std::fabs(p.x - s.x) < 0.55f && std::fabs(p.y - s.y) < 0.6f) return true;
            return false;
        };
        // a seat the camera is in or right by over the shot: his shoulder across the picture (SYND_3A's close-ups of CJ
        // from the back seat)
        auto atCamera = [&](const scene_game::CVector& seat) {
            scene_game::CVector hips = cab.World(seat), head = hips + e->Matrix()->up * 0.75f;
            auto near = [&](const scene_game::CVector& cam, const scene_game::CVector& moved) {
                scene_game::CVector a = hips + moved, ab = head - hips;
                float t = std::clamp((cam - a).Dot(ab) / ab.Dot(ab), 0.0f, 1.0f);
                return (cam - (a + ab * t)).Length() < 0.9f;
            };
            if (near(sc.cam, {})) return true;
            for (const Sample& s : g_ahead)
                if (near(s.view.pos, s.cars[k] - c.pos)) return true;
            return false;
        };
        // a seat between the camera and CJ or someone of the scene: the shot looks at CJ through the empty seat by him
        // (SYND_3A, through the passenger's window), and the partner there hid CJ
        auto hidesSomeone = [&](const scene_game::CVector& seat) {
            scene_game::CVector feet = cab.World(seat) - e->Matrix()->up * 0.45f;
            Body b, p;
            auto hides = [&](const Body& who) { return b.depth < who.depth - 0.2f && Covered(who, b) > 0.3f; };
            if (Project(feet, PoseHeight(POSE_SIT), b) && OnScreen(sc, b)) {
                if (sc.cjOnScreen && hides(sc.cjBody)) return true;
                for (const Actor& a : sc.actors)
                    if (a.onScreen && hides(a.body)) return true;
            }
            for (const Sample& m : g_ahead) {
                if (!ProjectAt(m.view, feet + (m.cars[k] - c.pos), PoseHeight(POSE_SIT), b) || !OnScreen(sc, b)) continue;
                if (PersonAt(m.view, m.cj, p) && OnScreen(sc, p) && hides(p)) return true;
                for (const scene_game::CVector& a : m.actors)
                    if (PersonAt(m.view, a, p) && OnScreen(sc, p) && hides(p)) return true;
            }
            return false;
        };
        auto bad = [&](const scene_game::CVector& seat) { return atCamera(seat) || hidesSomeone(seat); };
        // the car's front (the models face either way, and on the model's own heading he sat facing the boot): the way
        // CJ in it faces, across his shoulders (bones 21 and 31), else where the drawn car reaches further beyond the
        // roof (a bonnet is longer than a boot)
        float front = cab.roof0 - cab.lo.y > cab.hi.y - cab.roof1 ? 3.14159265f : 0.0f;
        scene_game::CVector lc, rc, root = cjEntity->GetPosition();
        if (BoneAt(cjEntity, 31, lc) && BoneAt(cjEntity, 21, rc) && (lc - root).Length() < 1.5f && (rc - root).Length() < 1.5f)
            if (float f = e->Matrix()->up.Cross(rc - lc).Dot(e->Matrix()->forward); std::fabs(f) > 0.1f)
                front = f < 0.0f ? 3.14159265f : 0.0f;
        // the other seat of CJ's row, then (four seats and more) the other row. Both rows are under the roof, so the
        // other one is towards the roof's middle from CJ, whichever way the model faces (cutscene cars face either way,
        // and a bonnet is longer than a boot: a "row" taken a metre off the wrong way was on the bonnet)
        float side = std::max(std::fabs(cj.x), 0.4f), dy = cj.y > (cab.roof0 + cab.roof1) * 0.5f ? -1.1f : 1.1f;
        std::vector<scene_game::CVector> tries{{-cj.x, cj.y, cj.z}};
        if (seats > 2)
            for (float x : {-side, side}) tries.push_back({x, cj.y + dy, cj.z});
        // what is left of them goes to the next partners
        auto withOthers = [&](Spot sp) {
            sp.seats = 0;
            for (const scene_game::CVector& s : tries)
                if (sp.seats < 2 && (s - sp.ground).Length() > 0.5f && cab.Holds(s) && !taken(s) && !bad(s)) sp.seat[sp.seats++] = s;
            return sp;
        };
        // the seat he has in this car keeps for the scene (the next shot's CJ sits a little otherwise), until someone
        // of the scene sits there
        bool kept = g_haveSeat && g_seat.act == c.index && cab.Holds(g_seat.ground) && !taken(g_seat.ground);
        if (kept && !bad(g_seat.ground)) return out = withOthers(g_seat), true;
        // the seat he has keeps (one of a new shot's camera would not do: out of that shot rather than to another seat)
        bool free = kept;
        for (const scene_game::CVector& s : tries) {
            if (kept || !cab.Holds(s) || taken(s)) continue;
            free = true;
            if (!bad(s)) return g_haveSeat = true, g_seat = Spot{s, front, POSE_CAR, (uint8_t)c.index}, out = withOthers(g_seat), true;
        }
        // a free seat only where the camera is or looks through: out of this shot rather than in its way (no seat at all:
        // no ride)
        if (free) return out = Spot{{sc.cj.x, sc.cj.y, sc.floor}, 0.0f, POSE_HIDDEN, 0}, true;  // on the floor, not in the air
        return false;
    }
    return false;
}

// CJ left the car he rides in: his hips out of its room for half a second (he leans about in his seat)
bool CjGotOut(const Spot& ride) {
    static uint64_t checked = 0, outSince = 0;
    uint64_t now = NowMs();
    if (now - checked < 100) return outSince && now - outSince > 500;
    checked = now;
    scene_game::CEntity* e = ride.act < NumCutsceneObjects() ? CutsceneObject(ride.act) : nullptr;
    scene_game::CEntity* cj = CutsceneCj();
    if (!e || !e->Matrix() || !cj) return false;
    Cabin cab{e->Matrix()};
    ForEachTriangle(e, &ExtendCabin, &cab);
    if (cab.Holds(cab.Local(HipsOf(cj)))) outSince = 0;
    else if (!outSince) outSince = now;
    return outSince && now - outSince > 500;
}

