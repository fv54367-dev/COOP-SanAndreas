#include "stdafx.h"
#include "CCutsceneKeepSeat.h"
#include "CConfigLoader.h"
#include "CCoopMission.h"
#include "CMissionMirror.h"
#include <CCutsceneMgr.h>
#include <CPools.h>

namespace
{
struct
{
    bool armed = false, sawScene = false;
    int carRef = -1;
    CVehicle* car = nullptr;
    int seat = -2;  // -1 driver, 0..7 index of m_apPassengers
    uint32_t armedAt = 0, endedAt = 0;
} kept;

bool SceneOn()
{
    return CCutsceneMgr::ms_running || CCutsceneMgr::ms_cutsceneLoadStatus != 0;
}
}

void CCutsceneKeepSeat::BeforeNetworkLoad()
{
    kept = {};
    CPlayerPed* me = FindPlayerPed(0);
    if (!CConfigLoader::ms_bCutsceneKeepSeat || !CNetwork::m_bConnected || CCoopMission::IsScriptAuthority() || !me ||
        me->m_fHealth <= 0.0f || !me->m_nPedFlags.bInVehicle || !me->m_pVehicle)
        return;
    CVehicle* v = me->m_pVehicle;
    int seat = v->m_pDriver == me ? -1 : -2;
    for (int k = 0; k < 8 && seat == -2; ++k)
        if (v->m_apPassengers[k] == me) seat = k;
    if (seat == -2)
        return;
    kept.armed = true;
    kept.car = v;
    kept.carRef = CPools::GetVehicleRef(v);
    kept.seat = seat;
    kept.armedAt = GetTickCount();
    CMissionMirror::LogEventf("cutscene-keep-seat saved seat=%d model=%d", seat, v->m_nModelIndex);
}

void CCutsceneKeepSeat::Process()
{
    if (!kept.armed)
        return;
    const uint32_t now = GetTickCount();
    CPlayerPed* me = FindPlayerPed(0);
    if (!CConfigLoader::ms_bCutsceneKeepSeat || !CNetwork::m_bConnected || !me || me->m_fHealth <= 0.0f ||
        now - kept.armedAt > 300000)
    {
        kept = {};
        return;
    }
    if (SceneOn())
    {
        kept.sawScene = true;
        kept.endedAt = 0;
        return;
    }
    if (!kept.sawScene)
    {
        if (now - kept.armedAt > 20000)
            kept = {};  // the scene never loaded here
        return;
    }
    if (!kept.endedAt)
        kept.endedAt = now;
    if (now - kept.endedAt < 500)
        return;  // the script's own moves right after CLEAR_CUTSCENE come first

    auto done = [](const char* why)
    {
        CMissionMirror::LogEventf("cutscene-keep-seat %s seat=%d", why, kept.seat);
        kept = {};
    };
    if (me->m_nPedFlags.bInVehicle)
        return done("already-in-car");
    CVehicle* v = CPools::GetVehicle(kept.carRef);
    if (!v || v != kept.car || v->m_fHealth <= 0.0f || v->m_nAreaCode != me->m_nAreaCode)
        return done("car-gone");
    if ((v->GetPosition() - me->GetPosition()).Magnitude() > 60.0f)
        return done("car-far");

    const int ped = CPools::GetPedRef(me);
    if (kept.seat == -1 && !v->m_pDriver)
        Command<Commands::WARP_CHAR_INTO_CAR>(ped, kept.carRef);
    else
    {
        // the seat he had, else any free passenger seat of the same car
        int seat = kept.seat >= 0 && kept.seat < v->m_nMaxPassengers && !v->m_apPassengers[kept.seat] ? kept.seat : -1;
        for (int k = 0; k < v->m_nMaxPassengers && k < 8 && seat == -1; ++k)
            if (!v->m_apPassengers[k]) seat = k;
        if (seat == -1)
            return done("seat-taken");
        Command<Commands::WARP_CHAR_INTO_CAR_AS_PASSENGER>(ped, kept.carRef, seat);
    }
    done("warped");
}
