// CJ walks in this shot, or has just been walking at the cut
bool CjWalks(const Scene& sc, const Track& tr) {
    const auto& path = PathOf(tr);
    if (path.size() < 2) return false;
    scene_game::CVector feet{sc.cj.x, sc.cj.y, sc.floor}, p = feet;
    float walked = 0;  // over the last second, not across a cut of his track
    for (float s = tr.time - kPathStep; s >= tr.time - 1.0f && s >= 0; s -= kPathStep) {
        scene_game::CVector q = CjAt(path, tr.time, feet, s);
        if ((q - p).Length2D() > kPathJump) break;
        walked += (q - p).Length2D(), p = q;
    }
    for (const Sample& m : g_ahead) walked = std::max(walked, (m.cj - feet).Length2D());
    return walked >= 2.0f;  // a step or two is no walk: he stays at CJ's side (BesideCj)
}

// The walk over the whole shot: room where he goes (the side step off CJ's path too), nobody of the scene or a
// car there, and in the picture nobody covered and no bigger than the people in it.
bool WalkFits(const Scene& sc, const Track& tr, int side) {
    const auto& path = PathOf(tr);
    scene_game::CEntity* cj = CutsceneCj();
    if (path.size() < 2 || !cj || !cj->Matrix()) return false;
    scene_game::CVector feet{sc.cj.x, sc.cj.y, sc.floor}, fwd = CjFacing();
    auto clear = [&](const Walker& w, const scene_game::CVector& cjAt, const std::vector<scene_game::CVector>& actors, const std::vector<scene_game::CVector>* cars) {
        if (!g_grid.Clear(w.feet, 0.25f) || (w.feet - cjAt).Length2D() < 0.9f) return false;
        for (const scene_game::CVector& a : actors)
            if ((a - w.feet).Length2D() < 0.6f) return false;
        for (size_t k = 0; k < sc.cars.size(); ++k)
            if (sc.cars[k].Covers(w.feet, cars ? (*cars)[k] : sc.cars[k].pos, 0.5f)) return false;
        return true;
    };
    std::vector<scene_game::CVector> actors;
    for (const Actor& a : sc.actors) actors.push_back(a.feet);
    Walker w = WalkerAt(path, tr.time, feet, fwd, tr.time, side);
    Body b;
    if (!clear(w, feet, actors, nullptr) || AtCamera(sc.cam, w.feet, 1.8f)) return false;
    if (Project(w.feet, 1.8f, b) && OnScreen(sc, b) && (b.depth < 1.5f || InPeoplesWay(sc, b) >= 0)) return false;
    for (const Sample& m : g_ahead) {
        w = WalkerAt(path, tr.time, feet, fwd, tr.time + m.dt, side);
        if (!clear(w, m.cj, m.actors, &m.cars) || AtCamera(m.view.pos, w.feet, 1.8f)) return false;
        if (ProjectAt(m.view, w.feet, 1.8f, b) && OnScreen(sc, b) && (b.depth < 1.5f || InWayAt(sc, m, b, false))) return false;
    }
    return true;
}

const char* g_how = "";  // how the shot's spot came about (logs)
// He is at CJ's side up to here. A place is left only when it no longer fits, or when a shot that shows CJ does not show
// him: that move is a jump at the cut (g_forView), and a walk only follows CJ, the players' word of 03.11. Walked after
// each turn of CJ's he went about in the middle of a talk (RYDER2A, 04.11: 1.3, 0.9, 3 and 0.8 m in four shots). At most
// one such move in kViewMoveMs, or a talk cutting from one side to the other would take him along at every cut.
constexpr float kBesideFar = 3.5f, kViewMoveMs = 6000.0f;
bool g_forView = false;
float g_clock = 0.0f, g_viewMoved = -1e9f;  // the camera's clock at this cut and at the last such move
// The spot is out of the picture, no place in the scene (OutOfPicture, or kept from such): the next shot looks for one
// in the picture before it keeps him there. Chosen so at the first shot of "Cleaning the Hood" (SWEET1B), he stood out
// of the picture for all ten shots after it, seen 0% of them (28.10).
bool g_spotOut = false;

