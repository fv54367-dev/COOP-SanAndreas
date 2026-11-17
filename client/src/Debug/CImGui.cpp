#include "CImGui.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx9.h"
#include "CPacketTimeline.h"
#include "MissionRunner.h"
#include "UI/CCheatMenu.h"
#include "UI/CModeMenu.h"
#include "UI/CHudHideAll.h"
#include "CVoice.h"
#include "CMinigameVs.h"
#include "CVsLobby.h"
#include "CVsRace.h"
#include "CCoopMission.h"
#include "CConfigLoader.h"
#include "CMissionHints.h"
#include <CFont.h>
#include <CSprite2d.h>

ImFont* pFont;
ImFont* pFontSa;

void InitFonts()
{
    char windowsDir[MAX_PATH];
    GetWindowsDirectoryA(windowsDir, MAX_PATH);
    std::string fontsDir = std::string(windowsDir) + "\\Fonts\\";

    ImGuiIO& io = ImGui::GetIO();
    pFont = nullptr;

    std::string segoePath = fontsDir + "segoeui.ttf";
    if (FileExists(segoePath.c_str()))
    {
        pFont = io.Fonts->AddFontFromFileTTF(segoePath.c_str(), 16.0f);
    }

    if (!pFont)
    {
        pFont = io.Fonts->AddFontDefault();
    }

    // menus-estilo-sa: SA's own fonts are game textures (not usable by ImGui); Arial Bold is the closest TTF to the
    // "Arial" subtitles/menu font the game was drawn from
    pFontSa = nullptr;
    std::string arialBoldPath = fontsDir + "arialbd.ttf";
    if (FileExists(arialBoldPath.c_str()))
        pFontSa = io.Fonts->AddFontFromFileTTF(arialBoldPath.c_str(), 17.0f);
    if (!pFontSa)
        pFontSa = pFont;
}

// Palette of CHud/CMenuManager: pause-menu highlight is HUD_COLOUR_LIGHT_BLUE (172,203,241); panels are black with alpha
static void ApplyStyle(bool sa)
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);
    if (!sa)
    {
        style.Colors[ImGuiCol_WindowBg] = ImColor(13, 19, 33, 255);
        style.Colors[ImGuiCol_ChildBg] = ImColor(24, 31, 47, 255);
        style.ChildRounding = 3.0f;
        style.WindowRounding = 0.0f; style.FrameRounding = 0.0f; style.GrabRounding = 0.0f;
        style.WindowBorderSize = 1.0f; style.FrameBorderSize = 0.0f; style.WindowPadding = ImVec2(8.0f, 8.0f);
        style.ItemSpacing = ImVec2(8.0f, 4.0f); style.FramePadding = ImVec2(4.0f, 3.0f);
        style.PopupRounding = 0.0f; style.ScrollbarRounding = 9.0f; style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
        return;
    }

    const float scale = RsGlobal.maximumHeight > 0 ? RsGlobal.maximumHeight / 900.0f : 1.0f; // like the HUD: tied to resolution
    const ImVec4 black = ImColor(0, 0, 0, 190);
    const ImVec4 blue = ImColor(172, 203, 241, 255);
    const ImVec4 text = ImColor(225, 225, 225, 255);
    const ImVec4 blueSoft = ImColor(172, 203, 241, 70);
    const ImVec4 blueMid = ImColor(172, 203, 241, 120);
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = ImColor(130, 130, 130, 255);
    c[ImGuiCol_WindowBg] = black;
    c[ImGuiCol_ChildBg] = ImColor(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImColor(0, 0, 0, 230);
    c[ImGuiCol_Border] = ImColor(0, 0, 0, 255);
    c[ImGuiCol_TitleBg] = ImColor(0, 0, 0, 230);
    c[ImGuiCol_TitleBgActive] = ImColor(0, 0, 0, 230);
    c[ImGuiCol_TitleBgCollapsed] = ImColor(0, 0, 0, 230);
    c[ImGuiCol_FrameBg] = ImColor(0, 0, 0, 160);
    c[ImGuiCol_FrameBgHovered] = blueSoft;
    c[ImGuiCol_FrameBgActive] = blueMid;
    c[ImGuiCol_CheckMark] = blue;
    c[ImGuiCol_Button] = ImColor(0, 0, 0, 150);
    c[ImGuiCol_ButtonHovered] = blueMid;   // selected row of a script menu: light-blue highlight
    c[ImGuiCol_ButtonActive] = blue;
    c[ImGuiCol_Header] = blueSoft;
    c[ImGuiCol_HeaderHovered] = blueMid;
    c[ImGuiCol_HeaderActive] = blue;
    c[ImGuiCol_Separator] = ImColor(172, 203, 241, 90);
    c[ImGuiCol_SliderGrab] = blue;
    c[ImGuiCol_SliderGrabActive] = text;
    c[ImGuiCol_ScrollbarBg] = ImColor(0, 0, 0, 120);
    c[ImGuiCol_ScrollbarGrab] = blueMid;
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.PopupRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.WindowBorderSize = 2.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    style.WindowPadding = ImVec2(12.0f * scale, 10.0f * scale);
    style.ItemSpacing = ImVec2(8.0f * scale, 6.0f * scale);
    style.FramePadding = ImVec2(8.0f * scale, 4.0f * scale);
}

