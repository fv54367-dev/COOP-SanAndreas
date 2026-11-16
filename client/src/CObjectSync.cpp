#include "stdafx.h"
#include "CObjectSync.h"
#include "CMissionObjectMoves.h"
#include "CHomeInvasion.h"
#include "COpCodeSync.h"
#include "CConfigLoader.h"
#include "CMissionMirror.h"
#include <CObject.h>
#include <CCoopMission.h>
#include <unordered_set>

struct SSentObject
{
    uint32_t objectid;
    uint16_t modelId;
    CVector pos;
    CVector right;
    CVector forward;
    uint8_t areaCode;
    bool visible;
    bool collision;
    bool broken;
};

static std::unordered_map<int, SSentObject> ms_sent;  // host: object handle -> what the others have
static uint32_t ms_nNextId = 1;
static bool ms_bResendAll = false;

static std::unordered_map<uint32_t, CObject*> ms_copies;  // the others: objectid -> local copy
static std::unordered_set<uint32_t> ms_brokenCopies;  // the others: objectids whose copy was already broken
// the others: a copy that is being moved by the owner (a crate on a moving truck bed, a thrown one) has no collision
// until it stood still for a while: a solid copy that jumps with every packet pushes cars and CJ around (Catalyst)
struct SMovingCopy
{
    uint32_t lastMove = 0;
    bool collision = true;  // what the owner's packet said
};
static std::unordered_map<uint32_t, SMovingCopy> ms_moving;
constexpr uint32_t MOVING_QUIET_MS = 700;
// Catalyst (RYDER3) is running here (the synced copy of the host's mission)
static bool IsCatalystRunning()
{
    for (auto script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
        if (script->m_bIsMission && !_strnicmp(script->m_szName, "RYDER3", 8))
            return true;
    return false;
}

// the open parachute and its collision object are owned by CParachuteSync (one per skydiving player), never by this sync
static bool IsParachuteModel(int modelId)
{
    return modelId == 3131 || modelId == 3060;
}

static void DestroyCopy(CObject*& pObject)
{
    if (pObject && pObject->IsVTableValid())
    {
        CHomeInvasion::BeforeObjectDelete(pObject);
        CObject* pToDelete = pObject;
        pToDelete->CleanUpOldReference((CEntity**)&pObject);
        CWorld::Remove(pToDelete);
        CWorld::RemoveReferencesToDeletedObject(pToDelete);
        delete pToDelete;
    }
    pObject = nullptr;
}

static void RemoveAllCopies()
{
    for (auto& [id, pObject] : ms_copies)
        DestroyCopy(pObject);
    ms_copies.clear();
    ms_brokenCopies.clear();
    ms_moving.clear();
}

uint32_t CObjectSync::GetNetworkId(int handle)
{
    const auto it = ms_sent.find(handle);
    return it == ms_sent.end() ? 0 : it->second.objectid;
}

CObject* CObjectSync::Resolve(uint32_t id)
{
    const auto it = ms_copies.find(id);
    CObject* object = it == ms_copies.end() ? nullptr : it->second;
    return object && CPools::ms_pObjectPool && CPools::ms_pObjectPool->IsObjectValid(object) &&
        object->IsVTableValid() ? object : nullptr;
}

std::vector<std::pair<uint32_t, CObject*>> CObjectSync::GetPhotoTargets(bool owned)
{
    std::vector<std::pair<uint32_t, CObject*>> result;
    if (owned)
    {
        for (const auto& [handle, sent] : ms_sent)
            if (auto object = CPools::GetObject(handle); object && object->m_nObjectType == OBJECT_MISSION &&
                object->m_nModelIndex == sent.modelId)
                result.emplace_back(sent.objectid, object);
    }
    else
    {
        for (const auto& [id, copy] : ms_copies)
            if (auto object = Resolve(id)) result.emplace_back(id, object);
    }
    return result;
}

void CObjectSync::ResendAll()
{
    ms_bResendAll = true;
}

static bool HasChanged(const SSentObject& sent, const Packets::World::ObjectSync& now)
{
    return (sent.pos - now.pos).Magnitude() > 0.02f || (sent.right - now.right).Magnitude() > 0.005f ||
           (sent.forward - now.forward).Magnitude() > 0.005f || sent.areaCode != now.areaCode ||
           sent.visible != now.visible || sent.collision != now.collision || sent.broken != now.broken ||
           sent.modelId != now.modelId;
}

void CObjectSync::Process()
{
    if (!CNetwork::m_bAuthenticated)
    {
        RemoveAllCopies();
        ms_sent.clear();
        return;
    }

    if (!CCoopMission::IsScriptAuthority())
    {
        ms_sent.clear();
        if (!ms_moving.empty())
        {
            // copies that stood still again get the collision the owner's object has
            const uint32_t now = GetTickCount();
            for (auto it = ms_moving.begin(); it != ms_moving.end();)
            {
                const auto copy = ms_copies.find(it->first);
                if (copy == ms_copies.end() || !copy->second || !copy->second->IsVTableValid())
                {
                    it = ms_moving.erase(it);
                    continue;
                }
                if (now - it->second.lastMove > MOVING_QUIET_MS)
                {
                    if (!ms_brokenCopies.count(it->first)) copy->second->m_bUsesCollision = it->second.collision;
                    it = ms_moving.erase(it);
                    continue;
                }
                ++it;
            }
        }
        return;
    }

    // we just became the host: the copies of the old host's objects go away
    if (!ms_copies.empty())
        RemoveAllCopies();

    bool active = COpCodeSync::IsSyncedMissionRunning();
    std::unordered_set<int> seen;

    if (active && CPools::ms_pObjectPool)
    {
        for (int i = 0; i < CPools::ms_pObjectPool->m_nSize; i++)
        {
            CObject* pObject = CPools::ms_pObjectPool->GetAt(i);
            if (!pObject || pObject->m_nObjectType != OBJECT_MISSION || !pObject->m_matrix)
                continue;

            if (IsParachuteModel(pObject->m_nModelIndex))
                continue;

            int handle = CPools::GetObjectRef(pObject);
            seen.insert(handle);

            Packets::World::ObjectSync packet{};
            packet.modelId = (uint16_t)pObject->m_nModelIndex;
            packet.pos = pObject->m_matrix->pos;
            packet.right = pObject->m_matrix->right;
            packet.forward = pObject->m_matrix->up;  // CMatrix::up is the forward axis (y)
            packet.areaCode = pObject->m_nAreaCode;
            packet.visible = pObject->m_bIsVisible;
            packet.collision = pObject->m_bUsesCollision;
            packet.broken = pObject->m_nObjectFlags.bIsBroken;

            // a broken bottle/glass object also stops being visible and collidable without the flag
            if (!packet.broken && !packet.visible && !packet.collision && pObject->m_nColDamageEffect >= 200)
                packet.broken = true;

            auto it = ms_sent.find(handle);
            if (it != ms_sent.end() && !ms_bResendAll && !HasChanged(it->second, packet))
                continue;

            if (it == ms_sent.end())
            {
                it = ms_sent.emplace(handle, SSentObject{}).first;
                it->second.objectid = ms_nNextId++;
            }

            packet.objectid = it->second.objectid;
            GetPacketFactory().Send(packet);

            it->second.modelId = packet.modelId;
            it->second.pos = packet.pos;
            it->second.right = packet.right;
            it->second.forward = packet.forward;
            it->second.areaCode = packet.areaCode;
            it->second.visible = packet.visible;
            it->second.collision = packet.collision;
            it->second.broken = packet.broken;
        }
    }
    ms_bResendAll = false;

    // deleted (or mission over): remove the copies
    for (auto it = ms_sent.begin(); it != ms_sent.end();)
    {
        if (seen.count(it->first))
        {
            ++it;
            continue;
        }

        Packets::World::ObjectRemove packet{};
        packet.objectid = it->second.objectid;
        GetPacketFactory().Send(packet);
        it = ms_sent.erase(it);
    }
}

static bool LoadModel(int modelId)
{
    if (CStreaming::ms_aInfoForModel[modelId].m_nLoadState == LOADSTATE_LOADED)
        return true;

    CStreaming::RequestModel(modelId, eStreamingFlags::MISSION_REQUIRED);
    CStreaming::LoadAllRequestedModels(false);
    return CStreaming::ms_aInfoForModel[modelId].m_nLoadState == LOADSTATE_LOADED;
}

void CObjectSync::HandleSync(Packets::World::ObjectSync* pPacket)
{
    if (CCoopMission::IsScriptAuthority())
        return;

    // CParachuteSync creates these on its own; never spawn a copy of another player's parachute as a mission object
    if (IsParachuteModel(pPacket->modelId))
        return;

    if (CHomeInvasion::IsCarried(pPacket->objectid) && Resolve(pPacket->objectid)) return;
    // a guest that is moving the object with his own machine owns the copy meanwhile (T70)
    if (CMissionObjectMoves::IsClaimed(pPacket->objectid)) return;

    CObject*& pObject = ms_copies[pPacket->objectid];

    if (pObject && pObject->m_nModelIndex != pPacket->modelId)
    {
        DestroyCopy(pObject);
        ms_brokenCopies.erase(pPacket->objectid);
    }

    // no copy yet: any remembered broken state belongs to an older copy
    if (!pObject)
        ms_brokenCopies.erase(pPacket->objectid);

    bool wasBroken = ms_brokenCopies.count(pPacket->objectid) != 0;

    bool created = false;
    if (!pObject)
    {
        if (pPacket->modelId == 0 || !LoadModel(pPacket->modelId))
        {
            ms_copies.erase(pPacket->objectid);
            return;
        }

        CObject* pNew = CObject::Create(pPacket->modelId);
        if (!pNew)
        {
            ms_copies.erase(pPacket->objectid);
            return;
        }

        pNew->m_nObjectType = OBJECT_MISSION;  // the game must not remove it
        // a copy only shows where the host's object is
        pNew->SetIsStatic(true);
        pNew->m_nPhysicalFlags.bDisableMoveForce = true;
        pNew->m_nPhysicalFlags.bInfiniteMass = true;
        pObject = pNew;
        pObject->RegisterReference((CEntity**)&pObject);
        created = true;
    }
    else
    {
        CWorld::Remove(pObject);
    }

    CMatrixLink* pMatrix = pObject->m_matrix;
    const bool movedNow = !created && pMatrix && (pMatrix->pos - pPacket->pos).Magnitude() > 0.03f;
    if (!pMatrix)
    {
        pObject->AllocateMatrix();
        pMatrix = pObject->m_matrix;
    }

    CVector right = pPacket->right;
    CVector forward = pPacket->forward;
    pMatrix->right = right;
    pMatrix->up = forward;
    pMatrix->at = CVector(right.y * forward.z - right.z * forward.y, right.z * forward.x - right.x * forward.z,
        right.x * forward.y - right.y * forward.x);
    pMatrix->pos = pPacket->pos;

    pObject->m_nAreaCode = pPacket->areaCode;

    bool breakNow = pPacket->broken && !wasBroken;
    if (created && pPacket->broken)
    {
        // it was already broken on the host: a hidden copy, no effect
        pObject->m_bIsVisible = false;
        pObject->m_bUsesCollision = false;
        ms_brokenCopies.insert(pPacket->objectid);
    }
    else if (wasBroken)
    {
        // the game already broke this copy: leave its visibility/collision to it
    }
    else if (breakNow)
    {
        // the break effect needs a visible, solid object
        pObject->m_bIsVisible = true;
        pObject->m_bUsesCollision = true;
    }
    else
    {
        pObject->m_bIsVisible = pPacket->visible;
        pObject->m_bUsesCollision = pPacket->collision;
        if (CConfigLoader::ms_bObjectMovingNoCollide && IsCatalystRunning())
        {
            auto track = ms_moving.find(pPacket->objectid);
            if (movedNow && pPacket->collision && track == ms_moving.end())
                track = ms_moving.emplace(pPacket->objectid, SMovingCopy{}).first;
            if (track != ms_moving.end())
            {
                if (movedNow) track->second.lastMove = GetTickCount();
                track->second.collision = pPacket->collision;
                pObject->m_bUsesCollision = false;
            }
        }
    }

    pObject->UpdateRwMatrix();
    pObject->UpdateRwFrame();
    CWorld::Add(pObject);

    if (breakNow)
    {
        ms_brokenCopies.insert(pPacket->objectid);
        Command<Commands::BREAK_OBJECT>(CPools::GetObjectRef(pObject), 1);
        if (!pObject->m_nObjectFlags.bIsBroken)
        {
            CVector fxPos = pObject->GetPosition();
            pObject->ObjectDamage(10000.0f, &fxPos, nullptr, nullptr, WEAPON_PISTOL);
        }
    }

    (void)created;
}

void CObjectSync::HandleRemove(Packets::World::ObjectRemove* pPacket)
{
    ms_brokenCopies.erase(pPacket->objectid);
    ms_moving.erase(pPacket->objectid);

    auto it = ms_copies.find(pPacket->objectid);
    if (it == ms_copies.end())
        return;

    DestroyCopy(it->second);
    ms_copies.erase(it);
}
