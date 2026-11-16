// T75: the guest at the war table of "New Model Army" (ZERO4): see the header. The mission's own magnet is its
// script (ZERO4.txt 2779-2811: GET_OFFSET_FROM_CAR_IN_WORLD_COORDS 0,0,-1, LOCATE_OBJECT_3D of 1 m, the fire
// button, ATTACH_OBJECT_TO_CAR); the guest's takes up the copies the same way, and the owner's real objects follow
// his copy by the object-move channel. The bomb (RCBOMB) stays the owner's: the script blows up only the one its own
// magnet dropped.
// Adapted from Karimov-Akbar/sa-coop (MIT), rc-toys/RcToys.cpp and mission-objects/Objects.cpp; see NOTICE.md.
#include "stdafx.h"
#include "CMissionWarTable.h"
#include "CMissionMachines.h"
#include "CMissionObjectMoves.h"
#include "CObjectSync.h"
#include "CConfigLoader.h"
#include "SceneCompat/Game.h"
#include "SceneCompat/Script.h"

#include <cmath>
#include <cstring>

namespace
{
using scene_game::script::Cmd;

constexpr int kGoblin = 501;
constexpr uint8_t kTableArea = 10;
// where our CJ waits in area 10 meanwhile: beside the owner's, which the mission parks on a BMX (ZERO4.txt 727:
// -1004.097, 1000.789, 1349.749)
const CVector kParked{-1006.5f, 1000.8f, 1349.7f};
const CVector kTableView{-982.635f, 1055.88f, 1348.926f};  // a camera over the table while the toy's floor loads
constexpr float kGrab = 1.0f;         // ZERO4: LOCATE_OBJECT_3D of 1 m around the point 1 m under the toy
constexpr uint32_t kAgainMs = 500;    // between a drop and the next lift
constexpr uint32_t kFloorMs = 5000;   // back in the street: frozen until its floor is there, this long at most

struct State
{
    bool entered = false;     // our CJ moved into the table's area for the toy
    CVector stood{};          // where he stood in the street
    uint32_t floorUntil = 0;  // back in the street: frozen until its floor is under him
    int magnetObj = -1;       // the MINI_MAGNET under our toy (it only shows)
    uint32_t holding = 0;     // the copy on our magnet (CObjectSync id), 0: none
    uint32_t droppedMs = 0;
    int barrel = -1, plank = -1, magnet = -1;  // model indices, looked up once
} s;

bool On() { return CConfigLoader::ms_bCoopMissionMachines && CNetwork::m_bConnected && CNetwork::m_bAuthenticated; }
CVehicle* Toy() { return CWorld::Players[0].m_pRemoteVehicle; }
// our own RC Goblin in the war room
bool TableToy(CVehicle* v)
{
    return v && v->IsVTableValid() && v->m_nModelIndex == kGoblin && v->m_nAreaCode == kTableArea;
}

int Model(const char* name)
{
    char buf[32];
    strncpy_s(buf, name, _TRUNCATE);
    int index = -1;
    return CModelInfo::GetModelInfo(buf, &index) ? index : -1;
}
void Models()
{
    if (s.barrel < 0) s.barrel = Model("BARREL2");
    if (s.plank < 0) s.plank = Model("KMB_PLANK");
    if (s.magnet < 0) s.magnet = Model("MINI_MAGNET");
}

// a MINI_MAGNET under the toy, as ZERO4 hangs its own (ZERO4.txt 741-743; no collision: it only shows)
void HangMagnet(CVehicle* toy)
{
    if (s.magnetObj >= 0 && CPools::GetObject(s.magnetObj)) return;
    s.magnetObj = -1;
    if (s.magnet <= 0) return;
    if (CStreaming::ms_aInfoForModel[s.magnet].m_nLoadState != LOADSTATE_LOADED)
    {
        CStreaming::RequestModel(s.magnet, eStreamingFlags::MISSION_REQUIRED);
        CStreaming::LoadAllRequestedModels(false);
        return;
    }
    const CVector at = toy->GetPosition();
    Cmd(0x0107).I(s.magnet).F(at.x).F(at.y).F(at.z - 0.2f).Out(&s.magnetObj).Run();  // CREATE_OBJECT
    if (s.magnetObj < 0) return;
    Cmd(0x0382).I(s.magnetObj).I(0).Run();  // SET_OBJECT_COLLISION off
    Cmd(0x0681).I(s.magnetObj).I(CPools::GetVehicleRef(toy)).F(0.0f).F(0.0f).F(-0.2f).F(0.0f).F(0.0f).F(0.0f)
        .Run();  // ATTACH_OBJECT_TO_CAR
    if (auto* o = CPools::GetObject(s.magnetObj)) o->m_nAreaCode = toy->m_nAreaCode;
}

void TakeDownMagnet()
{
    if (s.magnetObj >= 0 && CPools::GetObject(s.magnetObj)) Cmd(0x0108).I(s.magnetObj).Run();  // DELETE_OBJECT
    s.magnetObj = -1;
}

CObject* Held() { return s.holding ? CObjectSync::Resolve(s.holding) : nullptr; }

// the copy off our magnet, where it hangs now: the owner's real one falls from there
void Drop()
{
    if (!s.holding) return;
    if (auto* copy = Held())
    {
        const int h = CPools::GetObjectRef(copy);
        if (copy->m_pAttachedTo) Cmd(0x0682).I(h).F(0.0f).F(0.0f).F(0.0f).I(0).Run();  // DETACH_OBJECT
        Cmd(0x0382).I(h).I(1).Run();  // SET_OBJECT_COLLISION back
    }
    CMissionObjectMoves::Unclaim(s.holding);  // final place told, the copy a copy again
    s.holding = 0;
    s.droppedMs = GetTickCount();
}

// the nearest barrel or plank copy 1 m under the toy, on our magnet (and claimed: the owner's follows)
void Lift(CVehicle* toy)
{
    const CMatrix* m = toy->m_matrix;
    if (!m) return;
    const CVector at = m->pos - m->at;  // GET_OFFSET_FROM_CAR_IN_WORLD_COORDS 0,0,-1
    uint32_t best = 0;
    CObject* bestCopy = nullptr;
    float bestD = 0.0f;
    for (const auto& [id, copy] : CObjectSync::GetPhotoTargets(false))
    {
        if (!copy || !copy->m_matrix || copy->m_pAttachedTo) continue;
        if (copy->m_nModelIndex != s.barrel && copy->m_nModelIndex != s.plank) continue;
        CVector d = copy->m_matrix->pos - at;
        if (std::fabs(d.x) > kGrab || std::fabs(d.y) > kGrab || std::fabs(d.z) > kGrab) continue;
        const float dist = d.Magnitude();
        if (!bestCopy || dist < bestD)
        {
            best = id;
            bestCopy = copy;
            bestD = dist;
        }
    }
    if (!bestCopy) return;
    CMissionObjectMoves::Claim(best);
    if (!CMissionObjectMoves::IsClaimed(best)) return;
    const int h = CPools::GetObjectRef(bestCopy), t = CPools::GetVehicleRef(toy);
    const bool barrel = bestCopy->m_nModelIndex == s.barrel;
    Cmd(0x0382).I(h).I(0).Run();  // SET_OBJECT_COLLISION off: it rides the toy
    // as ZERO4 hangs them (2804, 2837): barrels on their side 1.2 m under, planks flat 1 m under
    Cmd(0x0681).I(h).I(t).F(0.0f).F(0.0f).F(barrel ? -1.2f : -1.0f).F(0.0f).F(barrel ? 90.0f : 0.0f).F(0.0f)
        .Run();  // ATTACH_OBJECT_TO_CAR
    Cmd(0x050E).I(h).I(t).Run();  // SORT_OUT_OBJECT_COLLISION_WITH_CAR, as ZERO4
    s.holding = best;
    CMissionObjectMoves::Report(best);
}

void Magnet()
{
    CVehicle* toy = Toy();
    if (!TableToy(toy))
    {
        Drop();
        TakeDownMagnet();
        return;
    }
    Models();
    HangMagnet(toy);
    if (s.holding)
    {
        auto* copy = Held();
        if (!copy || copy->m_pAttachedTo != toy)
            Drop();  // the copy went (the owner's script used it) or came off
        else
            CMissionObjectMoves::Report(s.holding);  // 100 ms at most, inside
    }
    const CPad* pad = CPad::GetPad(0);
    if (!pad->NewState.ButtonCircle || pad->OldState.ButtonCircle) return;  // the fire button, as ZERO4 (2775)
    if (s.holding)
        Drop();
    else if (GetTickCount() - s.droppedMs > kAgainMs)
        Lift(toy);
}

// back in the street from the table: frozen until its floor is under CJ
void FloorWait(CPed* me)
{
    if (!s.floorUntil) return;
    bool found = false;
    const float z = scene_game::Call<0x5696C0, float>(s.stood.x, s.stood.y, s.stood.z + 1.0f, &found,
                                                     (CEntity**)nullptr);  // CWorld::FindGroundZFor3DCoord
    if ((!found || z < s.stood.z - 2.5f) && GetTickCount() < s.floorUntil) return;
    Cmd(0x04D7).I(CPools::GetPedRef(me)).I(0).Run();  // FREEZE_CHAR_POSITION off
    s.floorUntil = 0;
}

// CJ back in the street where he stood (the toy is gone: CMissionMachines gave him back already)
void Leave(CPed* me)
{
    Drop();
    TakeDownMagnet();
    if (!s.entered) return;
    s.entered = false;
    const int h = CPools::GetPedRef(me);
    Cmd(0x02EB).Run();            // RESTORE_CAMERA_JUMPCUT: ours over the table, if the toy's never came
    Cmd(0x04BB).I(0).Run();       // SET_AREA_VISIBLE
    Cmd(0x0860).I(h).I(0).Run();  // SET_CHAR_AREA_VISIBLE
    if (me->m_fHealth <= 0.0f) return;  // dead: the respawn takes him
    Cmd(0x03CB).F(s.stood.x).F(s.stood.y).F(s.stood.z).Run();              // LOAD_SCENE: the street's floor back
    Cmd(0x00A1).I(h).F(s.stood.x).F(s.stood.y).F(s.stood.z - 1.0f).Run();  // SET_CHAR_COORDINATES (feet)
    Cmd(0x04D7).I(h).I(1).Run();  // FREEZE_CHAR_POSITION: until that floor is there (FloorWait)
    s.floorUntil = GetTickCount() + kFloorMs;
}
}  // namespace

