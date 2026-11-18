void EaseHeading(Held& h, float want, float ease) {
    float dt = TimeStep() / 50.0f;
    if (dt <= 0.0f) return;
    float omega = 2.0f / ease, x = omega * dt, decay = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    float full = std::remainder(want - h.heading, 6.2831853f), off = std::clamp(full, -kMostTurn * ease, kMostTurn * ease);
    float temp = (h.turnRate - omega * off) * dt;
    h.turnRate = (h.turnRate - omega * temp) * decay;
    h.heading += off + (temp - off) * decay;
}

// In and out of the walk and the turn: a quarter of a second was a jerk at every start and stop (06.11)
constexpr float kStepBlend = 2.5f;  // 1 / s
// cutscene-partner-smooth (12.11, "movimientos roboticos" in Wrong Side of the Tracks): the walker's speed is a difference of
// the path over 0.15 s and the way's keys change it in steps, and the cadence followed it every 100 ms, so the steps jerked
// and the walk / run clips swapped at the edge of their thresholds. Now the speed is eased (kPaceEase s), the walk and the
// run keep at least kGaitHold ms each, the cadence is set every frame, and a clip given as he is first put there comes in
// in kFreshBlend (the blend of 2.5 /s from an empty skeleton is a stiff pose for 0.4 s).
constexpr float kPaceEase = 0.25f, kFreshBlend = 8.0f;
constexpr uint64_t kGaitHold = 700;
const char* TurnClip(int side) { return side > 0 ? "Turn_L" : "Turn_R"; }