// A short way from the last spot to this one is walked, not jumped (the players' word after the eighth test build,
// 02.11: he jumped from spot to spot): both on foot (standing or leaning), on one floor, no farther than kGoMax, the line
// clear of anything drawn to bump into, with floor under it, and of the scene's people, CJ too.
constexpr float kGoMax = 8.0f, kGoPace = 1.3f;  // m, m/s
bool LegClear(const scene_game::CVector& a, const scene_game::CVector& b, const Scene& sc) {
    scene_game::CVector d = b - a;
    float len = d.Length2D();
    for (float s = 0.35f; s < len - 0.35f; s += kCell) {
        scene_game::CVector p = a + d * (s / len);
        if (!g_grid.Clear(p, 0.25f) || !g_grid.Ground(p.x, p.y)) return false;
    }
    if (SegDist2(sc.cj.x, sc.cj.y, a, b) < 0.45f * 0.45f) return false;
    for (const Actor& act : sc.actors)
        if (SegDist2(act.feet.x, act.feet.y, a, b) < 0.45f * 0.45f) return false;
    return true;
}
// A straight line blocked (a table, a sofa, someone of the scene) goes round it with one turn aside of the line, the
// shortest that is clear (the players' word, 03.11: walking in every cutscene, rooms too). None: a jump at the cut.
bool WalkWay(const Spot& from, Spot& to, const Scene& sc) {
    to.viaSet = false;
    auto onFoot = [](Pose p) { return p == POSE_STAND || p == POSE_LEAN; };
    scene_game::CVector a = from.ground, d = to.ground - a;
    float len = d.Length2D();
    if (!onFoot(from.pose) || !onFoot(to.pose) || len < 0.3f || len > kGoMax || std::fabs(d.z) > 0.4f) return false;
    if (LegClear(a, to.ground, sc)) return true;
    scene_game::CVector aside{d.y / len, -d.x / len, 0.0f};
    float best = kGoMax * 1.5f;
    for (float off : {0.8f, -0.8f, 1.4f, -1.4f, 2.0f, -2.0f})
        for (float along : {0.5f, 0.3f, 0.7f}) {
            scene_game::CVector v = a + d * along + aside * off;
            float way = (v - a).Length2D() + (to.ground - v).Length2D();
            if (way >= best || !g_grid.Clear(v, 0.25f) || !g_grid.Ground(v.x, v.y) || !LegClear(a, v, sc) || !LegClear(v, to.ground, sc))
                continue;
            best = way, to.via = v, to.viaSet = true;
        }
    return to.viaSet;
}

// ---- the plan of a stop. Chosen shot by shot he knew nothing of the shots to come, and a place good for one was out of the
// next: 14 of 26 shots did not show him, and he moved in the middle of a talk (04.11). The cutscene's files hold it all
// ahead: the camera's spline and everybody's root track run to its end. So while CJ stays about one place (a stop) the
// places are chosen for all its shots at once, the most of it seen with the fewest moves: a move costs less across a cut that
// does not show him on one side of it, and much where he is seen before and after. The places lie at CJ's side and a
// little behind him (the players' word of 03.11), and a moment in front of him counts as one in the way. All round him, with
// a move at 0.5 s, he went from one side of CJ to the other at every other cut, 9 places in 100 s of INTRO1A (05.11).
constexpr float kStopReach = 3.0f;    // CJ this far from where the stop began: another stop
constexpr float kMoveHidden = 4.0f;   // what a move costs, in seconds of him seen
constexpr float kMoveSeen = 8.0f;     // and seen on both sides of its cut, plus a second a metre
constexpr float kStayBonus = 4.0f;    // no first move
constexpr float kFar = 0.02f;         // the share of a shot a metre from CJ's side costs
constexpr float kOffAbreast = 0.03f;  // and the share a quarter turn off abreast of him costs
constexpr float kOtherSide = 0.05f;   // and the other side of him than the scene's
constexpr size_t kPlanShots = 64;
struct PlanShot {
    float from = 0, to = 0;
    std::vector<Sample> samples;
};
struct PlanStep {
    float from, to;
    Spot spot;
};
std::vector<PlanStep> g_plan;
size_t g_planMoves = 0;

