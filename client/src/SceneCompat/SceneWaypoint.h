// Adapted from Karimov-Akbar/sa-coop (MIT), cutscene-partner; see NOTICE.md.
#pragma once
#include "SceneCompat/Game.h"
#include <string_view>
#include <vector>

namespace coop::scene::waypoint {

constexpr float kWalkGap = 1.7f;
constexpr float kWalkSide = 0.75f;
constexpr float kPathStep = 0.05f;
constexpr float kPathJump = 0.5f;

struct Track {
    const uint8_t* seq = nullptr;
    float time = 0;
};
bool TrackOf(scene_game::CEntity* e, Track& tr);
scene_game::CVector RootAt(const uint8_t* seq, float t);
scene_game::CVector FeetLater(const Track& tr, const scene_game::CVector& feet, float dt);
scene_game::CVector FacingAt(const uint8_t* seq, float t);
scene_game::CVector CjFacing();

struct Walker {
    scene_game::CVector feet;
    float heading = 0, speed = 0;
    int turning = 0;
};
struct WayKey {
    float t;
    scene_game::CVector at;
    float heading;
    int mark;
};
using Way = std::vector<WayKey>;

const std::vector<scene_game::CVector>& PathOf(const Track& tr);
scene_game::CVector CjAt(const std::vector<scene_game::CVector>& path, float now, const scene_game::CVector& feetNow, float t);
scene_game::CVector CjFeet(scene_game::CEntity* cj);
scene_game::CVector WalkOff(const std::vector<scene_game::CVector>& path, float now, const scene_game::CVector& feetNow, const scene_game::CVector& cjFwd, float s);
scene_game::CVector WalkPoint(const std::vector<scene_game::CVector>& path, float now, const scene_game::CVector& feetNow, const scene_game::CVector& cjFwd, float t, int side);
Walker WalkerAt(const std::vector<scene_game::CVector>& path, float now, const scene_game::CVector& feetNow, const scene_game::CVector& cjFwd, float t, int side);
bool NextLine(std::string_view& text, char* line, size_t size);
std::string_view Embedded(const wchar_t* name);
const Way* WayOf(const char* cutscene, int rank = 0);
bool WayAt(const Way& way, float t, Walker& w);

}  // namespace coop::scene::waypoint