// Walking along with CJ (both games: the path comes from their own CJ): put on it every frame, stepping on the spot
// with CJ's walk from the cutscenes as fast as the path goes, the clips when CJ stops. Turning on the spot he takes the small
// steps the game's own people take (PED/Turn_L, Turn_R: they keep the root as it is, his heading comes from here). The walk played
// on the spot for it looked like treading (06.11).
void HoldMoving(scene_game::CPed* ped, Held& h, const Spot& s, bool start, scene_game::CEntity* cj, const Walker& w, float stepsFrom, float stepsTo, const SceneAct* act = nullptr) {
    if (start) {
        if (ped->InVehicle()) Cmd(0x0362).I(h.handle).F(w.feet.x).F(w.feet.y).F(w.feet.z + 1.0f).Run();  // WARP_CHAR_FROM_CAR_TO_COORD
        Cmd(0x0792).I(h.handle).Run();  // CLEAR_CHAR_TASKS_IMMEDIATELY: the previous act stops, and what he did before the scene
        Cmd(0x04D7).I(h.handle).I(1).Run();  // FREEZE_CHAR_POSITION: moved by hand, not by physics (sets lack collision)
        if (h.prop >= 0) Cmd(0x0108).I(h.prop).Run(), h.prop = -1;  // DELETE_OBJECT
        h.seed = Mix(Mix((uint32_t)std::lround(s.ground.x * 100.0f), (uint32_t)std::lround(s.ground.y * 100.0f)), POSE_WALK * 8u + s.act);
        h.stage = 0, h.walking = h.running = false, h.turning = 0, h.clip = nullptr, h.clips = 0, h.fidget = nullptr, h.slot = ~0u, h.loopClock = -1, h.act = nullptr;
        h.heading = w.heading, h.turnRate = 0;
        h.pace = w.speed, h.gaitAt = 0, h.fresh = true;
    }
    h.spot = s, h.placed = true;
    const bool smooth = CConfigLoader::ms_bCutscenePartnerSmooth;
    float speed = w.speed;
    if (smooth) {
        float dt = TimeStep() / 50.0f;
        if ((w.feet - ped->GetPosition()).Length2D() > 0.7f || dt > 0.5f) h.pace = w.speed;  // a cut: no easing across it
        else h.pace += (w.speed - h.pace) * (1.0f - std::exp(-dt / kPaceEase));
        speed = h.pace;
    }
    bool moving = w.speed > (h.walking ? stepsTo : stepsFrom);
    bool running = moving && speed > (h.running ? kRunTo : kRunFrom);
    if (smooth && moving && h.walking && running != h.running && NowMs() - h.gaitAt < kGaitHold) running = h.running;
    // put somewhere else (a jump of the way, on a cut): he stands there facing as he should, he does not spin round in the shot
    if ((w.feet - ped->GetPosition()).Length2D() > 0.7f) h.heading = w.heading, h.turnRate = 0;
    EaseHeading(h, w.heading, moving ? kEaseWalk : kEaseStand);
    if (scene_game::CMatrix* m = ped->Matrix()) {
        float c = std::cos(h.heading), sn = std::sin(h.heading);
        m->right = {c, sn, 0}, m->forward = {-sn, c, 0}, m->up = {0, 0, 1};
    }
    SetEntityPosition(ped, w.feet + scene_game::CVector{0, 0, scene_game::CallMethod<0x536BE0, float>(ped)});  // game::CEntity::GetDistanceFromCentreOfMassToBaseOfModel
    ped->CurrentRotation() = ped->AimingRotation() = h.heading;
    ped->PedFlags() = (ped->PedFlags() | 0x3u) & ~0x200u;  // bIsStanding, bWasStanding, not bIsInTheAir
    ped->Flags() |= 0x80;
    scene_game::Field<float>(ped, 0x12C) = scene_game::Field<float>(cj, 0x12C);  // lit like CJ
    // the small steps until the eased heading has got there
    int turning = moving ? 0 : w.turning ? w.turning : std::fabs(std::remainder(w.heading - h.heading, 6.2831853f)) > 0.1f ? h.turning : 0;
    if (moving || turning || (act && !BlockReady(act->block))) act = nullptr;
    if (h.stage == 0) {
        // The task given in the frame he is first put here did not take in any of the 67 scenes of 05.11: the clip was gone at
        // the first check, and another started half a second in. Hold gives it a frame later and it takes (why is not known).
        h.stage = 1;
    } else if (h.stage == 1 || moving != h.walking || running != h.running || turning != h.turning || act != h.act) {
        bool stepped = h.walking || h.turning;
        h.walking = moving, h.running = running, h.turning = turning, h.stage = 2, h.act = act;
        h.gaitAt = NowMs();
        if (moving) {
            if (running) h.walk = kRunClip, h.walkSpeed = kRunSpeed;
            else h.walk = MocapWalk(kGait), h.walkSpeed = MocapWalkSpeed(kGait);
            h.speedAt = 0;
            Cmd(0x0605).I(h.handle).S(h.walk).S(running ? "PED" : kMocapBlock).F(smooth && h.fresh ? kFreshBlend : kStepBlend).I(1).I(0).I(0).I(0).I(-1).Run();  // TASK_PLAY_ANIM, looped
            h.checkedAt = NowMs();
            LOGD("Cutscene partner %s: %s (%.2f m/s)", running ? "runs" : "walks", h.walk, w.speed);
        } else if (turning) {
            Cmd(0x0605).I(h.handle).S(TurnClip(turning)).S("PED").F(kStepBlend).I(1).I(0).I(0).I(0).I(-1).Run();  // TASK_PLAY_ANIM, looped
            h.checkedAt = NowMs();
            LOGD("Cutscene partner turns: %s", TurnClip(turning));
        } else if (act) {
            Cmd(0x0605).I(h.handle).S(act->anim).S(act->block).F(4.0f).I(act->loop).I(0).I(0).I(!act->loop).I(-1).Run();  // TASK_PLAY_ANIM
            h.clip = act->anim, h.checkedAt = NowMs();
            LOGD("Cutscene partner: %s/%s by the scene", act->block, act->anim);
        } else {
            NextClip(h, smooth && h.fresh ? kFreshBlend : stepped ? kStepBlend : 1.0f / kClipBlend);  // out of a walk soon, or he treads on the spot
        }
    }
    uint64_t now = NowMs();
    if (h.walking && (smooth || now - h.speedAt > 100)) {  // the steps as fast as the path goes
        h.speedAt = now;
        Cmd(0x0393).I(h.handle).S(h.walk).F(h.running ? std::clamp(speed / h.walkSpeed, 0.7f, 1.5f) : std::clamp(speed / h.walkSpeed, 0.2f, 1.8f)).Run();  // SET_CHAR_ANIM_SPEED
    }
    if (h.stage == 2) h.fresh = false;
    Live(ped, h, &kClips);
    // Anything may end the animation: it starts again. The time is read after Live: its next clip set checkedAt past an older
    // `now`, the difference came out huge, and the clip just given was found not playing and swapped a frame later (05.11).
    if (uint64_t at = NowMs(); h.stage == 2 && at - h.checkedAt > (h.clips > 1 ? 500u : 150u)) {
        h.checkedAt = at;
        if (!Cmd(0x0611).I(h.handle).S(h.walking ? h.walk : h.turning ? TurnClip(h.turning) : AnimNow(h, &kClips)).Run()) h.stage = 0;  // IS_CHAR_PLAYING_ANIM
    }
}