// A place over a shot: no good (somebody at it, the camera in it, in the way of the people for over a quarter of the shot),
// else how many moments the shot shows him, less those he is in somebody's way
bool RateShot(const Scene& all, const scene_game::CVector& feet, const PlanShot& shot, int& seen) {
    int way = 0;
    seen = 0;
    for (const Sample& m : shot.samples) {
        if ((m.cj - feet).Length2D() < 1.0f || AtCamera(m.view.pos, feet, 1.8f)) return false;
        for (const scene_game::CVector& a : m.actors)
            if ((a - feet).Length2D() < 0.75f) return false;
        for (size_t k = 0; k < all.cars.size(); ++k)
            if (all.cars[k].Covers(feet, m.cars[k], 0.6f)) return false;
        if (InFrontOf(m.cj, m.cjFwd, feet)) {
            ++way;
            continue;
        }
        Body b;
        if (!ProjectAt(m.view, feet, 1.8f, b) || !OnScreen(all, b)) continue;
        if (InWayAt(all, m, b, true)) ++way;
        else if (Shown(all, b) && GridSeen(m.view.pos, feet + scene_game::CVector{0, 0, 1.1f})) ++seen;
    }
    seen -= way;
    return way * 4 <= (int)shot.samples.size();
}

void BuildPlan(const Scene& sc, const Spot* from) {
    g_plan.clear();
    scene_game::CEntity* cj = CutsceneCj();
    CameraPath path;
    float now = 0, fovNow = 0;
    View cur;
    Track cjTrack;
    if (!cj || !CameraNow(sc, path, now, fovNow, cur) || !TrackOf(cj, cjTrack)) return;
    // everybody of the scene, those who come in later too
    Scene all = sc;
    all.actors.clear();
    all.cars.clear();
    for (int i = 0; i < NumCutsceneObjects(); ++i) {
        scene_game::CEntity* o = CutsceneObject(i);
        if (!o || o == cj) continue;
        Car c{};
        if (IsPerson(o)) {
            scene_game::CVector p = o->GetPosition();
            all.actors.push_back({o, {p.x, p.y, sc.floor}, Body{}, false});
        } else if (IsCar(o, c.b)) {
            c.index = i, c.pos = o->GetPosition();
            TrackOf(o, c.track);
            all.cars.push_back(c);
        }
    }
    std::vector<Track> tracks(all.actors.size());
    for (size_t i = 0; i < all.actors.size(); ++i) TrackOf(all.actors[i].e, tracks[i]);
    // the shots of the stop: up to one where CJ walks off or is out of reach
    scene_game::CVector start{sc.cj.x, sc.cj.y, sc.floor}, centre{}, facing{};
    std::vector<PlanShot> shots;
    int moments = 0;
    for (float t = now; shots.size() < kPlanShots;) {
        float end = 0, next = 0;
        ShotEnd(path, t, end, next);
        PlanShot shot;
        shot.from = t, shot.to = next;
        int steps = std::clamp((int)((end - t) / 500.0f), 1, 16);
        bool leaves = false;
        for (int i = 0; i <= steps && !leaves; ++i) {
            float at = t + (end - 30.0f - t) * (float)i / (float)steps;
            Sample m;
            m.dt = (std::max(at, t) - now) / 1000.0f;
            float f = fovNow;
            path.At(now + m.dt * 1000.0f, m.view, f);
            float zoom = std::tan(fovNow * 0.00872665f) / std::tan(std::clamp(f, 5.0f, 170.0f) * 0.00872665f);  // tan(fov/2)
            m.view.kx = cur.kx * zoom, m.view.ky = cur.ky * zoom, m.view.cx = cur.cx, m.view.cy = cur.cy;
            m.cj = FeetLater(cjTrack, start, m.dt);
            m.cjFwd = FacingAt(cjTrack.seq, cjTrack.time + m.dt);
            for (size_t k = 0; k < all.actors.size(); ++k) m.actors.push_back(FeetLater(tracks[k], all.actors[k].feet, m.dt));
            for (const Car& c : all.cars) m.cars.push_back(FeetLater(c.track, c.pos, m.dt));
            leaves = (m.cj - start).Length2D() > kStopReach ||
                     (!shot.samples.empty() && (m.cj - shot.samples.front().cj).Length2D() >= 2.0f);
            shot.samples.push_back(std::move(m));
        }
        if (leaves) break;
        for (const Sample& m : shot.samples) centre = centre + m.cj, facing = facing + m.cjFwd, ++moments;
        shots.push_back(std::move(shot));
        if (next <= t || next >= path.pos.Time(path.pos.n - 1)) break;
        t = next;
    }
    if (shots.empty() || !moments) return;
    centre = centre * (1.0f / (float)moments);
    centre.z = sc.floor;
    // CJ turning right round in the stop has no way he faces on the whole: his first
    facing = facing.Length2D() < 0.2f * (float)moments ? shots.front().samples.front().cjFwd : Flat(facing);
    scene_game::CVector right{facing.y, -facing.x, 0.0f};
    // the people he stands with, to face them
    scene_game::CVector group = centre;
    int with = 1;
    for (const scene_game::CVector& a : shots.front().samples.front().actors)
        if ((a - start).Length2D() < 4.0f) group = group + a, ++with;
    group = with > 1 ? group * (1.0f / (float)with) : centre + facing * 3.0f;  // nobody: the way CJ looks
    // the places: at the side of where CJ stays and a little behind (deg from his front), and the one he stands at
    BuildReach(sc.cj);
    std::vector<Spot> places;
    std::vector<float> cost;  // of a second there
    std::vector<int> sides;
    for (float r : {1.5f, 2.0f, 2.6f})
        for (float deg : {90.0f, 75.0f, 110.0f, 60.0f, 130.0f, 150.0f})
            for (int side : {1, -1}) {
                float a = deg * 0.0174533f;
                Spot s{centre + right * ((float)side * r * std::sin(a)) + facing * (r * std::cos(a)), 0, POSE_STAND, (uint8_t)(g_seed % 4)};
                s.ground.z = sc.floor;
                if (NoRoom(sc, s) >= 0 || !Reachable(s.ground)) continue;
                s.heading = HeadingOf(Flat(group - s.ground));
                places.push_back(s), sides.push_back(side);
                cost.push_back(kFar * r + kOffAbreast * std::fabs(deg - 90.0f) / 90.0f + (g_side && side != g_side ? kOtherSide : 0.0f));
            }
    // Where he stands, if that is by this stop: taken from anywhere and with its bonus, he stayed under the floor for 28 s of
    // SWEET3A after CJ had gone up (CJ waits there for his part), and 7 m across the room in INTRO1A (05.11).
    bool stay = from && from->pose == POSE_STAND && std::fabs(from->ground.z - sc.floor) <= 1.0f &&
                (from->ground - centre).Length2D() <= kBesideFar;
    if (stay) places.push_back(*from), cost.push_back(0.0f), sides.push_back(0);
    size_t n = places.size(), k = shots.size();
    if (!n) return;
    // over all the shots: the best way through, a place a shot refuses being no way
    constexpr float kNo = -1e9f;
    std::vector<float> value(n * k), best(n), step(n);
    std::vector<uint8_t> shows(n * k);
    std::vector<int16_t> came(n * k, -1);
    for (size_t s = 0; s < k; ++s) {
        float seconds = (shots[s].to - shots[s].from) / 1000.0f, share = seconds / (float)shots[s].samples.size();
        for (size_t c = 0; c < n; ++c) {
            int seen = 0;
            bool fits = RateShot(all, places[c].ground, shots[s], seen);
            value[s * n + c] = fits ? share * (float)seen - seconds * cost[c] : kNo;
            shows[s * n + c] = seen > 0;
        }
    }
    for (size_t c = 0; c < n; ++c) best[c] = value[c];
    if (stay && best[n - 1] > kNo) best[n - 1] += kStayBonus;
    for (size_t s = 1; s < k; ++s) {
        for (size_t c = 0; c < n; ++c) {
            step[c] = kNo;
            if (value[s * n + c] <= kNo) continue;
            float top = best[c];
            int16_t was = (int16_t)c;
            for (size_t p = 0; p < n; ++p) {
                if (p == c || best[p] <= kNo) continue;
                float cost = kMoveHidden;
                if (shows[(s - 1) * n + p] && shows[s * n + c]) cost += kMoveSeen + (places[p].ground - places[c].ground).Length2D();
                if (best[p] - cost > top) top = best[p] - cost, was = (int16_t)p;
            }
            if (top > kNo) step[c] = top + value[s * n + c], came[s * n + c] = was;
        }
        best = step;
    }
    size_t at = (size_t)(std::max_element(best.begin(), best.end()) - best.begin());
    if (best[at] <= kNo) return;  // no way through all of it: shot by shot, as without a plan
    g_plan.resize(k);
    g_planMoves = 0;
    for (size_t s = k; s-- > 0;) {
        g_plan[s] = {shots[s].from, shots[s].to, places[at]};
        if (s) {
            if ((size_t)came[s * n + at] != at) ++g_planMoves;
            at = (size_t)came[s * n + at];
        }
    }
    if (!g_side) g_side = sides[at];
    LOGI("Cutscene %s: plan of %zu shots, %zu places tried, %zu moves", g_cutscene, k, n, g_planMoves);
}

