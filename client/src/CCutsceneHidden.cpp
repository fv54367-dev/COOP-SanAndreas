#include "stdafx.h"
#include "CCutsceneHidden.h"
#include "CConfigLoader.h"
#include "CObjectSync.h"
#include <CCutsceneMgr.h>
#include <list>
#include <cstdarg>

namespace {
struct Request { int model; CVector pos; char name[33]; };
std::vector<Request> requests;
// List nodes never move while the game holds their addresses.
struct Hidden { CEntity* entity; int type; int handle; };
std::list<Hidden> hidden;
bool active = false, started = false, sampled = false;
DWORD startTime = 0;
char scene[9]{};

void Log(const char* format, ...) {
    char path[MAX_PATH]{};
    if (!GetModuleFileNameA(nullptr, path, MAX_PATH)) return;
    auto* name = strrchr(path, '\\');
    if (!name) return;
    strcpy_s(name + 1, MAX_PATH - (name + 1 - path), "CoopAndreas_cutscene.log");
    FILE* file = fopen(path, "a");
    if (!file) return;
    fprintf(file, "t=%u scene=%.8s ", GetTickCount(), scene);
    va_list args;
    va_start(args, format);
    vfprintf(file, format, args);
    va_end(args);
    fputc('\n', file);
    fclose(file);
}

template<class Pool, class F> void Visit(Pool* pool, F fn) {
    if (!pool) return;
    for (int i = 0; i < pool->m_nSize; ++i)
        if (auto* entity = pool->GetAt(i)) fn(entity, pool->GetRef(entity));
}
template<class F> void VisitWorld(F fn) {
    Visit(CPools::ms_pBuildingPool, fn);
    Visit(CPools::ms_pDummyPool, fn);
    Visit(CPools::ms_pObjectPool, fn);
}
bool IsDecor(CEntity* entity) {
    if (entity->m_nType == ENTITY_TYPE_OBJECT && static_cast<CObject*>(entity)->m_nObjectType == 4) return true;
    for (unsigned i = 0; i < CCutsceneMgr::ms_numCutsceneObjs && i < 50; ++i)
        if (CCutsceneMgr::ms_pCutsceneObjects[i] == entity) return true;
    return false;
}
bool NativeHidden(CEntity* entity) {
    for (unsigned i = 0; i < CCutsceneMgr::ms_iNumHiddenEntities && i < 50; ++i)
        if (CCutsceneMgr::ms_pHiddenEntities[i] == entity) return true;
    return false;
}
void Describe(const char* tag, CEntity* entity) {
    const auto pos = entity->GetPosition();
    int type = entity->m_nType;
    int objectType = type == ENTITY_TYPE_OBJECT ? static_cast<CObject*>(entity)->m_nObjectType : -1;
    const char* origin = IsDecor(entity) ? "cutscene" : "unknown/map-pool";
    if (type == ENTITY_TYPE_OBJECT && !IsDecor(entity) &&
        CObjectSync::GetNetworkId(CPools::GetObjectRef(static_cast<CObject*>(entity)))) origin = "object-sync";
    if (type == ENTITY_TYPE_OBJECT && !IsDecor(entity))
        for (const auto& [id, copy] : CObjectSync::GetPhotoTargets(false))
            if (copy == entity) { origin = "object-sync-copy"; break; }
    bool ours = false;
    for (const auto& h : hidden) if (h.entity == entity) ours = true;
    Log("%s ptr=%p model=%u type=%s objectType=%d visible=%d pos=%.3f,%.3f,%.3f area=%u origin=%s native-hidden=%d extra-hidden=%d",
        tag, entity, entity->m_nModelIndex, type == ENTITY_TYPE_BUILDING ? "building" : type == ENTITY_TYPE_DUMMY ? "dummy" : "object",
        objectType, entity->m_bIsVisible, pos.x, pos.y, pos.z, entity->m_nAreaCode, origin, NativeHidden(entity), ours);
}
void Snapshot(const char* phase) {
    Log("cutscene-hide-snapshot phase=%s requests=%u native-count=%u enabled=%d", phase,
        static_cast<unsigned>(requests.size()), CCutsceneMgr::ms_iNumHiddenEntities, CConfigLoader::ms_bCutsceneHideExtras);
    for (const auto& r : requests) {
        unsigned found = 0, nativeFound = 0;
        VisitWorld([&](auto* entity, int) {
            if (entity->m_nModelIndex != r.model) return;
            auto delta = entity->GetPosition() - r.pos;
            if (delta.Magnitude() <= 30.0f) {
                ++found;
                if (NativeHidden(entity)) ++nativeFound;
                Describe("cutscene-hide-entity", entity);
            }
        });
        Log("cutscene-hide-request name=%.32s model=%d pos=%.3f,%.3f,%.3f found=%u native-found=%u", r.name, r.model, r.pos.x, r.pos.y, r.pos.z, found, nativeFound);
    }
}
// Diagnostic for the new game scene (PROLOG1, interior 14): its map pieces were reported not drawn. Counts the map
// entities around the cutscene offset: in the shown area, visible, and with a loaded RwObject.
void AreaReport(const char* phase) {
    const CVector offset = CCutsceneMgr::ms_cutsceneOffset;
    unsigned total = 0, inArea = 0, visible = 0, loaded = 0, drawable = 0;
    unsigned areas[256]{};
    VisitWorld([&](auto* entity, int) {
        const auto pos = entity->GetPosition();
        if ((CVector2D(pos.x, pos.y) - CVector2D(offset.x, offset.y)).Magnitude() > 60.0f || fabsf(pos.z - offset.z) > 40.0f) return;
        ++total;
        ++areas[entity->m_nAreaCode];
        bool here = entity->m_nAreaCode == CGame::currArea || entity->m_nAreaCode == 13;
        if (here) ++inArea;
        if (entity->m_bIsVisible) ++visible;
        if (entity->m_pRwObject) ++loaded;
        if (here && entity->m_bIsVisible && entity->m_pRwObject) ++drawable;
    });
    char byArea[256]{};
    for (int a = 0, n = 0; a < 256; ++a)
        if (areas[a]) n += sprintf_s(byArea + n, sizeof(byArea) - n, "%d:%u ", a, areas[a]);
    // the baggage belt of PROLOG1 (BAG_BELT2 3969, 62 m from the offset) and its reclaim (3971): reported not drawn
    unsigned belts = 0;
    VisitWorld([&](auto* entity, int) {
        if (entity->m_nModelIndex != 3969 && entity->m_nModelIndex != 3971) return;
        ++belts;
        const auto pos = entity->GetPosition();
        Log("cutscene-area-model phase=%s model=%u type=%d area=%u visible=%d rw=%p pos=%.1f,%.1f,%.1f", phase,
            entity->m_nModelIndex, (int)entity->m_nType, entity->m_nAreaCode, entity->m_bIsVisible, entity->m_pRwObject,
            pos.x, pos.y, pos.z);
    });
    if (!belts) Log("cutscene-area-model phase=%s none", phase);
    auto* player = FindPlayerPed(0);
    Log("cutscene-area phase=%s currArea=%d offset=%.1f,%.1f,%.1f playerArea=%d near=%u in-area=%u visible=%u loaded=%u drawable=%u by-area=%s",
        phase, CGame::currArea, offset.x, offset.y, offset.z, player ? (int)player->m_nAreaCode : -1, total, inArea, visible,
        loaded, drawable, byArea);
}
bool Valid(const Hidden& h) {
    if (!h.entity) return false;
    if (h.type == ENTITY_TYPE_OBJECT) return CPools::ms_pObjectPool && CPools::ms_pObjectPool->GetAtRef(h.handle) == h.entity;
    if (h.type == ENTITY_TYPE_DUMMY) return CPools::ms_pDummyPool && CPools::ms_pDummyPool->GetAtRef(h.handle) == h.entity;
    return CPools::ms_pBuildingPool && CPools::ms_pBuildingPool->GetAtRef(h.handle) == h.entity;
}
void Restore() {
    // gta-reversed CutsceneMgr::DeleteCutsceneData: release reference, then SetIsVisible(true).
    for (auto& h : hidden) if (Valid(h)) {
        h.entity->CleanUpOldReference(&h.entity);
        h.entity->m_bIsVisible = true;
    }
    hidden.clear();
    requests.clear();
    active = started = sampled = false;
}
void HideExtras() {
    if (!CConfigLoader::ms_bCutsceneHideExtras) return;
    if (requests.empty()) return;
    VisitWorld([](auto* candidate, int handle) {
        CEntity* entity = candidate;
        if (!entity->m_bIsVisible || IsDecor(entity)) return;
        for (const auto& r : requests) {
            // an exact twin of the requested object only: 2 m also took the next piece of repeated map models (the
            // conveyor belt of the new game scene), which the cutscene does not redraw
            if (entity->m_nModelIndex != r.model || (entity->GetPosition() - r.pos).Magnitude() > 0.75f) continue;
            for (const auto& h : hidden) if (h.entity == entity && Valid(h)) { entity->m_bIsVisible = false; return; }
            Describe("cutscene-hide-extra", entity);
            hidden.push_back({entity, static_cast<int>(entity->m_nType), handle});
            auto& h = hidden.back();
            entity->RegisterReference(&h.entity);
            // Same visibility path as gta-reversed HideRequestedObjects (0x5AFAD0).
            entity->m_bIsVisible = false;
            return;
        }
    });
}
void __cdecl HideRequested() {
    Restore();
    memcpy(scene, CCutsceneMgr::ms_cutsceneName, 8);
    // The native function resets/reuses the REMOVE counter for hidden entity references.
    unsigned count = CCutsceneMgr::ms_iNumHiddenEntities;
    if (count > 50) Log("cutscene-hide-invalid-count count=%u", count);
    for (unsigned i = 0; i < count && i < 50; ++i) {
        const auto& item = CCutsceneMgr::ms_crToHideItems[i];
        Request r{};
        memcpy(r.name, item.m_szObjectName, 32);
        r.pos = item.m_vecPosn;
        r.model = -1;
        CModelInfo::GetModelInfo(r.name, &r.model);
        requests.push_back(r);
    }
    CCutsceneMgr::HideRequestedObjects();
    active = true;
    Snapshot("native-hide");
    AreaReport("native-hide");
    HideExtras();
}
void Process() {
    if (!active) return;
    if (!CCutsceneMgr::ms_cutsceneProcessing || !CCutsceneMgr::dataFileLoaded ||
        memcmp(scene, CCutsceneMgr::ms_cutsceneName, 8) || (started && !CCutsceneMgr::ms_running)) {
        Restore();
        return;
    }
    if (CCutsceneMgr::ms_running && !started) {
        started = true;
        startTime = GetTickCount();
        Snapshot("start");
        AreaReport("start");
    }
    if (started && !sampled && GetTickCount() - startTime >= 2000) {
        sampled = true;
        Snapshot("start+2s");
        AreaReport("start+2s");
    }
    HideExtras();
}
}

void CCutsceneHidden::InjectHooks() {
    // CCutsceneMgr::Update_overlay call site; verified against 1.0 US exe.
    if (*reinterpret_cast<unsigned char*>(0x5B176A) == 0xE8 &&
        injector::GetBranchDestination(0x5B176A).as_int() == 0x5AFAD0) {
        patch::RedirectCall(0x5B176A, HideRequested);
        Events::gameProcessEvent.after += Process;
    }
}