void HoldWalk(scene_game::CPed* ped, Held& h, const Spot& s, bool start) {
    scene_game::CEntity* cj = CutsceneCj();
    Track tr;
    if (!cj || !cj->Matrix() || !TrackOf(cj, tr) || PathOf(tr).size() < 2) return;  // stays where it was
    HoldMoving(ped, h, s, start, cj, WalkerAt(PathOf(tr), tr.time, CjFeet(cj), CjFacing(), tr.time, (int)s.act - 1), 0.6f, 0.25f);
}
// Along the scene's way: the steps from the first slow ones on, or he would slide off standing as the way starts gently
void HoldWay(scene_game::CPed* ped, Held& h, const Spot& s, bool start) {
    scene_game::CEntity* cj = CutsceneCj();
    Track tr;
    const Way* way = WayOf((const char*)kCutsceneName, s.rank);
    scene_game::CVector aside{};
    if (!way && s.rank) {  // none of its own: the first one's, a step off
        way = WayOf((const char*)kCutsceneName);
        if (way) aside = scene_game::CVector{std::cos(way->front().heading), std::sin(way->front().heading), 0.0f} * (s.rank == 1 ? 0.9f : -0.9f);
    }
    Walker w;
    if (!cj || !TrackOf(cj, tr)) return;  // stays where it was
    if (!way || !WayAt(*way, tr.time, w)) w.feet = s.ground, w.heading = s.heading, w.speed = 0;  // this game has no such way
    w.feet = w.feet + aside;
    HoldMoving(ped, h, s, start, cj, w, 0.15f, 0.08f, SceneActAt(tr.time));
}

// Riding with CJ: in the car's own frame every frame as it drives, sitting as a passenger does, the head to who talks
const Act kRide = {"PED", "CAR_sitp", false, 0.0f};
void HoldInCar(scene_game::CPed* ped, Held& h, const Spot& s, bool start, scene_game::CEntity* car = nullptr) {
    if (!car) car = s.act < NumCutsceneObjects() ? CutsceneObject(s.act) : nullptr;
    scene_game::CMatrix* m = car ? car->Matrix() : nullptr;
    if (!m) {  // the scene has no such car any more: out of it until the next spot
        ped->Flags() &= ~0x80u;
        return;
    }
    if (start) {
        if (h.placed) Cmd(0x0792).I(h.handle).Run();  // CLEAR_CHAR_TASKS_IMMEDIATELY: the previous act stops
        Cmd(0x04D7).I(h.handle).I(1).Run();            // FREEZE_CHAR_POSITION: moved by hand
        if (h.prop >= 0) Cmd(0x0108).I(h.prop).Run(), h.prop = -1;  // DELETE_OBJECT: no cigarette in a car
        h.stage = 0;
    }
    h.spot = s, h.placed = true;
    // the car's front (the heading of the spot): its model may face backwards, and he sat facing the boot
    float facing = std::cos(s.heading) < 0.0f ? -1.0f : 1.0f;
    if (scene_game::CMatrix* pm = ped->Matrix()) pm->right = m->right * facing, pm->forward = m->forward * facing, pm->up = m->up;
    // the seat is where CJ's hips would sit, and the passenger's animation holds the hips 7 cm from the ped's origin. Its
    // own skeleton is no measure of that: an old one (not drawn yet) sent the partner off further every frame, 2800 km.
    SetEntityPosition(ped, m->pos + m->right * s.ground.x + m->forward * s.ground.y + m->up * s.ground.z);
    ped->CurrentRotation() = ped->AimingRotation() = HeadingOf(m->forward * facing);
    ped->PedFlags() = (ped->PedFlags() | 0x3u) & ~0x200u;  // bIsStanding, bWasStanding, not bIsInTheAir
    ped->Flags() |= 0x80;
    if (scene_game::CEntity* cj = CutsceneCj()) scene_game::Field<float>(ped, 0x12C) = scene_game::Field<float>(cj, 0x12C);  // lit like CJ
    if (h.stage < 2) {
        Cmd(0x0605).I(h.handle).S("CAR_sitp").S("PED").F(4.0f).I(1).I(0).I(0).I(0).I(-1).Run();  // TASK_PLAY_ANIM, looped
        h.stage = 2, h.checkedAt = NowMs();
        LOGD("Cutscene partner rides in object %d at %.2f,%.2f,%.2f of it", s.act, s.ground.x, s.ground.y, s.ground.z);
    } else if (NowMs() - h.checkedAt > 500) {  // anything may end the animation: it starts again
        h.checkedAt = NowMs();
        if (!Cmd(0x0611).I(h.handle).S("CAR_sitp").Run()) h.stage = 0;  // IS_CHAR_PLAYING_ANIM
    }
    if (h.stage == 2) Live(ped, h, &kRide);
}

