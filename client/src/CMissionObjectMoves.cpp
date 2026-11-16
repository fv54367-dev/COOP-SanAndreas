// T70: the reusable "a guest moved a mission object" channel (see the header). One owner at a time per object:
// the authority applies the moves of the guest that keeps reporting, and a guest who stops (or disconnects)
// leaves the object where his last report put it, open for the next one.
#include "stdafx.h"
#include "CMissionObjectMoves.h"
#include "CConfigLoader.h"
#include "CCoopMission.h"
#include "CObjectSync.h"
#include "CMissionMirror.h"
#include "COpCodeSync.h"
#include "network/packets/mission_machines.h"

#include <map>

namespace
{
using D = Packets::Scripts::GuestObjectMove;

// guest: objectid -> the last transform told and when
struct ClaimState
{
    uint32_t told = 0;
    float x = 0, y = 0, z = 0;
    bool releasing = false;  // the final report: the owner's real one is let go there (T75)
};
std::map<uint32_t, ClaimState> claims;

// authority: objectid -> the guest that may move it and when his last report came
struct Mover
{
    playerid_t player{};
    uint32_t heard = 0;
};
std::map<uint32_t, Mover> movers;

bool On() { return CConfigLoader::ms_bCoopMissionMachines; }
bool GuestTalks() { return On() && CNetwork::m_bConnected && CNetwork::m_bAuthenticated; }

// the copy turns pushable while a guest moves it, and static again on release (CObjectSync made it static)
void SetPushable(CObject* object, bool pushable)
{
    if (!object) return;
    object->SetIsStatic(!pushable);
    object->m_nPhysicalFlags.bDisableMoveForce = !pushable;
    object->m_nPhysicalFlags.bInfiniteMass = !pushable;
}

// the authority: the real object let go by the guest's machine falls from where it was put (a script object is
// static until something touches it: a barrel dropped on a tank must come down, T75)
void Wake(CObject* object)
{
    if (object->m_nPhysicalFlags.bDisableMoveForce) return;  // a fixed thing stays one
    object->SetIsStatic(false);
    object->m_bIsStaticWaitingForCollision = false;
    object->m_vecMoveSpeed = CVector{};
    object->m_vecTurnSpeed = CVector{};
    object->AddToMovingList();
}

CObject* Owned(uint32_t objectid)
{
    for (const auto& [id, object] : CObjectSync::GetPhotoTargets(true))
        if (id == objectid && object && object->IsVTableValid()) return object;
    return nullptr;
}

// the authority: put the real object where the guest's copy stands (the same re-add the copies get, so the
// mission's own checks see it move)
bool ApplyToOwned(uint32_t objectid, const D& p)
{
    for (const auto& [id, object] : CObjectSync::GetPhotoTargets(true))
    {
        if (id != objectid || !object || !object->IsVTableValid()) continue;
        if (object->m_pAttachedTo) return false;  // the owner's own machine took it: his script's hold stands (T75)
        CWorld::Remove(object);
        if (object->m_matrix)
        {
            object->m_matrix->right = p.right;
            object->m_matrix->up = p.forward;
            object->m_matrix->at = CVector(p.right.y * p.forward.z - p.right.z * p.forward.y,
                p.right.z * p.forward.x - p.right.x * p.forward.z,
                p.right.x * p.forward.y - p.right.y * p.forward.x);
            object->m_matrix->pos = p.pos;
        }
        object->m_nAreaCode = p.area;
        object->UpdateRwMatrix();
        object->UpdateRwFrame();
        CWorld::Add(object);
        if (p.released) Wake(object);
        return true;
    }
    return false;
}

// RYDER2 "Robbing Uncle Sam": the script counts a crate only while ITS player sits in the forklift 99@ and the crate is
// at the truck's back (RYDER2.txt 16562-16947: Object.Locate3D(crate, Mule +0/-2.61/+1.04, 0.71/1.0/1.12) and
// IS_CHAR_IN_CAR($scplayer, 99@)). A guest drives his own forklift, so the owner's check never holds for him: the crate
// he brings is counted here the way the script does (the crate goes away, 127@ "loaded" +1; the HUD counter and the
// end of the part read 127@ every frame). Locals are the mission's (LocalVariablesForCurrentMission).
int& MissionLocal(int n) { return reinterpret_cast<int*>(CTheScripts::LocalVariablesForCurrentMission)[n]; }

bool DeliverRobbingCrate(CObject* object, unsigned who)
{
    if (!CConfigLoader::ms_bCoopRobbingGuestDeliver || !CConfigLoader::ms_bVanillaScripts) return false;
    bool running = false;
    for (auto s = CTheScripts::pActiveScripts; s; s = s->m_pNext)
        if (s->m_bIsMission && !_strnicmp(s->m_szName, "RYDER2", 8) && COpCodeSync::IsScriptSynced(s)) running = true;
    if (!running || !object || !object->IsVTableValid() || !object->m_matrix) return false;
    const int handle = CPools::GetObjectRef(object);
    bool isCrate = false;
    for (int i = 0; i < 25 && !isCrate; ++i) isCrate = MissionLocal(62 + i) == handle;  // 62@(i,25i): the depot crates
    if (!isCrate) return false;
    auto* mule = CPools::GetVehicle(MissionLocal(95));  // 95@: the Mule
    if (!mule || !mule->IsVTableValid() || !mule->m_matrix || mule->m_fHealth <= 0.0f || mule->m_nModelIndex != 414)
        return false;
    if (MissionLocal(127) >= MissionLocal(200)) return false;  // 127@ of 200@: all loaded already
    const CMatrix& m = *mule->m_matrix;
    const CVector zone = m.pos + m.up * -2.61f + m.at * 1.04f;
    const CVector d = object->m_matrix->pos - zone;
    if (std::fabs(d.x) > 0.71f * 1.3f || std::fabs(d.y) > 1.0f * 1.3f || std::fabs(d.z) > 1.12f * 1.3f) return false;
    Command<Commands::DELETE_OBJECT>(handle);
    MissionLocal(127) += 1;
    CMissionMirror::LogEventf("guest-crate-delivered player=%u loaded=%d of %d", who, MissionLocal(127), MissionLocal(200));
    return true;
}
}  // namespace

