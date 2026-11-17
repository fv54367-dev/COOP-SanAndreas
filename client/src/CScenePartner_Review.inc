// ---- review harness
void DumpShot(const std::string& path) {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return;
    const Scene& sc = g_lastScene;
    auto body = [&](const Body& b) { fprintf(f, " %.1f %.1f %.1f %.1f %.2f", b.x, b.top, b.bottom, b.half, b.depth); };
    fprintf(f, "shot %s %d\n", g_cutscene, g_shot);
    fprintf(f, "cam %.3f %.3f %.3f %.4f %.4f %.4f %.1f\n", sc.cam.x, sc.cam.y, sc.cam.z, sc.camFwd.x, sc.camFwd.y,
            sc.camFwd.z, sc.fov);
    fprintf(f, "screen %.0f %.0f\n", sc.w, sc.h);
    fprintf(f, "cj %.3f %.3f %.3f %.3f %d", sc.cj.x, sc.cj.y, sc.cj.z, sc.floor, sc.cjOnScreen);
    body(sc.cjBody);
    fprintf(f, "\n");
    for (const Actor& a : sc.actors) {
        fprintf(f, "actor %.3f %.3f %.3f %d", a.feet.x, a.feet.y, a.feet.z, a.onScreen);
        body(a.body);
        fprintf(f, "\n");
    }
    fprintf(f, "spot %.3f %.3f %.3f %.3f %d %d\n", g_spot.ground.x, g_spot.ground.y, g_spot.ground.z, g_spot.heading,
            g_spot.pose, g_spot.act);
    // the rest of the shot: the camera's path and the people's tracks
    for (const Sample& m : g_ahead) {
        fprintf(f, "ahead %.2f %.3f %.3f %.3f %.4f %.4f %.4f %.3f %.3f", m.dt, m.view.pos.x, m.view.pos.y, m.view.pos.z,
                m.view.fwd.x, m.view.fwd.y, m.view.fwd.z, m.cj.x, m.cj.y);
        for (const scene_game::CVector& a : m.actors) fprintf(f, " %.3f %.3f", a.x, a.y);
        fprintf(f, "\n");
    }
    for (const Tried& t : g_tried)
        fprintf(f, "try %.3f %.3f %.3f %.3f %d %.2f %d\n", t.spot.ground.x, t.spot.ground.y, t.spot.ground.z,
                t.spot.heading, t.spot.pose, t.score, t.reject);
    // the height map the spots were checked against: per row, top (2 cm) and seat (1 cm) in hex, ground as 0/1
    fprintf(f, "grid %.2f %.2f %.3f %d %.2f\n", g_grid.x0, g_grid.y0, g_grid.floor, kCells, kCell);
    for (int y = 0; y < kCells; ++y) {
        fprintf(f, "gt ");
        for (int x = 0; x < kCells; ++x) fprintf(f, "%02x", g_grid.top[y * kCells + x]);
        fprintf(f, "\ngs ");
        for (int x = 0; x < kCells; ++x) fprintf(f, "%02x", g_grid.seat[y * kCells + x]);
        fprintf(f, "\ngg ");
        for (int x = 0; x < kCells; ++x) fputc('0' + g_grid.ground[y * kCells + x], f);
        fprintf(f, "\n");
    }
    fclose(f);
}

struct WorldDump {
    FILE* f;
    float lo, hi;
    char tag;
};
void DumpTriangle(const scene_game::CVector& a, const scene_game::CVector& b, const scene_game::CVector& c, void* user) {
    auto* d = (WorldDump*)user;
    if (std::max({a.z, b.z, c.z}) < d->lo || std::min({a.z, b.z, c.z}) > d->hi) return;
    fprintf(d->f, "%c %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f\n", d->tag, a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z);
}

