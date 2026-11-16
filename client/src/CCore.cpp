#include "stdafx.h"
#include "CMissionMode.h"
#include "CVsRace.h"
#include "CVsLobby.h"
#include <CMissionMirror.h>
#include "CNetworkPadSync.h"
#include "CCrashLog.h"
#include "Commands/CCustomCommandRegistrar.h"
#include "CDiscordRPC.h"
#include <COpCodeSync.h>
#include <CCustomMenuManager.h>
#include <winuser.h>
#include <CLaunchManager.h>
#include <Debug/CImGui.h>
#include <CGangSync.h>
#include "CTestMode.h"
#include "UI/CHudHideAll.h"
#include "Editor/CMissionEditor.h"
#include "CMissionRulesData.h"

semver_t CCore::Version;

WNDPROC prevWndProc;

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    CChat::WndProc(hWnd, message, wParam, lParam);
    CCustomMenuManager::WndProc(hWnd, message, wParam, lParam);
    if (CImGui::WndProc(hWnd, message, wParam, lParam))
    {
        return TRUE;
    }
    return CallWindowProcW(prevWndProc, hWnd, message, wParam, lParam);
}

void InitWndProc()
{
    HWND hWnd = *(HWND*)0xC97C1C;

    SetWindowTextW(hWnd, L"CoopSA");
    RsGlobal.appName = "CoopSA";

    if (hWnd)
    {
        prevWndProc = (WNDPROC)GetWindowLongW(hWnd, GWL_WNDPROC);
        SetWindowLongW(hWnd, GWL_WNDPROC, (LONG)WindowProc);
        return;
    }
}

uintptr_t WinMain_AfterWindowInit_ptr = 0x0;
void WinMain_AfterWindowInit()
{
    InitWndProc();
    plugin::CallDyn(WinMain_AfterWindowInit_ptr);
}

void CCore::Init()
{
    CCrashLog::ms_lpPreviousFilter = SetUnhandledExceptionFilter(CCrashLog::ExceptionHandler);
    CCrashLog::InstallAbortHandlers();

    // test mode / own user dir / windowed: must be in place before WinMain runs (see CTestMode.h)
    CTestMode::EarlyInit();

    LPTSTR cmd = GetCommandLine();
    if (strstr(cmd, "--coopd0") != nullptr)
    {
        ms_bDebug = true;
        ms_nRunIndex = 0;
    }
    if (strstr(cmd, "--coopd1") != nullptr)
    {
        ms_bDebug = true;
        ms_nRunIndex = 1;
    }

    gvm.Detect();
#ifdef _DEV
    CCore::AllocateConsole();
#endif
    CImGui::Init();
    CCheatMenu::Init();
    CMissionEditor::Init();
    CMissionRulesData::Init();
    CModeMenu::Init();
    CHudHideAll::Init();
    CSaveMenu::Init();
    CMissionMode::Init();
    CVsRace::Init();
    CVsLobby::Init();
    CGangSync::Init();
    CLaunchManager::CollectCommandLineArgs();
    WinMain_AfterWindowInit_ptr = injector::GetBranchDestination(0x748995).as_int();
    patch::RedirectCall(0x748995, WinMain_AfterWindowInit);
    CCustomMenuManager::Init();
    CPatch::ApplyPatches();
    CHook::Init();
    CCustomCommandRegistrar::Register();
    CDXFont::Init();
    CDiscordRPC::Init();
    COpCodeSync::Init();
    CMissionMirror::Init();
    CNetworkPadSync::Init();
    Events::initGameEvent.after += []
    {
        CPatch::TemporaryPatches();
        // init CNetworking async
        CNetwork::Start();
    };
    Events::initRwEvent += [] { CPatch::PatchFramerate(); };

    /*Events::initScriptsEvent.after += []
    {
        CPatch::TemporaryPatches();
    };*/
    gameShutdownEvent.before += []
    {
        // disconnect from server
        CDiscordRPC::Destroy();
        CNetwork::Disconnect();
    };
    semver_parse(COOPSA_VERSION, &CCore::Version);
}

void CCore::AllocateConsole()
{
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONIN$", "r", stdin);

    DWORD dwMode = 0;
    HANDLE hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleMode(hOutput, &dwMode);
    dwMode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOutput, dwMode);

    HWND hWnd = GetConsoleWindow();
    ShowWindow(hWnd, SW_SHOWMINIMIZED);
}