void CMissionObjectMoves::Reset()
{
    for (const auto& [id, claim] : claims) Unclaim(id);
    claims.clear();
    movers.clear();
}

void CMissionObjectMoves::Process()
{
    if (!GuestTalks())
    {
        if (!claims.empty())
        {
            for (const auto& [id, claim] : claims) Unclaim(id);
            claims.clear();
        }
        movers.clear();
        return;
    }
    const uint32_t now = GetTickCount();
    // authority: a mover that went quiet (left, died, disconnected) let go of it where his last report put it
    for (auto it = movers.begin(); it != movers.end();)
    {
        if (now - it->second.heard <= 1500)
        {
            ++it;
            continue;
        }
        if (auto* object = Owned(it->first); object && !object->m_pAttachedTo) Wake(object);
        it = movers.erase(it);
    }
    for (auto it = claims.begin(); it != claims.end();)
    {
        if (!CObjectSync::Resolve(it->first))
        {
            // the owner's script consumed the object (a crate loaded): the copy is gone, nothing to restore
            it = claims.erase(it);
            continue;
        }
        if (now - it->second.told > 1500)
        {
            Unclaim(it->first);  // my machine stopped reporting: back to the owner's sync
            it = claims.erase(it);
            continue;
        }
        ++it;
    }
}

bool CMissionObjectMoves::IsClaimed(uint32_t objectid)
{
    return !claims.empty() && claims.count(objectid) != 0;
}

bool CMissionObjectMoves::Claim(uint32_t objectid)
{
    auto* copy = CObjectSync::Resolve(objectid);
    if (!GuestTalks() || !copy) return false;
    SetPushable(copy, true);
    const auto it = claims.find(objectid);
    if (it != claims.end())
    {
        it->second.told = 0;  // Report sends again at once
        return false;
    }
    claims.emplace(objectid, ClaimState());
    Report(objectid);
    return true;
}

void CMissionObjectMoves::Report(uint32_t objectid)
{
    auto it = claims.find(objectid);
    auto* copy = CObjectSync::Resolve(objectid);
    if (it == claims.end() || !copy || !copy->m_matrix) return;
    const uint32_t now = GetTickCount();
    if (now - it->second.told < 100) return;
    it->second.told = now;
    it->second.x = copy->m_matrix->pos.x;
    it->second.y = copy->m_matrix->pos.y;
    it->second.z = copy->m_matrix->pos.z;
    D p{};
    p.objectid = objectid;
    p.pos = copy->m_matrix->pos;
    p.right = copy->m_matrix->right;
    p.forward = copy->m_matrix->up;
    p.area = copy->m_nAreaCode;
    p.released = it->second.releasing ? 1 : 0;
    GetPacketFactory().Send(p);
}

void CMissionObjectMoves::Unclaim(uint32_t objectid)
{
    auto it = claims.find(objectid);
    if (it == claims.end()) return;
    auto* copy = CObjectSync::Resolve(objectid);
    if (copy)
    {
        it->second.told = 0;
        it->second.releasing = true;
        Report(objectid);  // final position before the owner's sync takes the copy back
        SetPushable(copy, false);
    }
    claims.erase(it);
}

void CMissionObjectMoves::Receive(const D& p)
{
    if (!On() || !CNetwork::m_bAuthenticated || !CCoopMission::IsScriptAuthority() || !CCoopMission::IsRunning())
        return;
    auto sender = CNetworkPlayerManager::GetPlayer(p.senderPlayerid);
    if (!sender || sender->m_bIndividualMode || sender->m_bDead || !CCoopMission::IsParticipant(sender->m_playerId)) return;
    const uint32_t now = GetTickCount();
    auto it = movers.find(p.objectid);
    if (it != movers.end() && it->second.player != sender->m_playerId)
    {
        // one owner at a time: a new mover only takes over once the old one stopped reporting or left
        if (now - it->second.heard <= 1500 && CNetworkPlayerManager::GetPlayer(it->second.player)) return;
    }
    if (p.released)
        movers.erase(p.objectid);  // let go: open for the next one at once
    else
        movers[p.objectid] = {sender->m_playerId, now};
    if (ApplyToOwned(p.objectid, p))
    {
        CMissionMirror::LogEventf("object-move id=%u player=%u%s", p.objectid, p.senderPlayerid.value.value(),
                                  p.released ? " released" : "");
        if (DeliverRobbingCrate(Owned(p.objectid), p.senderPlayerid.value.value()))
            movers.erase(p.objectid);
    }
}