// On foot to the next spot (Spot::walk): the walk of the cutscene clips along the line from where he was, at a walker's
// pace, timed by the cutscene's clock, so both games show the same steps. There the spot's act starts as after a move.
float GoingLength(const Held& h) {
    return h.goViaSet ? (h.goVia - h.goFrom).Length2D() + (h.goTo - h.goVia).Length2D() : (h.goTo - h.goFrom).Length2D();
}
scene_game::CVector GoingAt(const Held& h, float clock) {
    float f = std::clamp((clock - h.goStart) / h.goMs, 0.0f, 1.0f);
    if (!h.goViaSet) return h.goFrom + (h.goTo - h.goFrom) * f;
    float first = (h.goVia - h.goFrom).Length2D(), second = (h.goTo - h.goVia).Length2D(), at = f * (first + second);
    if (at < first) return h.goFrom + (h.goVia - h.goFrom) * (at / first);
    return h.goVia + (h.goTo - h.goVia) * (second > 0.0f ? (at - first) / second : 1.0f);
}
// the leg it walks now: to the turn, then on to the spot
scene_game::CVector GoingWay(const Held& h, float clock) {
    if (!h.goViaSet) return h.goTo - h.goFrom;
    float first = (h.goVia - h.goFrom).Length2D(), f = std::clamp((clock - h.goStart) / h.goMs, 0.0f, 1.0f);
    return f * GoingLength(h) < first ? h.goVia - h.goFrom : h.goTo - h.goVia;
}
void StartGoing(scene_game::CPed* ped, Held& h, const Spot& s, float clock) {
    scene_game::CVector from = h.going ? GoingAt(h, clock) : h.spot.ground;
    if (!h.going) {
        if (h.stage > 0) Cmd(0x0792).I(h.handle).Run();  // CLEAR_CHAR_TASKS_IMMEDIATELY: the act stops
        h.heading = ped->CurrentRotation(), h.turnRate = 0;
    }
    if (h.prop >= 0) Cmd(0x0108).I(h.prop).Run(), h.prop = -1;  // DELETE_OBJECT: no walking off with the cigarette
    h.walk = MocapWalk(kGait), h.walkSpeed = MocapWalkSpeed(kGait), h.speedAt = 0, h.checkedAt = NowMs();
    Cmd(0x0605).I(h.handle).S(h.walk).S(kMocapBlock).F(4.0f).I(1).I(0).I(0).I(0).I(-1).Run();  // TASK_PLAY_ANIM, looped
    h.going = h.walking = true, h.running = false;
    h.goFrom = from, h.goTo = s.ground, h.goVia = s.via, h.goViaSet = s.viaSet, h.goStart = clock;
    h.goMs = std::max(400.0f, GoingLength(h) / kGoPace * 1000.0f);
    h.spot = s, h.stage = 0;
    LOGD("Cutscene partner goes on foot %.1f m to %s%s", GoingLength(h), Describe(s), s.viaSet ? " (round something)" : "");
}
// false once there
bool Going(scene_game::CPed* ped, Held& h, float clock) {
    if (clock < 0.0f || clock >= h.goStart + h.goMs) {  // there (or the scene's camera gone: there at once)
        h.going = h.walking = h.running = false;
        return false;
    }
    uint64_t now = NowMs();
    scene_game::CVector way = GoingWay(h, clock);
    // along the way, the spot's own heading for the last steps
    EaseHeading(h, clock > h.goStart + h.goMs - 400.0f ? h.spot.heading : HeadingOf(way), kEaseWalk);
    if (scene_game::CMatrix* m = ped->Matrix()) {
        float c = std::cos(h.heading), sn = std::sin(h.heading);
        m->right = {c, sn, 0}, m->forward = {-sn, c, 0}, m->up = {0, 0, 1};
    }
    SetEntityPosition(ped, GoingAt(h, clock) + scene_game::CVector{0, 0, scene_game::CallMethod<0x536BE0, float>(ped)});  // game::CEntity::GetDistanceFromCentreOfMassToBaseOfModel
    ped->CurrentRotation() = ped->AimingRotation() = h.heading;
    ped->PedFlags() = (ped->PedFlags() | 0x3u) & ~0x200u;  // bIsStanding, bWasStanding, not bIsInTheAir
    ped->Flags() |= 0x80;
    if (scene_game::CEntity* cj = CutsceneCj()) scene_game::Field<float>(ped, 0x12C) = scene_game::Field<float>(cj, 0x12C);  // lit like CJ
    if (now - h.speedAt > 100) {  // the steps as fast as he goes
        h.speedAt = now;
        Cmd(0x0393).I(h.handle).S(h.walk).F(std::clamp(kGoPace / h.walkSpeed, 0.6f, 1.8f)).Run();  // SET_CHAR_ANIM_SPEED
    }
    if (now - h.checkedAt > 500) {  // anything may end the walk: it starts again
        h.checkedAt = now;
        if (!Cmd(0x0611).I(h.handle).S(h.walk).Run())  // IS_CHAR_PLAYING_ANIM
            Cmd(0x0605).I(h.handle).S(h.walk).S(kMocapBlock).F(4.0f).I(1).I(0).I(0).I(0).I(-1).Run();  // TASK_PLAY_ANIM, looped
    }
    return true;
}