// menus-estilo-sa: ImGui cannot use the game's texture fonts, so after it draws, each title bar is covered and its
// title written again with the game's own Pricedown (CFont, FONT_PRICEDOWN), like the HUD's money and mission titles
static void DrawSaTitles()
{
    bool bAny = false;
    for (ImGuiWindow* window : GImGui->Windows)
    {
        if (!window->Active || window->Hidden || (window->Flags & (ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_ChildWindow |
            ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_Popup)))
            continue;
        const std::string title = CMissionHints::ToGameText(std::string(window->Name, ImGui::FindRenderedTextEnd(window->Name) - window->Name));
        if (title.empty())
            continue;
        const ImRect bar = window->TitleBarRect();
        CSprite2d::DrawRect(CRect(bar.Min.x, bar.Min.y, bar.Max.x, bar.Max.y), CRGBA(0, 0, 0, 255));
        // CFont::GetHeight = scale.y * 18 (gta-reversed Font.cpp): the title fills ~85% of the bar
        const float scaleY = bar.GetHeight() * 0.85f / 18.0f;
        CFont::SetScale(scaleY * 0.55f, scaleY);
        CFont::SetFontStyle(FONT_PRICEDOWN);
        CFont::SetProportional(true);
        CFont::SetJustify(false);
        CFont::SetBackground(false, false);
        CFont::SetOrientation(ALIGN_CENTER);
        CFont::SetCentreSize(bar.GetWidth());
        CFont::SetDropShadowPosition(0);
        CFont::SetEdge(1);
        CFont::SetDropColor(CRGBA(0, 0, 0, 255));
        CFont::SetColor(CRGBA(172, 203, 241, 255));
        CFont::PrintString((bar.Min.x + bar.Max.x) * 0.5f, bar.Min.y + bar.GetHeight() * 0.05f, title.c_str());
        bAny = true;
    }
    if (bAny)
    {
        CFont::DrawFonts();
        CFont::SetEdge(0);
        CFont::SetOrientation(ALIGN_LEFT);
    }
}

void InitStyles()
{
    ApplyStyle(CConfigLoader::ms_bMenusEstiloSa);
}

void CImGui::SetActive(bool bActive)
{
    ms_bActive = bActive;

    if (ms_bActive)
    {
        CPad::GetPad(0)->DisablePlayerControls |= 0x200;
    }
    else
    {
        CPad::GetPad(0)->DisablePlayerControls &= ~0x200;
    }

    static_cast<IDirect3DDevice9*>(RwD3D9GetCurrentD3DDevice())->ShowCursor(ms_bActive);
    ImGui::GetIO().MouseDrawCursor = ms_bActive;
}