// This shot's place of the plan, a new plan once the last has run out
bool PlanSpot(const Scene& sc, const Spot* from, Spot& out) {
    auto step = [] {
        for (const PlanStep& s : g_plan)
            if (g_clock >= s.from - 50.0f && g_clock < s.to - 50.0f) return &s;
        return (const PlanStep*)nullptr;
    };
    const PlanStep* s = step();
    bool same = s != nullptr;
    if (!s) BuildPlan(sc, from), s = step();
    if (!s) return false;
    out = s->spot;
    g_forView = same && from && (out.ground - from->ground).Length2D() > 0.3f;  // a move of the plan: at the cut
    return true;
}

Spot Choose(const Scene& sc) {
    g_staysNo = "";
    g_forView = false;
    const Places* own = PlacesOf(g_cutscene);
    Ride ride;
    if (!(own && own->hidden)) {  // CJ rides: in his car with him
        Spot seat;
        g_how = " (rides)";
        if (sc.cjInCar && InCar(sc, seat, ride)) return seat;
    }
    // no seat for him in CJ's car: he stands by it, or, if it drives off, where he was or out of the picture (a new
    // place by the kerb every shot along the road would show him there again and again). A seat of before is no place.
    bool stand = !(own && own->hidden), may = stand && !ride.drives;
    // the scene's way, where there is one (not at places chosen by hand)
    if (stand && !(own && !own->spots.empty()) && MocapWalkCount() && MocapCount() > 1) {
        Track now;
        Walker w;
        const Way* way = WayOf(g_cutscene);
        if (way && TrackOf(CutsceneCj(), now) && WayAt(*way, now.time, w)) {
            g_how = " (beside, way)";
            return Spot{w.feet, w.heading, POSE_WAY, 0};
        }
    }
    bool before = stand && g_haveSpot && g_spot.pose != POSE_HIDDEN && g_spot.pose != POSE_CAR;
    Track tr;
    bool track = TrackOf(CutsceneCj(), tr) && PathOf(tr).size() > 1;
    scene_game::CVector cjFwd = CjFacing();
    // where he is now: a walk has got on, it stands there if this shot is no walk
    Spot prev = g_spot;
    if (before && prev.pose == POSE_WALK && track) {
        Walker w = WalkerAt(PathOf(tr), tr.time, scene_game::CVector{sc.cj.x, sc.cj.y, sc.floor}, cjFwd, tr.time, (int)prev.act - 1);
        prev = Spot{w.feet, w.heading, POSE_STAND, 0};  // the clips, not a cigarette out of nowhere
    }
    // CJ walks: the partner goes along, on his side of CJ if he was walking already, unless that is a jump the
    // shot shows
    if (may && !(own && !own->spots.empty()) && track && MocapWalkCount() && MocapCount() > 1 && CjWalks(sc, tr)) {
        int first = before && g_spot.pose == POSE_WALK ? (int)g_spot.act - 1 : g_side ? g_side : 1;
        for (int side : {first, first ? -first : 1, first ? 0 : -1}) {
            if (!WalkFits(sc, tr, side)) continue;
            Walker w = WalkerAt(PathOf(tr), tr.time, scene_game::CVector{sc.cj.x, sc.cj.y, sc.floor}, cjFwd, tr.time, side);
            if (before && (prev.ground - w.feet).Length2D() > 1.0f && Shows(sc, prev, false)) break;
            g_how = " (walks)";
            return Spot{w.feet, w.heading, POSE_WALK, (uint8_t)(side + 1)};
        }
    }
    // at CJ's side: by the plan of this stop, else shot by shot (BesideCj)
    if (may && !(own && !own->spots.empty())) {
        Spot planned;
        g_how = " (beside, plan)";
        if (PlanSpot(sc, before ? &prev : nullptr, planned)) return planned;
        bool keep = before && (BodyOf(prev) - sc.cj).Length2D() <= kBesideFar && Stays(sc, prev);
        bool seen = CjShare(sc) >= 0.3f;
        bool view = keep && seen && g_clock - g_viewMoved >= kViewMoveMs && SeenShare(sc, prev) < 0.2f;
        g_how = " (beside, stays)";
        if (keep && !view) return prev;
        Spot side;
        if (BesideCj(sc, before ? &prev : nullptr, seen, side)) {
            if (view && SeenShare(sc, side) < 0.3f) return prev;  // no place the shot shows
            g_how = " (beside)";
            if (view) g_forView = true, g_viewMoved = g_clock;
            return side;
        }
        if (keep) return prev;
    }
    // A shot keeps him where he stood whenever it can, seen in it or not. Kept only where half of the next shot showed
    // him, he went where each shot did: from the wall to Sweet and on to the back of the room at every cut ("второй
    // Сиджей постоянно телепортируется", 28.10; the players chose one place for the whole scene).
    // Only where the shot shows him at all: kept where this shot never does, he was out of the picture for most of a
    // scene (29.10, SWEET1B: 8 of 12 shots unseen at the place he stayed at), and a place in the picture comes first.
    // Moved only then, he does not jump about in front of the camera: where he stood is not in this shot.
    // Kept where the shot shows him for a moment only, he was gone from the picture for most of it ("пропадает
    // моментами", 31.10): the place must be in it for a fifth of the shot. Half the shot made him change places almost
    // every shot, as if he teleported (31.10, the second test).
    g_how = " (stays)";
    static char seenThere[40];
    if (before && !g_spotOut && Stays(sc, prev)) {
        float seen = SeenShare(sc, prev);
        if (seen >= 0.2f) return prev;
        snprintf(seenThere, sizeof seenThere, "seen %.0f%% of the shot there", seen * 100.0f);
        g_staysNo = seenThere;
    }
    g_fromOk = before && (BodyOf(prev) - sc.cj).Length2D() < 25.0f && std::fabs(prev.ground.z - sc.floor) < 1.0f;
    g_from = prev.ground;
    auto offer = [&](Best& to) {
        if (own && !own->spots.empty()) {
            for (const Spot& sp : own->spots) to.Offer(sc, sp);
        } else {
            OfferAttractors(sc, to);
            OfferSeats(sc, to);
            OfferFloor(sc, to);
        }
    };
    Best best;
    if (may) offer(best);
    g_how = "";
    if (best.score > kRejected) return best.spot;
    // Nothing in the picture fits this shot well (a close-up, a cramped room): a looser place the camera still shows
    // first (cut by its edge, half behind something). Kept where he was or out of the picture before that, he was seen
    // 8% of the time in 13 hard cutscenes of the review (37% with this and the rules above).
    Best loose;
    loose.rules = LOOSE;
    if (may) offer(loose);
    g_how = " (loose)";
    if (loose.score > kRejected && Shows(sc, loose.spot, false)) return loose.spot;
    // None either: the last try in the picture. Without it he stood out of the picture or where the shot did not show
    // him through 21 of 47 shots of a test, the most of "Nines and AKs"'s first cutscene (31.10, "вовсе не виден"); the
    // players chose him seen in every shot.
    Best last;
    last.rules = SEEN;
    BuildReach(sc.cj);
    if (may) offer(last);
    g_how = " (last try)";
    if (last.score > kRejected) return last.spot;
    // Out of the picture in the shot before as well: once more, through narrower gaps and seen for a moment of the shot
    // at least. The talk at Sweet's in "Nines and AKs" had every try fail five shots on end, and he stood where the
    // camera never looked for half a minute ("практически не видно", 31.10, the fourth test).
    if (may && !g_seenBefore) {
        Best any;
        any.rules = SEEN;
        BuildReach(sc.cj, 1);
        g_lastChance = true;
        offer(any);
        g_lastChance = false;
        g_how = " (seen at last)";
        if (any.score > kRejected) return any.spot;
    }
    // then he stays where he was, out of the picture now, or stands by out of the picture (not at the places chosen by
    // hand: those alone)
    g_how = " (stays)";
    if (before && Stays(sc, prev)) return prev;
    Spot out;
    g_how = " (out of the picture)";
    if (stand && !(own && !own->spots.empty()) && OutOfPicture(sc, out)) return out;
    // the camera takes in all around CJ: in the picture anyway
    g_how = " (loose)";
    if (loose.score > kRejected) return loose.spot;
    // CJ in a car, or no room near him at all: where we were if the camera does not see that place, otherwise
    // out of the cutscene for this shot
    g_how = "";
    if (before && !Shows(sc, prev)) return prev;
    Spot near;
    g_how = " (beside)";
    if (may && !(own && !own->spots.empty()) && Beside(sc, near)) return near;
    g_how = "";
    Spot hidden = before ? prev : Spot{{sc.cj.x, sc.cj.y, sc.floor}, 0, POSE_STAND, 0};  // on the floor, not in the air
    hidden.pose = POSE_HIDDEN;
    return hidden;
}

