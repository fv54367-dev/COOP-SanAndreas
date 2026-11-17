#include "stdafx.h"
#include "GameHooks.h"
#include "CKeySync.h"
#include <CCutsceneMgr.h>
#include <CCutsceneSkipVote.h>
#include <CWeatherSync.h>
#include <CCoronas.h>
#include <CCoopMission.h>
#include "CCutsceneHidden.h"

static void __cdecl CMenuManager__DrawFrontEnd_FixChat_Hook(float alpha)
{
    CFont::SetAlphaFade(alpha);

    if (CChat::m_bInputActive)
        CChat::ToggleInput(false);
}

static void __cdecl CClock__RestoreClock_Hook()
{
    CClock::RestoreClock();
    CWeatherSync::SyncCurrentState();
}

static void __cdecl CClock__SetGameClock_Hook(unsigned char h, unsigned char m, unsigned char d)
{
    CClock::SetGameClock(h, m, d);
    CWeatherSync::SyncCurrentState();
}

static void __declspec(naked) ProcessCheat_Hook1()
{
    __asm
    {
        mov byte ptr ds : [0x969110] , bl

        call CWeatherSync::SyncCurrentState

        push 0x438589
        retn
    }
}
static void __declspec(naked) ProcessCheat_Hook2()
{
    __asm
    {
        mov byte ptr ds : [0x969110] , bl

        call CWeatherSync::SyncCurrentState

        push 0x4385A3
        retn
    }
}

DWORD CRenderer__AddEntityToRenderList_Hook_Continue = 0x5534B5;
// CRenderer::AddEntityToRenderList(CEntity* entity, float distance) is cdecl: the entity is the first stack argument.
// An entity without RwObject (m_pRwObject, +0x18) is skipped, it would crash in CVisibilityPlugins::GetClumpAlpha
static void __declspec(naked) CRenderer__AddEntityToRenderList_Hook()
{
    __asm
    {
        mov eax, [esp + 4]
        test eax, eax
        jz skip
        mov eax, [eax + 0x18]
        test eax, eax
        jz skip

        // the replaced instructions
        push esi
        mov esi, [esp + 4 + 4]
        jmp CRenderer__AddEntityToRenderList_Hook_Continue

    skip:
        retn
    }
}

uintptr_t CTheZones__Update_Dest = 0x0;
CControllerState oldControllerState;

static void __cdecl CTheZones__Update_Hook()
{
    plugin::CallDyn(CTheZones__Update_Dest);

    CPlayerPed* pPlayerPed = FindPlayerPed(0);
    if (!CNetwork::m_bAuthenticated || !pPlayerPed)
    {
        return;
    }

    // also while the controls are disabled: the host's minigames (pool) read the raw pad, see CNetworkPadSync
    CPad* pad = CPad::GetPad(0);
    CControllerState newState = pad->NewState;

    if (CUtil::CompareControllerStates(oldControllerState, newState))
        return;

    oldControllerState = newState;

    Packets::Players::KeyPressed keyPressed{};
    CKeySync::CollectState(keyPressed.keySnapshot);
    keyPressed.currentRotation = pPlayerPed->m_fCurrentRotation;
    keyPressed.aimingRotation = pPlayerPed->m_fAimingRotation;
    GetPacketFactory().Send(keyPressed);
}

// the black bars of cutscenes and widescreen scenes (gta-reversed Camera.cpp CCamera::DrawBordersForWideScreen, 0x514860):
// asked off by the owner (testeo 17-11: they hid half of every cutscene); cutscene-black-bars=true brings them back
void __fastcall CCamera__DrawBordersForWideScreen_Hook(CCamera* camera, void*)
{
    if (CConfigLoader::ms_bCutsceneBlackBars)
        plugin::CallMethod<0x514860, CCamera*>(camera);
}

