#include "stdafx.h"
#include "CrashfixHooks.h"
#include "DamageHooks.h"
#include "CMissionMirror.h"

constexpr unsigned int COLMODEL_POOL_SIZE = 20000;
constexpr unsigned int EVENT_POOL_SIZE = 512;

// TODO: find the cause of those crashes, cuz they dont appear in the vanilla game

// i hope it will work
void __declspec(naked) CVehicleAnimGroup__ComputeAnimDoorOffsets_Hook()
{
    __asm
    {
        // ensure door id < 16
        mov edx, [esp + 8] // get door id
        cmp edx, 16 // compare
        jb cont // if lower than 16, good, exit the hook 

        // zero if out of range
        mov edx, 0
        mov[esp + 8], edx

        cont:
        // code cave
        mov     edx, [esp + 8]
        lea     eax, [edx + edx * 2]

        push 0x6E3D17
        ret
    }
}

static void __declspec(naked) CAnimManager__BlendAnimation_Hook()
{
    __asm
    {
        mov eax, [esp + 4] // get RpClump*
        test eax, eax // check for nullptr

        jnz cont // jump if ok

        retn // exit if not ok

        cont:
        // code cave
        sub esp, 0x14
        mov ecx, [esp + 0x14 + 0x4]

        push 0x4D4617
        ret
    }
}

// 0x4D1680 = FrameUpdateCallBackSkinnedWithVelocityExtraction (RpAnimBlend, gta-reversed; eax = BlendNodeArrays, one stack
// argument cleaned by the caller): its second loop reads the first node without checking it, so a clump with no active
// animation node crashed the game (2026-10-19, ped copies whose animations were gone). Empty list: nothing to update.
static void __declspec(naked) AnimFrameUpdateVelocity_Hook()
{
    __asm
    {
        cmp dword ptr [eax], 0
        jne cont
        cmp dword ptr [eax + 4], 0
        jne cont
        retn

        cont:
        // code cave
        sub esp, 0x68
        push ebp
        push esi

        push 0x4D1685
        ret
    }
}

// 0x4D2B90 = FrameUpdateCallBackSkinned (RpAnimBlend, [esp + 8] = the same kind of list): same unchecked first node, same
// empty list crash (2026-10-19). Empty or missing list: nothing to update.
static void __declspec(naked) AnimFrameUpdateSkinned_Hook()
{
    __asm
    {
        mov eax, [esp + 8]
        test eax, eax
        jz skip
        cmp dword ptr [eax], 0
        jne cont
        cmp dword ptr [eax + 4], 0
        jne cont

        skip:
        retn

        cont:
        // code cave
        sub esp, 0x3C
        push ebp
        mov ebp, [esp + 0x44]

        push 0x4D2B98
        ret
    }
}

// 0x4442D0 = CPool<CPed>::GetIndex (thunk to the protected stub 0x156CD40; ecx = pool, one stack argument): a null ped
// gives a huge negative index and reads the flags array far outside the heap, so the script opcode "get char in car
// passenger seat" (0x432, SWEET3 Drive-Thru, empty seat) crashed the host (2026-10-19, dump 23-59-22). Null: no ped (-1).
static void __declspec(naked) CPool__GetPedRef_Hook()
{
    __asm
    {
        cmp dword ptr [esp + 4], 0
        jne cont
        mov eax, 0xFFFFFFFF
        retn 4

        cont:
        mov eax, 0x156CD40
        jmp eax
    }
}

// 0x5E57F0 = CPed::PlayFootSteps (ecx = ped, no stack arguments): it takes the first animation of the ped's clump and reads
// it without checking, so a ped whose clump has no animation at all crashed the game (2026-10-20, a civilian in Drive-Thru,
// same family as the RpAnimBlend guards above). No clump or no animation: no footsteps.
static void __declspec(naked) CPed__PlayFootSteps_Hook()
{
    __asm
    {
        mov eax, [ecx + 0x18]
        test eax, eax
        jz skip

        push ecx
        push eax
        mov eax, 0x4D15E0  // RpAnimBlendClumpGetFirstAssociation
        call eax
        add esp, 4
        pop ecx
        test eax, eax
        jz skip

        // code cave
        push -1
        push 0x83D978

        push 0x5E57F7
        ret

        skip:
        retn
    }
}