bool BuildScene(Scene& sc) {
    scene_game::CEntity* cj = CutsceneCj();
    scene_game::CMatrix* cam = scene_game::Ref<scene_game::CMatrix*>(kCameraMatrix);
    if (!cj || !cj->Matrix() || !cam) return false;
    sc.cj = cj->GetPosition();
    sc.who = cj;
    sc.cam = cam->pos;
    sc.camFwd = cam->forward;
    sc.fov = cam::Fov();
    float z;
    sc.floor = Floor(sc.cj.x, sc.cj.y, sc.cj.z + 0.2f, z) && sc.cj.z - z < 2.0f ? z : sc.cj.z - 1.0f;
    sc.w = (float)ScreenWidth();
    sc.h = (float)ScreenHeight();
    g_now = View{sc.cam, cam->forward, cam->right, cam->up};
    g_nowOk = Calibrate(g_now);
    sc.cjOnScreen = ProjectSkeleton(cj, sc.cjBody) || ProjectActor(scene_game::CVector{sc.cj.x, sc.cj.y, sc.floor}, sc.cjBody);
    for (int i = 0; i < NumCutsceneObjects(); ++i) {
        scene_game::CEntity* o = CutsceneObject(i);
        // the people of the scene within 20 m (they may walk up), not those hidden until their part comes
        if (!o || o == cj || !(o->Flags() & 0x80) || (o->GetPosition() - sc.cj).Length() > 20.0f || !IsPerson(o)) continue;
        Actor a;
        a.e = o;
        a.feet = o->GetPosition();
        a.feet.z = sc.floor;
        a.onScreen = ProjectSkeleton(o, a.body) || ProjectActor(a.feet, a.body);
        sc.actors.push_back(a);
    }
    for (int i = 0; i < NumCutsceneObjects(); ++i) {
        scene_game::CEntity* o = CutsceneObject(i);
        Car c{};
        if (!o || o == cj || !(o->Flags() & 0x80) || !IsCar(o, c.b)) continue;
        c.index = i;
        c.pos = o->GetPosition();
        TrackOf(o, c.track);
        sc.cjInCar = sc.cjInCar || c.Covers(sc.cj, c.pos, 0.0f);
        sc.cars.push_back(c);
    }
    return true;
}