Spot ForRank(Spot s, int rank) {
    s.rank = (uint8_t)std::max(rank, 0);
    if (!s.rank || s.pose == POSE_HIDDEN || s.pose == POSE_WAY) return s;
    if (s.pose == POSE_CAR) {
        if (s.rank <= s.seats) {
            s.ground = s.seat[s.rank - 1];
            return s;
        }
        scene_game::CEntity* cj = CutsceneCj();
        s.pose = POSE_HIDDEN, s.act = 0;  // no seat left
        s.ground = cj ? cj->GetPosition() : scene_game::CVector{};
        return s;
    }
    if (s.pose == POSE_WALK) {
        s.act = (uint8_t)((s.act + s.rank) % 3);  // another side of CJ
        return s;
    }
    // a step along what the first one stands at, the next to its other side
    scene_game::CVector along = scene_game::CVector{std::cos(s.heading), std::sin(s.heading), 0.0f} * (s.rank == 1 ? 0.9f : -0.9f);
    s.ground = s.ground + along, s.via = s.via + along;
    if (s.pose == POSE_SIT) s.pose = POSE_STAND, s.act = 0;  // the seat is the first one's
    return s;
}

// Scene exceptions are evaluated every frame, not only at camera cuts.
bool SceneRule(Spot& out) {
    const char* name = (const char*)kCutsceneName;
    Track tr;
    auto* cj = CutsceneCj();
    if (!cj || !TrackOf(cj, tr)) return false;
    enum Rule { ROUTE_OR_HIDE, WALK_WITH_CJ, LOW_SHOT };
    static const struct { const char* scene; Rule rule; } rules[] = {
        {"INTRO1B", ROUTE_OR_HIDE},
        {"SWEET1B", LOW_SHOT}, {"SWEET1C", LOW_SHOT},
        {"SWEET3B", WALK_WITH_CJ},
    };
    if ((CConfigLoader::ms_bCoopDriveBy && !_strnicmp(name, "SWEET4A", 8)) ||
        (CConfigLoader::ms_bCoopLosSepulcros && !_strnicmp(name, "SWEET7A", 8)) ||
        (CConfigLoader::ms_bCoopRunningDog && (!_strnicmp(name, "SMOKE2A", 8) || !_strnicmp(name, "SMOKE2B", 8))) ||
        (CConfigLoader::ms_bCoopWrongSideTracks && !_strnicmp(name, "SMOKE3A", 8)) ||
        (CConfigLoader::ms_bCoopJustBusiness && !_strnicmp(name, "SMOKE4A", 8))) {
        const Way* way = WayOf(name, out.rank);
        Walker w;
        if (MocapWalkCount() && MocapCount() > 1 && way && WayAt(*way, tr.time, w)) {
            out.ground = w.feet; out.heading = w.heading;
            out.pose = POSE_WAY; out.walk = false;
        } else if (CConfigLoader::ms_bSceneGuestsVisible) {
            return false;  // no route or clips: the general choice beside CJ, not out of the picture
        } else { out.pose = POSE_HIDDEN; out.walk = false; }
        return true;
    }
    for (const auto& rule : rules) {
        if (_strnicmp(name, rule.scene, 8)) continue;
        if (rule.rule == LOW_SHOT) {
            auto* camera = scene_game::Ref<scene_game::CMatrix*>(kCameraMatrix);
            if (!camera || camera->pos.z >= CjFeet(cj).z + 0.65f || camera->forward.z < 0.15f) return false;
        } else {
            const Way* way = WayOf(name, out.rank);
            Walker w;
            if (MocapWalkCount() && MocapCount() > 1 && way && WayAt(*way, tr.time, w)) {
                const auto& path = PathOf(tr);
                bool follows = false;
                if (path.size() > 1 && w.speed < 0.08f) {
                    auto feet = CjFeet(cj);
                    follows = (CjAt(path, tr.time, feet, tr.time + 0.4f) - feet).Length2D() > 0.15f;
                }
                if (follows || (w.feet - CjFeet(cj)).Length2D() > 4.0f) {
                    out.pose = rule.rule == WALK_WITH_CJ ? POSE_WALK : POSE_HIDDEN;
                    out.act = (uint8_t)((out.rank + 2) % 3); out.walk = false;
                    return true;
                }
                out.ground = w.feet; out.heading = w.heading;
                out.pose = POSE_WAY; out.walk = false;
                return true;
            }
            if (rule.rule == WALK_WITH_CJ && MocapWalkCount() && MocapCount() > 1 && PathOf(tr).size() > 1) {
                out.pose = POSE_WALK; out.act = (uint8_t)((out.rank + 2) % 3); out.walk = false;
                return true;
            }
        }
        // no route/clips (INTRO1B): the general choice; a low shot still hides him (he would cover the lens)
        if (CConfigLoader::ms_bSceneGuestsVisible && rule.rule != LOW_SHOT) return false;
        out.pose = POSE_HIDDEN; out.walk = false;
        return true;
    }
    return false;
}