void CMissionWarTable::Reset()
{
    if (auto me = FindPlayerPed(0); me && me->IsVTableValid()) Leave(me);
    s.entered = false;
    s.holding = 0;
    s.magnetObj = -1;
}

void CMissionWarTable::Process()
{
    auto me = FindPlayerPed(0);
    if (!me || !me->IsVTableValid())
    {
        s.entered = false;  // the world went, and our things with it
        s.holding = 0;
        s.magnetObj = -1;
        s.floorUntil = 0;
        return;
    }
    FloorWait(me);
    if (!On())
    {
        if (s.entered || s.holding || s.magnetObj >= 0) Leave(me);
        return;
    }
    Magnet();
    // the toy went, or none came (CMissionMachines took CJ back: unfrozen, at the table still)
    if (s.entered && !CMissionMachines::GuestToyPending()) Leave(me);
}

void CMissionWarTable::Enter(CPed* me, const CVector& table, uint8_t area)
{
    if (!me || !area || s.entered) return;
    const int h = CPools::GetPedRef(me);
    s.stood = me->GetPosition();
    s.entered = true;
    s.floorUntil = 0;
    // collision streams by where our CJ is: he waits in the area by the owner's parked one, the camera over the
    // table rather than on him in the void
    Cmd(0x0860).I(h).I(area).Run();                                  // SET_CHAR_AREA_VISIBLE
    Cmd(0x04BB).I(area).Run();                                       // SET_AREA_VISIBLE
    Cmd(0x00A1).I(h).F(kParked.x).F(kParked.y).F(kParked.z).Run();  // SET_CHAR_COORDINATES
    Cmd(0x03CB).F(table.x).F(table.y).F(table.z).Run();              // LOAD_SCENE
    Cmd(0x04E4).F(table.x).F(table.y).Run();                         // REQUEST_COLLISION
    Cmd(0x015F).F(kTableView.x).F(kTableView.y).F(kTableView.z).F(0.0f).F(0.0f).F(0.0f).Run();  // SET_FIXED_CAMERA_POSITION
    Cmd(0x0160).F(table.x).F(table.y).F(table.z).I(2).Run();         // POINT_CAMERA_AT_POINT
}