// A shot about people away from CJ: CJ not in its picture now or later (or out of sight, under a floor), somebody of
// the scene in it on another floor or 8 m off. "Nines and AKs" keeps CJ under the floor of Sweet's house for its first
// 10 s while the camera shows the others upstairs: every place by CJ was refused, and the partner was nowhere to be
// seen (31.10). The scene is then taken around the one of them nearest the middle of the picture.
bool Elsewhere(const Scene& sc, Scene& around) {
    if (sc.cjInCar) return false;
    bool cj = sc.cjOnScreen && Shown(sc, sc.cjBody) && Seen(sc.cam, scene_game::CVector{sc.cj.x, sc.cj.y, sc.floor + 1.1f});
    Body b;
    for (const Sample& m : g_ahead)
        cj = cj || (PersonAt(m.view, m.cj, b) && Shown(sc, b) && Seen(m.view.pos, m.cj + scene_game::CVector{0, 0, 1.1f}));
    if (cj) return false;
    const Actor* who = nullptr;
    float floor = 0;
    for (const Actor& a : sc.actors) {
        if (!a.onScreen || !Shown(sc, a.body)) continue;
        scene_game::CVector p = a.e->GetPosition();
        float z, f = Floor(p.x, p.y, p.z + 0.2f, z) && p.z - z < 2.0f ? z : p.z - 1.0f;
        if (std::fabs(f - sc.floor) < 1.5f && (p - sc.cj).Length2D() < 8.0f) continue;
        if (!who || std::fabs(a.body.x - sc.w * 0.5f) < std::fabs(who->body.x - sc.w * 0.5f)) who = &a, floor = f;
    }
    if (!who) return false;
    around = sc;
    around.who = who->e;
    around.cj = who->e->GetPosition();
    around.floor = floor;
    around.cjBody = who->body;
    around.cjOnScreen = true;
    around.actors.clear();
    if (std::fabs(sc.floor - floor) < 1.5f) around.actors.push_back({sc.who, scene_game::CVector{sc.cj.x, sc.cj.y, floor}, sc.cjBody, sc.cjOnScreen});
    for (const Actor& a : sc.actors)
        if (&a != who) around.actors.push_back({a.e, scene_game::CVector{a.feet.x, a.feet.y, floor}, a.body, a.onScreen});
    return true;
}