void Hold(scene_game::CPed* ped, const Spot& requested) {
    Spot s = requested;
    // cutscene-show-others off (F1 "Un solo CJ"): a hidden copy stays hidden, the per-scene rules do not show it
    bool ruled = (requested.pose == POSE_HIDDEN && !CConfigLoader::ms_bCutsceneShowOthers) ? false : SceneRule(s);
    if ((s.pose==POSE_WALK || s.pose==POSE_WAY) && !MocapWalkCount()) {
        Spot fallback=s; fallback.pose=POSE_STAND; fallback.walk=false;
        auto cj=CutsceneCj();
        if(!cj) { ped->Flags() &= ~0x80u; return; }
        fallback.ground=CjFeet(cj)+scene_game::CVector{1.6f+float(s.rank)*0.9f,0,0};
        Hold(ped,fallback); return;
    }
    // Mocap and fidgets own the head, including walking/car poses and animation loading.
    StopLooking(ped);
    Held& h = g_held[ped];
    int handle = GetPedRef(ped);
    if(h.handle >= 0 && h.handle != handle) {
        if(h.prop >= 0) Cmd(0x0108).I(h.prop).Run();
        h=Held{};
    }
    if (h.lit < 0) h.lit = scene_game::Field<float>(ped, 0x12C);  // the scene lights it like CJ: its own comes back at the end
    // Empty hands in a scene: he stood by CJ with the chainsaw he had out when INTRO1A began (05.11). A player's ped only
    // picks the slot by the command and takes it in its own control, which a scene holds: set at once too. Each frame: the
    // copy gets the gun again from its owner's state until the owner's game has started the scene as well. After the scene
    // that state brings the copy's gun back.
    if (h.weapon < 0) h.weapon = ped->ActiveWeapon().type;
    if (ped->ActiveWeapon().type != 0) {
        Cmd(0x01B9).I(handle).I(0).Run();  // SET_CURRENT_CHAR_WEAPON
        scene_game::CallMethod<0x5E6280>(ped, 0);      // game::CPed::SetCurrentWeapon
    }
    bool newAct = !h.placed || h.spot.pose != s.pose || h.spot.act != s.act;
    if (ruled && newAct) LOGI("scene-rule %.8s rank %u: %s", (const char*)kCutsceneName, s.rank, Describe(s));
    bool moved = !h.placed || (h.spot.ground - s.ground).Length() > 0.05f || std::fabs(h.spot.heading - s.heading) > 0.01f;
    h.handle = handle;
    if (s.pose == POSE_WALK || s.pose == POSE_CAR || s.pose == POSE_WAY) h.going = false;
    if (s.pose == POSE_WALK) return HoldWalk(ped, h, s, newAct);
    if (s.pose == POSE_WAY && MocapWalkCount() && MocapCount() > 1) return HoldWay(ped, h, s, newAct);
    if (s.pose == POSE_CAR) return HoldInCar(ped, h, s, newAct || moved);
    float clock = cam::Mode() == kModeFlyBy ? scene_game::Ref<float>(cam::Active() + 0x90) : -1.0f;
    bool fromFoot = h.going || h.spot.pose == POSE_STAND || h.spot.pose == POSE_LEAN;
    if (moved && s.walk && h.placed && fromFoot && clock >= 0.0f && MocapWalkCount() && !ped->InVehicle())
        StartGoing(ped, h, s, clock);
    else if (moved)
        h.going = false;  // a jump after all: the move below
    if (h.going) {
        if (Going(ped, h, clock)) return;
        moved = newAct = true;  // there: set on the spot, its act starts
        if (h.walking || h.stage > 0) Cmd(0x0792).I(handle).Run();  // CLEAR_CHAR_TASKS_IMMEDIATELY: the walk stops
        h.stage = 0;
    }
    if (moved) {
        if (h.placed) Cmd(0x0792).I(handle).Run();  // CLEAR_CHAR_TASKS_IMMEDIATELY: no landing from a fall here
        // still seated (its on-foot state has not come yet): moving it would move the vehicle, so out first
        if (ped->InVehicle()) Cmd(0x0362).I(handle).F(s.ground.x).F(s.ground.y).F(s.ground.z + 1.0f).Run();  // WARP_CHAR_FROM_CAR_TO_COORD
        Cmd(0x04D7).I(handle).I(1).Run();  // FREEZE_CHAR_POSITION: the scene's collision may not be loaded here
        Cmd(0x00A1).I(handle).F(s.ground.x).F(s.ground.y).F(s.ground.z).Run();  // SET_CHAR_COORDINATES (feet)
        Cmd(0x0173).I(handle).F(s.heading * 57.29578f).Run();                   // SET_CHAR_HEADING
        h.stage = 0;  // the move ends the animation task: it starts again
    }
    const Act* a = ActOf(s);
    if (newAct) {
        if (h.stage > 0) Cmd(0x0792).I(handle).Run();  // CLEAR_CHAR_TASKS_IMMEDIATELY: the previous act stops
        if (h.prop >= 0 && (!a || !a->cigarette)) {
            Cmd(0x0108).I(h.prop).Run();  // DELETE_OBJECT
            h.prop = -1;
        }
        h.stage = 0;
    }
    if (moved || newAct) {  // the variations start over, drawn from the new spot
        h.seed = Mix(Mix((uint32_t)std::lround(s.ground.x * 100.0f), (uint32_t)std::lround(s.ground.y * 100.0f)), s.pose * 8u + s.act);
        h.slot = ~0u, h.fidgetAt = -1, h.loopClock = -1, h.fidget = nullptr, h.clip = nullptr, h.clips = 0, h.walking = h.running = false, h.turning = 0;
    }
    h.spot = s;
    h.placed = true;
    ped->CurrentRotation() = ped->AimingRotation() = s.heading;
    // frozen, it never lands after a move: "in the air", it would fall (hidden too: it lands when it shows again)
    // and the fall cancels the animation
    ped->PedFlags() = (ped->PedFlags() | 0x3u) & ~0x200u;  // bIsStanding, bWasStanding, not bIsInTheAir
    if (!a) {  // nowhere to be in this shot
        ped->Flags() &= ~0x80u;
        return;
    }
    ped->Flags() |= 0x80;  // bIsVisible: cutscenes hide players
    // lit like CJ: a frozen ped keeps the light of the place it stood before (white at night)
    if (scene_game::CEntity* cj = CutsceneCj()) scene_game::Field<float>(ped, 0x12C) = scene_game::Field<float>(cj, 0x12C);  // m_fContactSurfaceBrightness
    if (h.stage == 0) {
        bool ready = BlockReady(a->block);
        if (a->cigarette && !IsModelLoaded(kCigarette)) {
            RequestModel(kCigarette);
            ready = false;
        }
        if (!ready) return;
        if (a->cigarette) {
            if (h.prop < 0 && HasRoom(ObjectPool(), kKeepObjects, "object"))
                Cmd(0x0107).I(kCigarette).F(s.ground.x).F(s.ground.y).F(s.ground.z + 1.0f).Out(&h.prop).Run();  // CREATE_OBJECT
            // TASK_PICK_UP_OBJECT: into the right hand, no pick-up animation
            if (h.prop >= 0) Cmd(0x070A).I(handle).I(h.prop).F(0.0f).F(0.0f).F(0.0f).I(6).I(16).S("NULL").S("NULL").I(-1).Run();
        }
        h.stage = 1;
        return;  // the animation starts next frame, once the hand holds the cigarette
    }
    if (h.stage == 1) {
        if (!a->anim) NextClip(h, 8.0f);
        else Cmd(0x0605).I(handle).S(a->anim).S(a->block).F(8.0f).I(1).I(0).I(0).I(0).I(-1).Run();  // TASK_PLAY_ANIM, looped
        h.stage = 2, h.checkedAt = NowMs();  // the task starts it next frame
        LOGD("Cutscene partner: %s/%s started", a->block, AnimNow(h, a));
    }
    Live(ped, h, a);
    // anything may end the animation (an event of the ped, the sync of a remote player): it starts again
    if (h.stage == 2 && NowMs() - h.checkedAt > 500) {
        h.checkedAt = NowMs();
        if (!Cmd(0x0611).I(handle).S(AnimNow(h, a)).Run()) {  // IS_CHAR_PLAYING_ANIM
            LOGD("Cutscene partner: %s/%s stopped, starting it again (ped flags %08X)", a->block, AnimNow(h, a), ped->PedFlags());
            Cmd(0x0792).I(handle).Run();  // CLEAR_CHAR_TASKS_IMMEDIATELY: whatever took over
            h.stage = 0;
        }
    }
}