void CCutsceneMgr__StartCutscene_Hook()
{
    CCutsceneMgr::StartCutscene();
    if (CCoopMission::IsScriptAuthority())
    {
        Packets::Scripts::StartCutscene packet{};
        packet.currArea = static_cast<eVisibleArea>(CGame::currArea);
        strncpy_s(packet.name, CCutsceneMgr::ms_cutsceneName, 8);
        GetPacketFactory().Send(packet);
    }
}

int __purecall_Hook()
{
    *(char**)0xDEAD =
        "This hook is needed for a more detailed crash log when calling an unimplemented virtual function";
    *(char**)0xDEAD2 = "we will get a full backtrace instead of a single msgbox";
    __asm
    {
        mov eax, 0
        push 0x0
        ret
    }

    return 0;
}

static bool __fastcall CWeapon__FireSniper_Hook(CWeapon* This, SKIP_EDX, CPed* creator, CEntity* victim, CVector* target)
{
    if (creator != FindPlayerPed(0))
    {
        uint8_t moonSize = CCoronas::MoonSize;
        bool result = This->FireSniper(creator, victim, target);

        CCoronas::MoonSize = moonSize;
        return result;
    }

    uint8_t oldMoonSize = CCoronas::MoonSize;
    bool result = This->FireSniper(creator, victim, target);

    if (oldMoonSize != CCoronas::MoonSize)
    {
        Packets::World::UpdateMoonSize packet{};
        packet.moonSize = CCoronas::MoonSize;
        GetPacketFactory().Send(packet);
    }

    return result;
}

#ifdef DEBUG
bool __fastcall CPCKeyboard__GetKeyDown_Hook(int, int, uint16_t key_code, uint8_t use_mode, char* usage)
{
    return GetAsyncKeyState(VK_SHIFT) && GetAsyncKeyState(key_code);
}

bool __fastcall CPCKeyboard__GetJustKeyDown_Hook(int, int, uint16_t key_code, uint8_t use_mode, char* usage)
{
    return GetAsyncKeyState(VK_SHIFT) && GetAsyncKeyState(key_code) & 0x1;
}
#endif

void GameHooks::InjectHooks()
{
    patch::RedirectCall(0x57C2A3, CMenuManager__DrawFrontEnd_FixChat_Hook);

    patch::RedirectCall(0x47F1C7, CClock__RestoreClock_Hook);
    patch::RedirectCall(0x441534, CClock__SetGameClock_Hook);
    patch::RedirectJump(0x438583, ProcessCheat_Hook1);
    patch::RedirectJump(0x43859D, ProcessCheat_Hook2);

    // CRenderer::RenderEverythingBarRoads => CVisibilityPlugins::GetClumpAlpha crash fix
    patch::RedirectJump(0x5534B0, CRenderer__AddEntityToRenderList_Hook);

    // no, here we will not do anything with zones, here we will get and send player keys
    // it is necessary for the menu to be processed correctly
    CTheZones__Update_Dest = injector::GetBranchDestination(0x53BF49).as_int();
    patch::RedirectCall(0x53BF49, CTheZones__Update_Hook);

    // patch::RedirectCall(0x48072B, CCutsceneMgr__StartCutscene_Hook);

    CCutsceneSkipVote::InjectHooks();
    CCutsceneHidden::InjectHooks();

    // the only call of the frame (Render2dStuff); the other one is the credits'
    if (*reinterpret_cast<unsigned char*>(0x53E2B4) == 0xE8 && injector::GetBranchDestination(0x53E2B4).as_int() == 0x514860)
        patch::RedirectCall(0x53E2B4, CCamera__DrawBordersForWideScreen_Hook);

    patch::RedirectJump(PURECALL, __purecall_Hook);

    patch::RedirectCall(0x7424CB, CWeapon__FireSniper_Hook);

#ifdef DEBUG
    /*patch::ReplaceFunction(0x571980, CPCKeyboard__GetKeyDown_Hook);
    patch::ReplaceFunction(0x571970, CPCKeyboard__GetJustKeyDown_Hook);*/
#endif
}
