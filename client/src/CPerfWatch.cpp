#include "stdafx.h"
#include "CPerfWatch.h"
#include "CConfigLoader.h"
#include "CMissionMirror.h"
#include "CNetworkPedManager.h"
#include "CNetworkVehicleManager.h"
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

static int PoolUsed(CPool<CTask, char[128]>* pool) { return pool ? pool->GetNoOfUsedSpaces() : -1; }
static int PoolSize(CPool<CTask, char[128]>* pool) { return pool ? pool->m_nSize : -1; }

void CPerfWatch::Process()
{
    if (!CConfigLoader::ms_bPerfTelemetry)
        return;

    // per frame: two compares and an add; the rest runs once a minute
    static ULONGLONG s_nLastFrame = 0, s_nNextLog = 0, s_nWindowStart = 0;
    static unsigned int s_nFrames = 0, s_nWorstMs = 0;
    const ULONGLONG now = GetTickCount64();
    if (!s_nNextLog)
    {
        s_nNextLog = now + 60000;
        s_nWindowStart = now;
    }
    if (s_nLastFrame)
    {
        // a pause or loading screen is not a slow frame
        const ULONGLONG dt = now - s_nLastFrame;
        if (dt < 1000)
        {
            ++s_nFrames;
            if (dt > s_nWorstMs) s_nWorstMs = (unsigned int)dt;
        }
    }
    s_nLastFrame = now;
    if (now < s_nNextLog)
        return;
    s_nNextLog = now + 60000;

    PROCESS_MEMORY_COUNTERS_EX pmc = {};
    pmc.cb = sizeof(pmc);
    const unsigned int commitMB = GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))
        ? (unsigned int)(pmc.PrivateUsage >> 20) : 0;

    // streaming info array: loaded models and the ones pinned with KEEP_IN_MEMORY
    int loaded = 0, pinned = 0;
    for (int i = 0; i < 26316; i++)
    {
        const auto& info = CStreaming::ms_aInfoForModel[i];
        if (info.m_nLoadState == LOADSTATE_LOADED)
        {
            loaded++;
            if (info.m_nFlags & KEEP_IN_MEMORY) pinned++;
        }
    }

    const ULONGLONG windowMs = now - s_nWindowStart;
    const unsigned int avgFps = windowMs ? (unsigned int)(s_nFrames * 1000ULL / windowMs) : 0;
    const unsigned int minFps = s_nWorstMs ? 1000 / s_nWorstMs : 0;

    auto* ptrSingle = reinterpret_cast<CPool<CTask, char[128]>*>(CPools::ms_pPtrNodeSingleLinkPool);
    auto* ptrDouble = reinterpret_cast<CPool<CTask, char[128]>*>(CPools::ms_pPtrNodeDoubleLinkPool);
    // CPools::ms_pEntryInfoNodePool (0xB7448C, gta-reversed Pools.cpp)
    auto* entryInfo = *reinterpret_cast<CPool<CTask, char[128]>**>(0xB7448C);
    auto* eventPool = *reinterpret_cast<CPool<CTask, char[128]>**>(0xB744AC);
    auto* ped = reinterpret_cast<CPool<CTask, char[128]>*>(CPools::ms_pPedPool);
    auto* veh = reinterpret_cast<CPool<CTask, char[128]>*>(CPools::ms_pVehiclePool);
    auto* obj = reinterpret_cast<CPool<CTask, char[128]>*>(CPools::ms_pObjectPool);
    auto* task = reinterpret_cast<CPool<CTask, char[128]>*>(CPools::ms_pTaskPool);
    auto* col = reinterpret_cast<CPool<CTask, char[128]>*>(CPools::ms_pColModelPool);

    CMissionMirror::LogEventf(
        "perf t=%us commit=%uMB stream=%u/%uMB models=%d pinned=%d fps=%u/%u "
        "ped=%d/%d veh=%d/%d obj=%d/%d task=%d/%d event=%d/%d ptrS=%d/%d ptrD=%d/%d entry=%d/%d col=%d/%d netped=%d netveh=%d",
        (unsigned int)(CTimer::m_snTimeInMilliseconds / 1000), commitMB, CStreaming::ms_memoryUsed >> 20,
        CStreaming::ms_memoryAvailable >> 20, loaded, pinned, avgFps, minFps,
        PoolUsed(ped), PoolSize(ped), PoolUsed(veh), PoolSize(veh), PoolUsed(obj), PoolSize(obj), PoolUsed(task), PoolSize(task),
        PoolUsed(eventPool), PoolSize(eventPool), PoolUsed(ptrSingle), PoolSize(ptrSingle), PoolUsed(ptrDouble), PoolSize(ptrDouble),
        PoolUsed(entryInfo), PoolSize(entryInfo), PoolUsed(col), PoolSize(col),
        (int)CNetworkPedManager::m_pPeds.size(), (int)CNetworkVehicleManager::m_pVehicles.size());

    s_nFrames = 0;
    s_nWorstMs = 0;
    s_nWindowStart = now;
}