// A place among the people of such a shot, by the same rules as by CJ (Choose), where he was if the shot shows him there.
bool ChooseAmong(const Scene& sc, Spot& out) {
    const Places* own = PlacesOf(g_cutscene);
    if (own && (own->hidden || !own->spots.empty())) return false;  // the places chosen by hand alone
    const Spot& prev = g_spot;
    bool before = g_haveSpot && prev.pose != POSE_HIDDEN && prev.pose != POSE_CAR && prev.pose != POSE_WALK;
    g_fromOk = before && (BodyOf(prev) - sc.cj).Length2D() < 25.0f && std::fabs(prev.ground.z - sc.floor) < 1.0f;
    g_from = prev.ground;
    g_how = " (stays, the shot elsewhere)";
    if (before && Stays(sc, prev) && SeenShare(sc, prev) >= 0.5f) {
        out = prev;
        return true;
    }
    g_how = " (the shot elsewhere)";
    for (int pass = 0; pass < 4 && !(pass == 3 && g_seenBefore); ++pass) {  // the fourth one as in Choose
        Best best;
        best.rules = pass == 0 ? STRICT : pass == 1 ? LOOSE : SEEN;
        if (pass >= 2) BuildReach(sc.cj, pass == 3 ? 1 : 2);
        g_lastChance = pass == 3;
        OfferAttractors(sc, best);
        OfferSeats(sc, best);
        OfferFloor(sc, best);
        g_lastChance = false;
        if (best.score > kRejected && SeenShare(sc, best.spot) > 0.0f) {
            out = best.spot;
            return true;
        }
    }
    return false;
}