void CImGui::Init()
{
    Events::initRwEvent += []
    {
        ImGui::CreateContext();
        ImGui_ImplWin32_Init(RsGlobal.ps->window);
        ImGui_ImplWin32_EnableDpiAwareness();

        ImGui_ImplDX9_Init(static_cast<IDirect3DDevice9*>(RwD3D9GetCurrentD3DDevice()));

        ImGuiIO& io = ImGui::GetIO();
        io.MouseDrawCursor = false;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        io.IniFilename = nullptr;

        InitStyles();
        InitFonts();

        ImGui_ImplDX9_InvalidateDeviceObjects();
        ImGui_ImplDX9_CreateDeviceObjects();
    };

    Events::drawAfterFadeEvent += []
    {
        const bool bDraw = (CImGui::ms_bActive || CCheatMenu::ms_bOpen || CModeMenu::ms_bOpen || CMinigameVs::ShouldDraw() || CCoopMission::ShouldDraw() || CVsLobby::ShouldDraw() || CVsRace::ShouldDrawHud() || CVoice::ShouldDraw()) && !FrontEndMenuManager.m_bMenuActive && !CHudHideAll::IsActive();
        if (bDraw && !ms_bFrameActive)
        {
            // drop whatever input arrived while no frame ran and start from the real cursor position, otherwise the
            // trickled queue replays old mouse positions (the screen centre the game warps to) for a while
            ImGuiIO& io = ImGui::GetIO();
            io.ClearEventsQueue();
            io.ClearInputKeys();
            io.ClearInputMouse();
            POINT pos;
            if (GetCursorPos(&pos) && ScreenToClient(RsGlobal.ps->window, &pos))
                io.AddMousePosEvent((float)pos.x, (float)pos.y);
        }
        ms_bFrameActive = bDraw;

        if (bDraw)
        {
            ImGui_ImplDX9_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            if ((CImGui::ms_bActive || CCheatMenu::ms_bOpen || CModeMenu::ms_bOpen) && ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))
            {
                CPad::GetPad(0)->DisablePlayerControls |= 0x200;
            }
            else
            {
                CPad::GetPad(0)->DisablePlayerControls &= ~0x200;
            }

            // reapplied per frame: cheap, and the F1 config can flip menus-estilo-sa at runtime
            static int s_appliedSa = -1;
            if (s_appliedSa != (CConfigLoader::ms_bMenusEstiloSa ? 1 : 0))
            {
                s_appliedSa = CConfigLoader::ms_bMenusEstiloSa ? 1 : 0;
                ApplyStyle(CConfigLoader::ms_bMenusEstiloSa);
            }
            ImGui::PushFont(CConfigLoader::ms_bMenusEstiloSa && pFontSa ? pFontSa : pFont);

            CVoice::Draw();
            CMinigameVs::Draw();
            CCoopMission::Draw();
            CVsLobby::Draw();
            CVsRace::DrawHud();

            if (CCheatMenu::ms_bOpen)
            {
                CCheatMenu::Draw();
            }

            if (CModeMenu::ms_bOpen)
            {
                CModeMenu::Draw();
            }

            if (CImGui::ms_bActive)
            {
            ImGui::Begin("Debug", nullptr,
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);

            ImGui::SetWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetWindowSize(ImVec2(0.0, 0.0f));

            ImGui::TextUnformatted("Enter 'D1212' as a cheat-code to de/activate debug menu.");

            static bool bPacketTimeline = false;
            ImGui::Checkbox("Packet Timeline", &bPacketTimeline);
            if (bPacketTimeline)
            {
                CPacketTimeline::DrawUI();
            }

            static bool bMissionRunner = false;
            ImGui::Checkbox("Missions", &bMissionRunner);
            if (bMissionRunner && MissionRunner::DrawUI())
            {
                bMissionRunner = false;
                CImGui::SetActive(false);
            }

            ImGui::End();
            }

            ImGui::PopFont();

            ImGui::EndFrame();
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

            ImGui_ImplDX9_InvalidateDeviceObjects();

            if (CConfigLoader::ms_bMenusEstiloSa)
                DrawSaTitles();
        }
    };

    Events::gameProcessEvent.after += []
    {
        const char ACTIVATE_DEBUG_CHEAT[] = "2121D";  // type `d1212` as a cheat code
        if (strncmp(ACTIVATE_DEBUG_CHEAT, CCheat::m_CheatString, ARRAY_SIZE(ACTIVATE_DEBUG_CHEAT) - 1) == 0)
        {
            CCheat::m_CheatString[0] = '\0';
            CImGui::SetActive(!CImGui::ms_bActive);
        }
    };
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CImGui::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    // ImGui queues every event until the next NewFrame: with no frame running the queue only grows
    if (!ms_bFrameActive)
        return 0;
    return ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam);
}