// CStencilShadows::Process (0x711D90) calls RenderForObject 0x710310 (cdecl, 2 stack args) at 0x711E41. An object in the stencil
// shadow list whose CCollisionData has no m_pShadowVertices (collision reloaded or freed behind the list) crashed in
// CCollisionData::GetShadTrianglePoint 0x40F640 (2026-10-20, Jugador2, coop Vigilante). Skip that object's shadow this frame.
static void __cdecl CStencilShadows__RenderForObject_Hook(void* object, void* arg)
{
    __try
    {
        plugin::Call<0x710310, void*, void*>(object, arg);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

// CEntity::DestroyEffects 0x533BF0 calls Fx_c::DestroyEntityFx 0x4A1280 (g_fx 0xA9AE00, one stack argument the callee pops) at
// 0x533C5A for a building with a particle 2dfx. Loading a savegame from the menu in a coop session (2026-10-20, the three test
// games) crashed right after the load, when the game streamed buildings out: an entry of g_fx's entity list pointed to an
// FxSystem_c that was already gone and FxSystem_c::Kill/Stop (0x4AA3F0/0x4AA390) read it (backtrace: CBuilding destructor ->
// DeleteRwObject -> DestroyEffects -> DestroyEntityFx). The entry is unlinked before the Kill, so skipping it only leaks it.
static void __fastcall Fx_c__DestroyEntityFx_Hook(void* This, SKIP_EDX, void* entity)
{
    __try
    {
        plugin::CallMethod<0x4A1280, void*, void*>(This, entity);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

// The root cause of the entry above (gta-reversed Fx.cpp, Game.cpp:307): CGame::ShutDownForRestart calls Fx_c::Reset (0x49EAE0,
// its only call at 0x53C61B), which destroys every FxSystem_c (FxManager_c::DestroyAllFxSystems 0x4A98B0) but leaves g_fx's
// entity list (+0x44) as it is: an entity that kept its RW object through the restart (not streamed out by
// CStreaming::DeleteAllRwObjects, e.g. a big building or one being rendered) keeps a node to a destroyed system, and its later
// DeleteRwObject crashed. Empty the list first with the game's own Fx_c::ExitEntitySystems (0x4A12D0, what Fx_c::Exit does):
// such an entity only loses its particle until its RW object is created again.
static void __fastcall Fx_c__Reset_Hook(void* This)
{
    int nNodes = 0;
    for (void* node = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(This) + 0x44); node != nullptr && nNodes < 100000;
         node = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(node) + 4))
    {
        nNodes++;
    }

    plugin::CallMethod<0x4A12D0, void*>(This);
    plugin::CallMethod<0x49EAE0, void*>(This);
    CMissionMirror::LogEventf("fx-reset entity-systems=%d", nNodes);
}

// diagnostics (2026-10-21, dump 16-47-32): the local player ped was freed from the pool while Players[0].m_pPed still pointed to it
// (INTMAN GET_CHAR_AREA_VISIBLE on $scplayer crashed). Logs who deletes it, then deletes as the game does (CPed::operator delete)
static void __cdecl CPed__operatorDelete_Hook(void* data)
{
    if (data != nullptr && data == (void*)CWorld::Players[0].m_pPed)
    {
        void* frames[16]{};
        const int n = CaptureStackBackTrace(0, 16, frames, nullptr);
        char line[400];
        int len = sprintf_s(line, "local-player-ped-deleted bt=");
        for (int i = 0; i < n && len < 360; i++)
            len += sprintf_s(line + len, sizeof(line) - len, "%p ", frames[i]);
        // raw scan of the stack for return addresses into the exe
        const unsigned int* sp = (const unsigned int*)_AddressOfReturnAddress();
        len += sprintf_s(line + len, sizeof(line) - len, "| raw=");
        for (int i = 0, found = 0; i < 256 && found < 6 && len < 380; i++)
        {
            if (sp[i] >= 0x401000 && sp[i] < 0x858000)
            {
                len += sprintf_s(line + len, sizeof(line) - len, "%x ", sp[i]);
                found++;
            }
        }
        CMissionMirror::LogEvent(line);
    }
    CPools::ms_pPedPool->Delete((CPed*)data);
}

// GET_CHAR_AREA_VISIBLE (0x09E8, case at 0x47B145 in CRunningScript::ProcessCommands2500To2599) reads the ped's
// area byte (CPed +0x2F, gta-reversed CharacterCommands.cpp GetCharAreaVisible) through CPool<CPed>::GetAt
// (0x404910) without a null check. The char handle arrives stale when a network-replayed opcode references a char
// this game does not have: COpCodeSync executes the replayed command through CommandHandlerTable with a local
// script that is not in the active list, so the vanilla handler runs with a handle the pool does not know
// (Jugador1 2026-11-08 20:39:12, start of Sweet's Girl / hoods5, eax=0 at 0x47B15F). INTMAN's
// GET_CHAR_AREA_VISIBLE on $scplayer hit the same family (2026-10-21, only logged until now). Null ped: report
// area 0 (normal world) instead of crashing
static void* __fastcall GetCharAreaVisible_GetAt_Hook(CPool<CPed>* pool, SKIP_EDX, int handle)
{
    void* ped = plugin::CallMethodAndReturn<void*, 0x404910, CPool<CPed>*, int>(pool, handle);
    if (ped == nullptr)
    {
        static char ms_aNullPedArea[0x30]; // zeros: the byte at +0x2F reads as area 0
        return ms_aNullPedArea;
    }
    return ped;
}

// AsciiToGxtChar (0x718600) copies a string byte by byte: the "Advanced" display settings page (CMenuManager::DrawStandardMenus)
// passes it the name of the selected video mode, which is null when that mode is not in the list of exclusive modes (windowed
// test mode, a mode saved for another display): access violation. A null source gives an empty text
static void __cdecl AsciiToGxtChar_Hook(const char* src, char* dst)
{
    if (src == nullptr)
    {
        dst[0] = '\0';
        return;
    }

    while ((*dst++ = *src++) != '\0')
    {
    }
}

// CTaskSimpleFight::FightHitPed (0x61CBA0, crash 0x61CBD9 twice on a friend's game, 2026-10-24 and 25) asks the victim's task
// manager for its active task (CTaskManager::GetActiveTask 0x681720) and calls a virtual of the result at once: a victim with no
// task at all (a network ped whose tasks were flushed) gives null. A stand-in task of type 0 makes the check fail as for any
// other task
static int __fastcall NoTaskGetTaskType_Stub(void*)
{
    return 0;
}

static void* ms_apNoTaskVTable[16] = {
    nullptr, nullptr, nullptr, nullptr, (void*)NoTaskGetTaskType_Stub, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
static void* ms_pNoTask = ms_apNoTaskVTable;

static void* __fastcall FightHitPed_GetActiveTask_Hook(void* taskManager)
{
    // the intelligence pointer of the victim can be null too: the manager arrives as 0x4
    if ((uintptr_t)taskManager > 0x10000)
    {
        if (void* task = plugin::CallMethodAndReturn<void*, 0x681720>(taskManager))
            return task;
    }
    return &ms_pNoTask;
}

// CPools::ms_pEventPool lives at 0xB744AC (gta-reversed Pools.cpp, "new CEventPool(200, \"Event\")"); plugin-sdk
// has no symbol for it. CPool's counters do not depend on the object type, so any CPool alias can read them
static CPool<CTask, char[128]>** GetEventPoolPtr()
{
    return reinterpret_cast<CPool<CTask, char[128]>**>(0xB744AC);
}

// fired by the Add guards below when the event pool is full (CEventPool::New 0x4B5570 returned null): the ped
// misses the event for this frame instead of the game crashing (2026-11-08: write to 0xE inside
// CEventEditableResponse::Clone 0x420ED0, whose CloneEditable returned null). One line every 10 seconds
static void __cdecl LogEventPoolFull()
{
    static unsigned int s_nNextLog = 0;
    if (CTimer::m_snTimeInMilliseconds < s_nNextLog)
        return;
    s_nNextLog = CTimer::m_snTimeInMilliseconds + 10000;
    if (CPool<CTask, char[128]>** ppPool = GetEventPoolPtr(); *ppPool)
        CMissionMirror::LogEventf("event-pool-full used=%d size=%d", (*ppPool)->GetNoOfUsedSpaces(), (*ppPool)->m_nSize);
}

// CEventGroup::Add (0x4AB420, thiscall: ecx = group, [esp+4] = event, [esp+8] = bValid, ends in ret 8) reads the
// event's vtable right away (0x4AB435): a null event happens when CEventGlobalGroup::AddEventsToPed (0x4AB9C0)
// got a null clone from the same full event pool. Null: add nothing (the caller deletes its own clone)
// gta-reversed CEventGroup::Add: reject before response propagation to friends/group.
static bool __cdecl DiscardAllyEvent(CEventGroup* group, void* event)
{
    return DamageHooks::ShouldDiscardAllyEvent(event, group->m_pPed);
}

static void __declspec(naked) CEventGroup__Add_NullEventGuard()
{
    __asm
    {
        cmp dword ptr [esp + 4], 0 // event
        jnz valid_event

        xor eax, eax
        ret 8

        valid_event:
        push ecx
        push edx
        push dword ptr [esp + 12]
        push ecx
        call DiscardAllyEvent
        add esp, 8
        pop edx
        pop ecx
        test al, al
        jz keep_event
        xor eax, eax
        ret 8

        keep_event:
        // replaced prologue 0x4AB420..0x4AB427 (push ecx/ebx/esi, mov esi,[esp+0x10], push edi)
        push ecx
        push ebx
        push esi
        mov esi, [esp + 0x10]
        push edi
        push 0x4AB428
        ret
    }
}

// CEventGroup::Add clones the incoming event at 0x4AB55B (`call dword ptr [edx+0x10]`, the vtable Clone) and
// stores it at once (0x4AB55E mov esi,eax ...): a full event pool makes CloneEditable return null and Clone
// itself writes through it (2026-11-08 crash, write to 0xE). Null clone: log and fall into Add's "not added"
// epilogue at 0x4AB589 (xor eax,eax / ret 8), so nothing is stored and the caller deletes its own clone
static void __declspec(naked) CEventGroup__Add_CloneNullGuard()
{
    __asm
    {
        mov edx, [esi] // event's vtable (replaces 0x4AB557)
        mov ecx, esi
        call dword ptr [edx + 0x10] // event->Clone()
        test eax, eax
        jnz clone_ok

        push eax
        call LogEventPoolFull
        add esp, 4
        pop eax
        push 0x4AB589
        ret

        clone_ok:
        push 0x4AB55E
        ret
    }
}

// CEventVehicleHitAndRun::Clone (0x4B7100, vtable 0x85AFA8 slot 4) builds the copy with the constructor 0x4AE990, which
// calls RegisterReference on the vehicle and the victim without a null check (gta-reversed EventVehicleHitAndRun.cpp:
// "todo: possible nullptr"). The event sits in CEventGlobalGroup and CWorld::Process -> AddEventsToPed (0x4AB9C0) clones
// it for every moving ped; when the car or the ped it points to was deleted meanwhile (a network ped copy destroyed,
// "ped-copy-fail reason=destroy refs=4"), its reference was cleared to null and the clone crashed at 0x571B73
// (2026-11-15 12:14, Jugador1). Such an event is not copied: a null clone is skipped by AddEventsToPed (test esi) and by
// the CEventGroup::Add guards above.
static void* s_pHitAndRunClone = reinterpret_cast<void*>(0x4B7100);
static void __declspec(naked) CEventVehicleHitAndRun__Clone_NullGuard()
{
    __asm
    {
        cmp dword ptr [ecx + 0x0C], 0 // m_victim
        je no_clone
        cmp dword ptr [ecx + 0x10], 0 // m_vehicle
        je no_clone
        jmp dword ptr [s_pHitAndRunClone]

        no_clone:
        xor eax, eax
        ret
    }
}

// A task keeps a raw pointer to its animation and learns it is gone through the finish/delete callback, which
// CAnimBlendAssociation::UpdateBlend (0x4D1490) calls before deleting a faded animation. Destroying the clump
// (ClumpAnimDestruct 0x4D6110 -> RpAnimBlendClumpRemoveAllAssociations 0x4D6C00) deletes them without calling it, so a
// task that outlives the clump keeps a dangling pointer and its later MakeAbortable(IMMEDIATE) writes BlendDelta=-1000
// and SetDefaultFinishCallback into freed memory. 2026-11-17 crash 0x81214A: exactly those 4 fields (+0x1C, +0x30,
// +0x34 = 0x4D1390 DefaultAnimCB, +0x38) over the material list of the merit being created. Same order as UpdateBlend:
// callback, then the default one. A ped being deleted already lost its tasks (their destructors reset the callbacks).
static void __cdecl ClumpAnimDestruct_RemoveAll_Hook(RpClump* clump)
{
    if (CConfigLoader::ms_bAnimDestroyNotify && clump)
    {
        using AnimCB = void(__cdecl*)(void*, void*);
        const int dataOffset = *(int*)0xB5F878;  // RpAnimBlendClump plugin offset
        uintptr_t** clumpData = *(uintptr_t***)((uint8_t*)clump + dataOffset);
        int notified = 0;
        uintptr_t firstCallback = 0;
        uintptr_t* link = clumpData ? *clumpData : nullptr;
        while (link)
        {
            uintptr_t* next = (uintptr_t*)*link;
            uint8_t* assoc = (uint8_t*)link - 4;
            int& type = *(int*)(assoc + 0x30);
            uintptr_t& callback = *(uintptr_t*)(assoc + 0x34);
            void*& callbackData = *(void**)(assoc + 0x38);
            if ((type == 1 || type == 2) && callback != 0 && callback != 0x4D1390)
            {
                const AnimCB fn = (AnimCB)callback;
                void* data = callbackData;
                if (!firstCallback)
                    firstCallback = callback;
                fn(assoc, data);
                type = 1;
                callback = 0x4D1390;
                callbackData = nullptr;
                notified++;
            }
            link = next;
        }
        if (notified)
            CMissionMirror::LogEventf("anim-destroy-notify count=%d first=%08X", notified, (unsigned)firstCallback);
    }
    plugin::Call<0x4D6C00>(clump);
}

void CrashfixHooks::InjectHooks()
{
    patch::SetPointer(0x85AFA8 + 4 * 4, CEventVehicleHitAndRun__Clone_NullGuard);
    patch::RedirectCall(0x61CBD4, FightHitPed_GetActiveTask_Hook);
    patch::RedirectCall(0x533C5A, Fx_c__DestroyEntityFx_Hook);
    patch::RedirectCall(0x53C61B, Fx_c__Reset_Hook);
    patch::RedirectJump(0x718600, AsciiToGxtChar_Hook);
    patch::RedirectCall(0x711E41, CStencilShadows__RenderForObject_Hook);
    patch::RedirectJump(0x6E3D10, CVehicleAnimGroup__ComputeAnimDoorOffsets_Hook);
    patch::RedirectJump(0x4D4610, CAnimManager__BlendAnimation_Hook);
    patch::RedirectJump(0x4D1680, AnimFrameUpdateVelocity_Hook);
    patch::RedirectJump(0x4D2B90, AnimFrameUpdateSkinned_Hook);
    patch::RedirectJump(0x4442D0, CPool__GetPedRef_Hook);
    patch::RedirectJump(0x5E57F0, CPed__PlayFootSteps_Hook);
    patch::RedirectJump(0x5E4760, CPed__operatorDelete_Hook);
    patch::RedirectJump(0x4AB420, CEventGroup__Add_NullEventGuard);
    patch::RedirectJump(0x4AB557, CEventGroup__Add_CloneNullGuard);
    patch::RedirectCall(0x47B15A, GetCharAreaVisible_GetAt_Hook);
    patch::RedirectCall(0x4D6123, ClumpAnimDestruct_RemoveAll_Hook);

    // CPools::Initialise (0x550F10) builds the ColModel pool with 10150 slots (gta-reversed Pools.cpp); the map alone keeps
    // ~10050 in use, so the few vehicle/object models with a collision inside their dff (CCollisionPlugin) fill it and
    // CFileLoader::LoadCollisionModelVer3 writes through the null new CColModel (crash 0x537D12, 2026-10-23). The pool
    // constructor (0x5507C0) allocates from the pushed size, so raising the push is enough
    patch::SetUInt(0x551107, COLMODEL_POOL_SIZE);

    // Same for the Event pool: vanilla 200 slots (`push 200` at 0x551176, stored into ms_pEventPool 0xB744AC). Coop
    // fills them: every fleeing ped adds CEventSeenPanickedPed to the global event group (gta-reversed
    // TaskComplexFleeEntity.cpp:127) and CWorld::Process then clones it for EVERY ped in
    // ms_listMovingEntityPtrs (0xB9ACC8), each Add also cloning for InformGroup/InformRespectedFriends. With the
    // pool full CEventPool::New returns null and CEventEditableResponse::Clone crashed writing through it
    // (2026-11-08, 0x420EDC, write to 0xE with eax=0; the dump showed a healthy CEventSeenPanickedPed being cloned)
    patch::SetUInt(0x551177, EVENT_POOL_SIZE);
}

void CrashfixHooks::ProcessPoolWatch()
{
    static unsigned int s_nNextCheck = 0;
    static int s_nPeak = 0;
    if (!CPools::ms_pColModelPool || CTimer::m_snTimeInMilliseconds < s_nNextCheck)
        return;
    s_nNextCheck = CTimer::m_snTimeInMilliseconds + 5000;

    // a steadily climbing peak across a session means col models leak; a flat one means the map just needs the room
    const int nUsed = CPools::ms_pColModelPool->GetNoOfUsedSpaces();
    if (nUsed >= s_nPeak + 100)
    {
        s_nPeak = nUsed;
        CMissionMirror::LogEventf("colmodel-peak used=%d size=%d", nUsed, CPools::ms_pColModelPool->m_nSize);
    }

    // same watch for the event pool (vanilla 200, raised to 512 above): a session stuck at the raised size means
    // events leak into ped event groups that never tick (e.g. ped copies with no AI), a flat one means it is just
    // the coop churn
    static unsigned int s_nNextEventCheck = 0;
    static int s_nEventPeak = 0;
    if (CPool<CTask, char[128]>** ppPool = GetEventPoolPtr(); *ppPool && CTimer::m_snTimeInMilliseconds >= s_nNextEventCheck)
    {
        s_nNextEventCheck = CTimer::m_snTimeInMilliseconds + 5000;
        const int nEventUsed = (*ppPool)->GetNoOfUsedSpaces();
        if (nEventUsed >= s_nEventPeak + 25)
        {
            s_nEventPeak = nEventUsed;
            CMissionMirror::LogEventf("eventpool-peak used=%d size=%d", nEventUsed, (*ppPool)->m_nSize);
        }
    }
}