void HoldVehicleSeat(scene_game::CPed* ped, scene_game::CEntity* vehicle, const scene_game::CVector& seat) {
    Spot s{seat, 0, POSE_CAR, 0};
    if (!g_held.count(ped) || g_held[ped].handle != GetPedRef(ped))
        Hold(ped, Spot{ped->GetPosition(), 0, POSE_HIDDEN, 0});
    StopLooking(ped);
    Held& h = g_held[ped];
    HoldInCar(ped, h, s, h.spot.pose != POSE_CAR, vehicle);
}

// A decorative car follows the actual animated prop, never the live mission vehicle.
namespace {
int g_rideAnchor = -1;
Bounds g_rideBounds;
}
bool CarBehind(scene_game::CEntity* vehicle, int rank, float length) {
    auto* cj = CutsceneCj();
    if (!cj || !vehicle || !vehicle->Matrix()) return false;
    for (int i = 0; i < NumCutsceneObjects(); ++i) {
        auto* car = CutsceneObject(i);
        Bounds b;
        if (!car || !(car->Flags() & 0x80) || !car->Matrix() ||
            (car->GetPosition() - cj->GetPosition()).Length2D() > 6.0f) continue;
        if (i == g_rideAnchor) b = g_rideBounds;
        else {
            if (VehicleOf(car) < 400 || !IsCar(car, b)) continue;
            g_rideAnchor = i; g_rideBounds = b;
        }
        auto forward = Flat(car->Matrix()->forward);
        if (forward.Dot(CjFacing()) < 0.0f) forward = forward * -1.0f;
        auto* m = vehicle->Matrix();
        m->forward = forward; m->right = {forward.y, -forward.x, 0}; m->up = {0, 0, 1};
        float half = 0.5f * (std::fabs(forward.x) * (b.x1 - b.x0) + std::fabs(forward.y) * (b.y1 - b.y0));
        SetEntityPosition(vehicle, car->GetPosition() - forward * (half + length * 0.5f + 2.0f + rank * (length + 2.0f)));
        vehicle->Flags() |= 0x80;
        return true;
    }
    vehicle->Flags() &= ~0x80u;
    return false;
}

