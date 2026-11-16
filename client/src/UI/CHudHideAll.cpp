#include "stdafx.h"
#include "CHudHideAll.h"
#include "CChat.h"
#include "CConfigLoader.h"
#include "CTestMode.h"
#include "CCheatMenu.h"
#include "CModeMenu.h"

static bool ms_bWasKeyDown = false;
static uint32_t ms_nNoticeUntil = 0;

// 2D draw functions called from Render2dStuff / the after-fade pass (gta-reversed addresses); none of them takes
// arguments the caller cleans up differently from a plain `ret` (cdecl / thiscall without stack args)
static const uintptr_t HUD_FUNCTIONS[] = {
    0x58FAE0,  // CHud::Draw (radar, weapon, money, health, clock, stars, help text, counters)
    0x58D490,  // CHud::DrawAfterFade (area name, subtitles, mission title, odd job message)
    0x69EFC0,  // CMessages::Display (big messages)
    0x43CEC0,  // CDarkel::DrawMessages
    0x447790,  // CGarages::PrintMessages
    0x455000,  // CPickups::RenderPickUpText
    0x507030,  // CAudioEngine::DisplayRadioStationName
    0x44CC20,  // COnscreenTimer::ProcessForDisplay (mission timers and counters)
};
static uint8_t ms_aOriginalBytes[sizeof(HUD_FUNCTIONS) / sizeof(HUD_FUNCTIONS[0])];

static void ApplyGameHud(bool hidden)
{
    for (size_t i = 0; i < sizeof(HUD_FUNCTIONS) / sizeof(HUD_FUNCTIONS[0]); i++)
    {
        const uintptr_t address = HUD_FUNCTIONS[i];
        if (hidden)
        {
            ms_aOriginalBytes[i] = *reinterpret_cast<uint8_t*>(address);
            patch::SetUChar(address, 0xC3);
        }
        else
        {
            patch::SetUChar(address, ms_aOriginalBytes[i]);
        }
    }
}

bool CHudHideAll::IsChatHidden()
{
    return ms_bActive && !CChat::m_bInputActive && GetTickCount() >= ms_nNoticeUntil;
}

void CHudHideAll::Init()
{
    Events::gameProcessEvent.after += []
    {
        if (!CConfigLoader::ms_bHudHideAllF2)
        {
            if (ms_bActive)
            {
                ms_bActive = false;
                ApplyGameHud(false);
            }
            return;
        }

        // global key state: with 2 GTA on one PC only the focused window reacts
        const bool keyDown = CTestMode::IsGameWindowFocused() && (GetAsyncKeyState(VK_F2) & 0x8000) != 0;
        const bool canToggle = !CChat::m_bInputActive && !FrontEndMenuManager.m_bMenuActive;

        if (keyDown && !ms_bWasKeyDown && canToggle)
        {
            ms_bActive = !ms_bActive;
            ApplyGameHud(ms_bActive);
            if (ms_bActive)
            {
                // the ImGui menus are not drawn while hidden: close them so they keep no input captured
                CModeMenu::SetOpen(false);
                CCheatMenu::SetOpen(false);
            }
            ms_nNoticeUntil = GetTickCount() + 2500;
            CChat::AddMessage(ms_bActive ? "HUD oculto (F2 para mostrarlo)" : "HUD visible");
        }
        ms_bWasKeyDown = keyDown;
    };
}