void DumpWorld(const std::string& path) {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return;
    const Scene& sc = g_lastScene;
    scene_game::CEntity* ents[512];
    int16_t n = 0;
    scene_game::Call<0x564A20>(&sc.cj, 30.0f, false, &n, (int16_t)512, ents, true, false, false, true, false);  // FindObjectsInRange
    float lo = sc.floor - 1.0f, hi = sc.floor + 3.5f;  // what a person on CJ's floor can touch
    int area = scene_game::Ref<int32_t>(kCurrArea);
    for (int i = 0; i < n; ++i) {
        scene_game::CEntity* e = ents[i];
        if (e->AreaCode() != area && e->AreaCode() != 13) continue;
        void* mi = scene_game::Ref<void*>(addr::ModelInfoPtrs + (uintptr_t)e->ModelIndex() * 4);
        void* col = mi ? scene_game::Field<void*>(mi, 0x14) : nullptr;     // CBaseModelInfo::m_pColModel
        uint8_t* data = col ? scene_game::Field<uint8_t*>(col, 0x2C) : nullptr;  // CColModel::m_pColData
        scene_game::CVector p = e->GetPosition();
        fprintf(f, "E %d %d %.2f %.2f %.2f\n", e->ModelIndex(), (int)(scene_game::Field<uint8_t>(e, 0x36) & 7), p.x, p.y, p.z);
        if ((e->Flags() & 0x80) && !scene_game::Field<uint8_t>(e, 0x34)) {  // drawn, not a LOD: walls without collision too
            WorldDump d{f, lo, hi, 'R'};
            ForEachTriangle(e, &DumpTriangle, &d);
        }
        if (!data) continue;
        int spheres = *(uint16_t*)data, boxes = *(uint16_t*)(data + 2), tris = *(uint16_t*)(data + 4);
        auto* sp = *(uint8_t**)(data + 0x08);
        auto* bx = *(uint8_t**)(data + 0x0C);
        auto* vx = *(int16_t**)(data + 0x14);
        auto* tr = *(uint16_t**)(data + 0x18);
        for (int k = 0; sp && k < spheres; ++k) {
            const uint8_t* s = sp + k * 0x14;
            scene_game::CVector c = Transform(e, *(const scene_game::CVector*)s, true);
            float r = *(const float*)(s + 0x0C);
            if (c.z + r >= lo && c.z - r <= hi) fprintf(f, "S %.2f %.2f %.2f %.2f %d\n", c.x, c.y, c.z, r, s[0x10]);
        }
        for (int k = 0; bx && k < boxes; ++k) {
            const uint8_t* b = bx + k * 0x1C;
            const scene_game::CVector& mn = *(const scene_game::CVector*)b;
            const scene_game::CVector& mx = *(const scene_game::CVector*)(b + 0x0C);
            fprintf(f, "B %d", b[0x18]);
            for (int c = 0; c < 8; ++c) {
                scene_game::CVector w = Transform(e, scene_game::CVector{c & 1 ? mx.x : mn.x, c & 2 ? mx.y : mn.y, c & 4 ? mx.z : mn.z}, true);
                fprintf(f, " %.2f %.2f %.2f", w.x, w.y, w.z);
            }
            fprintf(f, "\n");
        }
        for (int k = 0; vx && tr && k < tris; ++k) {
            const uint16_t* t = tr + k * 4;  // a, b, c, material | light
            scene_game::CVector v[3];
            for (int j = 0; j < 3; ++j) {
                const int16_t* c = vx + t[j] * 3;
                v[j] = Transform(e, scene_game::CVector{c[0] / 128.0f, c[1] / 128.0f, c[2] / 128.0f}, true);
            }
            float zmin = std::min({v[0].z, v[1].z, v[2].z}), zmax = std::max({v[0].z, v[1].z, v[2].z});
            if (zmax < lo || zmin > hi) continue;
            fprintf(f, "T %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %d\n", v[0].x, v[0].y, v[0].z, v[1].x, v[1].y,
                    v[1].z, v[2].x, v[2].y, v[2].z, t[3] & 0xFF);
        }
    }
    // the cutscene's own objects: people are skinned, the rest are props (furniture, doors) without collision
    for (int i = 0; i < NumCutsceneObjects(); ++i) {
        scene_game::CEntity* o = CutsceneObject(i);
        if (!o || o->ModelIndex() == 1 || (o->GetPosition() - sc.cj).Length() > 20.0f) continue;
        bool person = IsPerson(o);
        scene_game::CVector p = o->GetPosition();
        Bounds cb;
        bool car = IsCar(o, cb);
        fprintf(f, "P %d %d %.2f %.2f %.2f %d %.1f %.1f\n", o->ModelIndex(), person, p.x, p.y, p.z, car, cb.x1 - cb.x0,
                cb.y1 - cb.y0);
        if (!person) {
            WorldDump d{f, lo, hi, 'Q'};
            ForEachTriangle(o, &DumpTriangle, &d);
        }
    }
    fclose(f);
}

scene_game::CVector LastCj() { return g_lastScene.cj; }