void ReleaseAll() {
    g_rideAnchor = -1;
    for (auto& [ped, h] : g_held) {
        if (GetPed(h.handle) == ped) {
            if (h.lookOn) StopLooking(ped);
            if (h.stage > 0) Cmd(0x0792).I(h.handle).Run();  // CLEAR_CHAR_TASKS_IMMEDIATELY
            Cmd(0x04D7).I(h.handle).I(0).Run();              // FREEZE_CHAR_POSITION off
            // A ped on foot takes the light of the ground only when it collides with it (game::CPed::ProcessEntityCollision),
            // one in a car the car's: standing still after "The Green Sabre" (its night scenes, then the move to Angel
            // Pine) the partner kept the scene's dark light, as if indoors, till he got into a car (02.11)
            if (h.lit >= 0) scene_game::Field<float>(ped, 0x12C) = h.lit;
            ped->Flags() |= 0x80;
            // his gun back, if the mission left it to him and nothing else came to his hand
            if (h.weapon > 0 && ped == scene_game::FindPlayerPed(0) && ped->Health() > 0.0f && !ped->InVehicle() && ped->ActiveWeapon().type == 0)
                for (int i = 0; i < NUM_WEAPON_SLOTS; ++i)
                    if (ped->Weapons()[i].type == h.weapon) {
                        Cmd(0x01B9).I(h.handle).I(h.weapon).Run();  // SET_CURRENT_CHAR_WEAPON
                        scene_game::CallMethod<0x5E6280>(ped, h.weapon);        // game::CPed::SetCurrentWeapon
                        break;
                    }
        }
        if (h.prop >= 0) Cmd(0x0108).I(h.prop).Run();  // DELETE_OBJECT
    }
    g_held.clear();
    ClearTalk();
    g_haveSpot = g_haveSeat = false;
    g_live = false;
    g_cutscene[0] = 0;
    g_follow.clear();
    g_decided = false;
}

