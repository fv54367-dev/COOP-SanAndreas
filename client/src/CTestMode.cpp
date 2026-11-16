#include "stdafx.h"
#include "CTestMode.h"
#include <shellapi.h>
#include <cfloat>

#define TEST_MSG_PREFIX "{c7a6ff}[Prueba]{d6dce8} "

// gta_sa.exe 1.0 US addresses (checked by disassembling gta_sa.exe, see INTERRUPTORES.md)
static constexpr uintptr_t ADDR_GET_USER_FILES_FOLDER = 0x744FB0;  // char* psGetUserFilesFolder(): Shell Folders\Personal + "\GTA San Andreas User Files"
static constexpr uintptr_t ADDR_CALL_WINMAIN = 0x8246EC;           // WinMainCRTStartup -> WinMain (0x748710)
static constexpr uintptr_t ADDR_USER_DIR = 0xC92368;               // char[256], returned by the function above
static constexpr uintptr_t ADDR_GALLERY_DIR = 0xC92268;            // char[256], <user dir>\Gallery
static constexpr uintptr_t ADDR_USER_TRACKS_DIR = 0xC92168;        // char[256], <user dir>\User Tracks
static constexpr uintptr_t ADDR_CALL_UPDATEPADS_GAME = 0x53BEE6;   // CGame::Process -> CPad::UpdatePads
static constexpr uintptr_t ADDR_CALL_UPDATEPADS_FRONTEND = 0x53E78B;  // FrontendIdle -> CPad::UpdatePads
static constexpr uintptr_t ADDR_UPDATEPADS = 0x541DD0;
static constexpr uintptr_t ADDR_TEMP_KEY_STATE = 0xB72CB0;  // CPad::TempKeyState (written by the window proc)
static constexpr uintptr_t ADDR_NEW_KEY_STATE = 0xB73190;   // CPad::NewKeyState
static constexpr size_t KEY_STATE_SIZE = 0x270;             // UpdatePads copies it with rep movsd, ecx = 0x9C
static constexpr uintptr_t ADDR_PC_TEMP_MOUSE_STATE = 0xB73404;
static constexpr uintptr_t ADDR_NEW_MOUSE_STATE = 0xB73418;
static constexpr size_t MOUSE_STATE_SIZE = 0x14;
static constexpr uintptr_t ADDR_WM_ACTIVATE_FOREGROUNDAPP_IMM = 0x747FB6;  // ForegroundApp = 0x8D621C
static constexpr uintptr_t ADDR_D3D9_DELAYLOAD_IAT = 0x8E3ED4;  // delay-load IAT slot of d3d9!Direct3DCreate9 (stub 0x807C2B)

static char ms_szUserDir[MAX_PATH] = "";
static char ms_szWindowPos[32] = "center";

static std::string GetGameDir()
{
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char* lastSlash = strrchr(path, '\\');
    if (lastSlash)
        *lastSlash = '\0';
    return path;
}

static std::string WideToAnsi(const wchar_t* text)
{
    char buffer[MAX_PATH * 2] = "";
    WideCharToMultiByte(CP_ACP, 0, text, -1, buffer, sizeof(buffer), nullptr, nullptr);
    return buffer;
}

bool CTestMode::IsGameWindowFocused()
{
    return RsGlobal.ps && GetForegroundWindow() == RsGlobal.ps->window;
}

// ---------------------------------------------------------------------------------------------------------------
// own user files dir

static char* __cdecl GetUserFilesFolder_Hook()
{
    char* userDir = (char*)ADDR_USER_DIR;
    strcpy_s(userDir, 256, ms_szUserDir);
    CreateDirectoryA(userDir, nullptr);

    sprintf_s((char*)ADDR_GALLERY_DIR, 256, "%s\\Gallery", userDir);
    CreateDirectoryA((char*)ADDR_GALLERY_DIR, nullptr);
    sprintf_s((char*)ADDR_USER_TRACKS_DIR, 256, "%s\\User Tracks", userDir);
    CreateDirectoryA((char*)ADDR_USER_TRACKS_DIR, nullptr);
    return userDir;
}

// SilentPatch puts its own jump on psGetUserFilesFolder from its DllMain, after ours: put ours back when WinMain starts,
// once every plugin is loaded and before the game asks for the folder
typedef int(__stdcall* WinMain_t)(HINSTANCE, HINSTANCE, LPSTR, int);
static WinMain_t ms_pOrigWinMain = nullptr;

static int __stdcall WinMain_Hook(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR cmdLine, int cmdShow)
{
    patch::RedirectJump(ADDR_GET_USER_FILES_FOLDER, GetUserFilesFolder_Hook);
    return ms_pOrigWinMain(hInstance, hPrevInstance, cmdLine, cmdShow);
}

// ---------------------------------------------------------------------------------------------------------------
// input only in the focused window: after the pads are read, the unfocused copy forgets keyboard, mouse and pad

static void ClearLocalInput()
{
    memset((void*)ADDR_TEMP_KEY_STATE, 0, KEY_STATE_SIZE);
    memset((void*)ADDR_NEW_KEY_STATE, 0, KEY_STATE_SIZE);
    memset((void*)ADDR_PC_TEMP_MOUSE_STATE, 0, MOUSE_STATE_SIZE);
    memset((void*)ADDR_NEW_MOUSE_STATE, 0, MOUSE_STATE_SIZE);
    memset(&CPad::GetPad(0)->NewState, 0, sizeof(CControllerState));
}

static void UpdatePads_Hook()
{
    plugin::Call<ADDR_UPDATEPADS>();
    if (!CTestMode::IsGameWindowFocused())
        ClearLocalInput();
}

// ---------------------------------------------------------------------------------------------------------------
// windowed mode: Direct3DCreate9 -> IDirect3D9::CreateDevice / IDirect3DDevice9::Reset with Windowed = TRUE

typedef IDirect3D9*(WINAPI* Direct3DCreate9_t)(UINT);
typedef HRESULT(STDMETHODCALLTYPE* CreateDevice_t)(
    IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
typedef HRESULT(STDMETHODCALLTYPE* Reset_t)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

static Direct3DCreate9_t ms_pOrigDirect3DCreate9 = nullptr;
static CreateDevice_t ms_pOrigCreateDevice = nullptr;
static Reset_t ms_pOrigReset = nullptr;
static HWND ms_hGameWindow = nullptr;
static UINT ms_nWindowWidth = 0;
static UINT ms_nWindowHeight = 0;

static void PatchVTableEntry(void* object, int index, void* hook, void** original)
{
    void** vtable = *(void***)object;
    if (vtable[index] == hook)
        return;

    DWORD oldProtect;
    VirtualProtect(&vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
    *original = vtable[index];
    vtable[index] = hook;
    VirtualProtect(&vtable[index], sizeof(void*), oldProtect, &oldProtect);
}

static void MakeWindowedParams(D3DPRESENT_PARAMETERS& params)
{
    params.Windowed = TRUE;
    params.FullScreen_RefreshRateInHz = 0;
    if (params.PresentationInterval != D3DPRESENT_INTERVAL_IMMEDIATE &&
        params.PresentationInterval != D3DPRESENT_INTERVAL_DEFAULT)
    {
        params.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    }
}

static void ApplyWindowStyle()
{
    HWND hWnd = ms_hGameWindow;
    if (!hWnd || !ms_nWindowWidth || !ms_nWindowHeight)
        return;

    const LONG style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
    const LONG exStyle = GetWindowLongA(hWnd, GWL_EXSTYLE) & ~WS_EX_TOPMOST;
    SetWindowLongA(hWnd, GWL_STYLE, style);
    SetWindowLongA(hWnd, GWL_EXSTYLE, exStyle);

    RECT rect = {0, 0, (LONG)ms_nWindowWidth, (LONG)ms_nWindowHeight};
    AdjustWindowRectEx(&rect, style, FALSE, exStyle);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    RECT workArea{};
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &workArea, 0);
    int x = workArea.left + ((workArea.right - workArea.left) - width) / 2;
    int y = workArea.top + ((workArea.bottom - workArea.top) - height) / 2;
    int customX = 0, customY = 0;
    if (_stricmp(ms_szWindowPos, "left") == 0)
    {
        x = workArea.left;
        y = workArea.top;
    }
    else if (_stricmp(ms_szWindowPos, "right") == 0)
    {
        x = workArea.right - width;
        y = workArea.top;
    }
    else if (sscanf_s(ms_szWindowPos, "%d,%d", &customX, &customY) == 2)
    {
        x = customX;
        y = customY;
    }
    if (x < workArea.left)
        x = workArea.left;
    if (y < workArea.top)
        y = workArea.top;

    SetWindowPos(hWnd, HWND_NOTOPMOST, x, y, width, height, SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

static HRESULT STDMETHODCALLTYPE Reset_Hook(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params)
{
    if (!params)
        return ms_pOrigReset(device, params);

    D3DPRESENT_PARAMETERS windowed = *params;
    MakeWindowedParams(windowed);
    HRESULT result = ms_pOrigReset(device, &windowed);
    if (FAILED(result) && windowed.BackBufferFormat != D3DFMT_UNKNOWN)
    {
        windowed.BackBufferFormat = D3DFMT_UNKNOWN;
        result = ms_pOrigReset(device, &windowed);
    }
    if (SUCCEEDED(result))
    {
        ms_nWindowWidth = windowed.BackBufferWidth;
        ms_nWindowHeight = windowed.BackBufferHeight;
        ApplyWindowStyle();
    }
    return result;
}

static HRESULT STDMETHODCALLTYPE CreateDevice_Hook(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE deviceType,
    HWND focusWindow, DWORD behaviorFlags, D3DPRESENT_PARAMETERS* params, IDirect3DDevice9** device)
{
    if (!params)
        return ms_pOrigCreateDevice(d3d, adapter, deviceType, focusWindow, behaviorFlags, params, device);

    D3DPRESENT_PARAMETERS windowed = *params;
    MakeWindowedParams(windowed);
    HRESULT result = ms_pOrigCreateDevice(d3d, adapter, deviceType, focusWindow, behaviorFlags, &windowed, device);
    if (FAILED(result) && windowed.BackBufferFormat != D3DFMT_UNKNOWN)
    {
        windowed.BackBufferFormat = D3DFMT_UNKNOWN;
        result = ms_pOrigCreateDevice(d3d, adapter, deviceType, focusWindow, behaviorFlags, &windowed, device);
    }
    if (FAILED(result))
    {
        // windowed didn't work on this machine: go on as the game asked (fullscreen)
        return ms_pOrigCreateDevice(d3d, adapter, deviceType, focusWindow, behaviorFlags, params, device);
    }

    ms_hGameWindow = windowed.hDeviceWindow ? windowed.hDeviceWindow : focusWindow;
    ms_nWindowWidth = windowed.BackBufferWidth;
    ms_nWindowHeight = windowed.BackBufferHeight;
    if (device && *device)
        PatchVTableEntry(*device, 16, (void*)Reset_Hook, (void**)&ms_pOrigReset);  // IDirect3DDevice9::Reset
    ApplyWindowStyle();
    return result;
}

static IDirect3D9* WINAPI Direct3DCreate9_Hook(UINT sdkVersion)
{
    if (!ms_pOrigDirect3DCreate9)
    {
        HMODULE d3d9 = LoadLibraryA("d3d9.dll");
        if (d3d9)
            ms_pOrigDirect3DCreate9 = (Direct3DCreate9_t)GetProcAddress(d3d9, "Direct3DCreate9");
        if (!ms_pOrigDirect3DCreate9)
            return nullptr;
    }

    IDirect3D9* d3d = ms_pOrigDirect3DCreate9(sdkVersion);
    if (d3d)
        PatchVTableEntry(d3d, 16, (void*)CreateDevice_Hook, (void**)&ms_pOrigCreateDevice);  // IDirect3D9::CreateDevice
    return d3d;
}

// ---------------------------------------------------------------------------------------------------------------

// psSelectDevice (0x746190) stores the chosen video mode in GcurSelVM (0x8D6220) at 0x7462C5. Video mode 0 of the
// RenderWare D3D9 driver is the windowed one: forcing it makes the engine create a windowed device by itself (the
// engine and the device agree, unlike when the D3D9 present parameters are edited behind its back).
static constexpr uintptr_t ADDR_PSSELECTDEVICE_USE_VM = 0x7462C5;  // mov edx, [GcurSelVM] (6 bytes), next instruction 0x7462CB

static void __declspec(naked) ForceWindowedVideoMode_Stub()
{
    __asm
    {
        mov dword ptr ds:[0x8D6220], 0  // GcurSelVM (literal: inline asm would read a constant name as a variable)
        xor edx, edx
        mov eax, 0x7462CB
        jmp eax
    }
}

void CTestMode::EarlyInit()
{
    std::string gameDir = GetGameDir();
    std::string localIni = gameDir + "\\CoopAndreas\\coopandreas.ini";

    auto readBool = [&](const char* key, bool defaultValue)
    {
        char value[8] = "";
        GetPrivateProfileStringA("config", key, defaultValue ? "true" : "false", value, sizeof(value), localIni.c_str());
        return _stricmp(value, "true") == 0 || strcmp(value, "1") == 0;
    };

    ms_bEnabled = readBool("test-mode", false);
    char userDir[MAX_PATH] = "";
    GetPrivateProfileStringA("config", "user-dir", ms_bEnabled ? "UserFiles" : "", userDir, sizeof(userDir), localIni.c_str());
    GetPrivateProfileStringA("config", "window-pos", "center", ms_szWindowPos, sizeof(ms_szWindowPos), localIni.c_str());

    bool windowedFromFile = readBool("windowed", ms_bEnabled);
    bool windowedFromArgs = false;

    // the W command line: CLaunchManager tokenizes the A one in place
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv)
    {
        for (int i = 1; i < argc; ++i)
        {
            if (_wcsicmp(argv[i], L"-testmode") == 0)
            {
                if (!ms_bEnabled && !userDir[0])
                    strcpy_s(userDir, "UserFiles");
                ms_bEnabled = true;
                windowedFromArgs = true;
            }
            else if (_wcsicmp(argv[i], L"-windowed") == 0)
                windowedFromArgs = true;
            else if (_wcsicmp(argv[i], L"-userdir") == 0 && i + 1 < argc)
                strcpy_s(userDir, WideToAnsi(argv[++i]).c_str());
            else if (_wcsicmp(argv[i], L"-winpos") == 0 && i + 1 < argc)
                strcpy_s(ms_szWindowPos, WideToAnsi(argv[++i]).c_str());
        }
        LocalFree(argv);
    }
    ms_bWindowed = windowedFromFile || windowedFromArgs;

    if (userDir[0])
    {
        std::string path = userDir;
        bool absolute = path.size() > 1 && (path[1] == ':' || (path[0] == '\\' && path[1] == '\\'));
        if (!absolute)
            path = gameDir + "\\" + path;
        while (!path.empty() && (path.back() == '\\' || path.back() == '/'))
            path.pop_back();

        // the save code builds "<user dir>\GTASAsf<n>.b" in small buffers
        if (path.size() < 200)
        {
            strcpy_s(ms_szUserDir, path.c_str());
            patch::RedirectJump(ADDR_GET_USER_FILES_FOLDER, GetUserFilesFolder_Hook);
            ms_pOrigWinMain = (WinMain_t)injector::GetBranchDestination(ADDR_CALL_WINMAIN).as_int();
            patch::RedirectCall(ADDR_CALL_WINMAIN, WinMain_Hook);
        }
        else
        {
            MessageBoxA(nullptr, "user-dir es demasiado larga (maximo 200 letras). Se usa la carpeta de Documentos.",
                "COOP San Andreas - modo prueba", MB_OK | MB_ICONWARNING);
        }
    }

    if (ms_bEnabled)
    {
        // WndProc WM_ACTIVATE(WA_INACTIVE) does `mov ForegroundApp, 0` (0x747FB0, imm at +6); WM_KILLFOCUS already
        // writes 1 instead of 0 (CPatch, 0x74805A). Same here, so the copy without focus never stops its loop
        patch::SetUInt(ADDR_WM_ACTIVATE_FOREGROUNDAPP_IMM, 1);
        // and never take the "window lost focus" branch of WM_ACTIVATE (0x747FA3: `jne active` -> `jmp active`): it tells the
        // engine the app went inactive, which opens the pause menu and freezes this copy (and its NPCs/vehicles for the other)
        patch::SetUChar(0x747FA3, 0xEB);
        patch::RedirectCall(ADDR_CALL_UPDATEPADS_GAME, UpdatePads_Hook);
        patch::RedirectCall(ADDR_CALL_UPDATEPADS_FRONTEND, UpdatePads_Hook);
    }

    if (ms_bWindowed)
    {
        patch::RedirectJump(ADDR_PSSELECTDEVICE_USE_VM, ForceWindowedVideoMode_Stub);
        Events::initRwEvent += []
        {
            if (RsGlobal.ps)
                ms_hGameWindow = RsGlobal.ps->window;
            ms_nWindowWidth = RsGlobal.maximumWidth;
            ms_nWindowHeight = RsGlobal.maximumHeight;
            ApplyWindowStyle();
        };
    }
}

// ---------------------------------------------------------------------------------------------------------------
// chat commands

static constexpr float WALK_STOP_DISTANCE = 3.0f;
static constexpr float WALK_RUN_DISTANCE = 15.0f;

static bool ms_bWalking = false;  // this copy walks by itself towards ms_walkLeader
static playerid_t ms_walkLeader{};
static CVector ms_walkLastTarget{};
static uint32_t ms_walkLastOrder = 0;
static std::vector<playerid_t> ms_walkersSentByMe;  // players told to walk towards me

static CNetworkPlayer* FindNearestNetworkPlayer()
{
    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (!localPlayer)
        return nullptr;

    CNetworkPlayer* nearest = nullptr;
    float bestDistance = FLT_MAX;
    for (auto player : CNetworkPlayerManager::m_pPlayers)
    {
        if (!player->m_pPed)
            continue;
        float distance = DistanceBetweenPoints(localPlayer->GetPosition(), player->m_pPed->GetPosition());
        if (distance < bestDistance)
        {
            bestDistance = distance;
            nearest = player;
        }
    }
    return nearest;
}

// z = feet height (as scripts do); findGround: -100 makes SET_CHAR_COORDINATES look for the ground
static void TeleportLocalPlayer(const CVector& pos, float heading, uint8_t interior, bool findGround)
{
    CPlayerPed* ped = FindPlayerPed(0);
    if (!ped)
        return;

    int pedRef = CPools::GetPedRef(ped);
    if (CGame::currArea != interior)
        plugin::Command<Commands::SET_AREA_VISIBLE>((int)interior);
    plugin::Command<Commands::SET_CHAR_AREA_VISIBLE>(pedRef, (int)interior);
    plugin::Command<Commands::LOAD_SCENE>(pos.x, pos.y, pos.z);
    plugin::Command<Commands::SET_CHAR_COORDINATES>(pedRef, pos.x, pos.y, findGround ? -100.0f : pos.z);
    if (!ped->m_nPedFlags.bInVehicle)
        plugin::Command<Commands::SET_CHAR_HEADING>(pedRef, heading * 180.0f / 3.14159265f);
}

static void StopWalking(bool clearTask)
{
    if (!ms_bWalking)
        return;
    ms_bWalking = false;
    CPlayerPed* ped = FindPlayerPed(0);
    if (clearTask && ped && !ped->m_nPedFlags.bInVehicle)
        plugin::Command<Commands::CLEAR_CHAR_TASKS>(CPools::GetPedRef(ped));
}

static void CommandBring()
{
    CPlayerPed* localPlayer = FindPlayerPed(0);
    CNetworkPlayer* target = FindNearestNetworkPlayer();
    if (!localPlayer || !target)
    {
        CChat::AddMessage(TEST_MSG_PREFIX "No hay otro jugador conectado.");
        return;
    }

    // 2 m in front of me, looking at me
    float heading = localPlayer->m_fCurrentRotation;
    CVector forward(-sinf(heading), cosf(heading), 0.0f);
    Packets::Players::TestCommand packet{};
    packet.targetPlayerid = target->m_playerId;
    packet.command = Packets::Players::TestCommand::TELEPORT;
    const CVector& myPos = localPlayer->GetPosition();
    packet.pos.x = myPos.x + forward.x * 2.0f;
    packet.pos.y = myPos.y + forward.y * 2.0f;
    packet.pos.z = myPos.z - 1.0f;  // center of the ped -> feet
    packet.heading.m_angle = heading + 3.14159265f;
    packet.interior = (uint8_t)localPlayer->m_nAreaCode;
    GetPacketFactory().Send(packet);

    CChat::AddMessage(TEST_MSG_PREFIX "Trayendo a %s.", target->GetName().c_str());
}

static void CommandGoTo(const std::wstring& args)
{
    CNetworkPlayer* target = nullptr;
    auto first = args.find_first_not_of(L" \t");
    if (first == std::wstring::npos)
        target = FindNearestNetworkPlayer();
    else
    {
        auto last = args.find_last_not_of(L" \t");
        auto trimmed = args.substr(first, last - first + 1);
        char query[128]{};
        if (!WideCharToMultiByte(CP_ACP, 0, trimmed.c_str(), -1, query, sizeof(query), nullptr, nullptr))
        {
            CChat::AddMessage(TEST_MSG_PREFIX "Nombre demasiado largo o invalido.");
            return;
        }
        std::vector<std::pair<std::string, CNetworkPlayer*>> matches;
        matches.emplace_back(CLocalPlayer::m_Name, nullptr);
        for (auto player : CNetworkPlayerManager::m_pPlayers)
            matches.emplace_back(player->GetName(), player);
        bool exact = false;
        for (const auto& match : matches)
            if (!_stricmp(query, match.first.c_str())) exact = true;
        matches.erase(std::remove_if(matches.begin(), matches.end(), [&](const auto& match) {
            return exact ? _stricmp(query, match.first.c_str()) != 0 :
                _strnicmp(query, match.first.c_str(), strlen(query)) != 0;
        }), matches.end());
        if (matches.empty())
        {
            CChat::AddMessage(TEST_MSG_PREFIX "Jugador desconocido: %s.", query);
            return;
        }
        if (matches.size() > 1)
        {
            CChat::AddMessage(TEST_MSG_PREFIX "Nombre ambiguo: %s. Candidatos:", query);
            for (const auto& match : matches)
                CChat::AddMessage(TEST_MSG_PREFIX "%s", match.first.c_str());
            return;
        }
        target = matches.front().second;
        if (!target)
        {
            CChat::AddMessage(TEST_MSG_PREFIX "Ese sos vos: elegi otro jugador.");
            return;
        }
        if (!target->m_pPed)
        {
            CChat::AddMessage(TEST_MSG_PREFIX "Ese jugador todavia no tiene un personaje disponible.");
            return;
        }
    }
    if (!target)
    {
        CChat::AddMessage(TEST_MSG_PREFIX "No hay otro jugador conectado.");
        return;
    }

    CVector pos = target->m_pPed->GetPosition();
    float heading = target->m_pPed->m_fCurrentRotation;
    pos.x += -sinf(heading) * 2.0f;
    pos.y += cosf(heading) * 2.0f;
    pos.z -= 1.0f;
    TeleportLocalPlayer(pos, heading + 3.14159265f, (uint8_t)target->m_pPed->m_nAreaCode, false);
    CChat::AddMessage(TEST_MSG_PREFIX "Fuiste hasta %s (area %u).", target->GetName().c_str(), (unsigned)target->m_pPed->m_nAreaCode);
}

static void CommandTeleport(const std::wstring& args)
{
    float x = 0.0f, y = 0.0f, z = -100.0f;
    int count = swscanf_s(args.c_str(), L"%f %f %f", &x, &y, &z);
    if (count < 2)
    {
        if (args.find_first_not_of(L" \t") != std::wstring::npos)
            CommandGoTo(args);
        else
            CChat::AddMessage(TEST_MSG_PREFIX "Uso: /tp <jugador> o /tp x y [z] (sin z busca el suelo).");
        return;
    }

    CPlayerPed* ped = FindPlayerPed(0);
    TeleportLocalPlayer(CVector(x, y, z), ped ? ped->m_fCurrentRotation : 0.0f, 0, count < 3);
    CChat::AddMessage(TEST_MSG_PREFIX "Teletransportado a %.1f %.1f %s.", x, y, count < 3 ? "(suelo)" : "");
}

static void CommandWalk()
{
    CNetworkPlayer* target = FindNearestNetworkPlayer();
    if (!target)
    {
        CChat::AddMessage(TEST_MSG_PREFIX "No hay otro jugador conectado.");
        return;
    }

    auto it = std::find(ms_walkersSentByMe.begin(), ms_walkersSentByMe.end(), target->m_playerId);
    bool stop = it != ms_walkersSentByMe.end();
    if (stop)
        ms_walkersSentByMe.erase(it);
    else
        ms_walkersSentByMe.push_back(target->m_playerId);

    Packets::Players::TestCommand packet{};
    packet.targetPlayerid = target->m_playerId;
    packet.command = stop ? Packets::Players::TestCommand::WALK_STOP : Packets::Players::TestCommand::WALK_START;
    GetPacketFactory().Send(packet);

    if (stop)
        CChat::AddMessage(TEST_MSG_PREFIX "%s deja de caminar hacia vos.", target->GetName().c_str());
    else
        CChat::AddMessage(TEST_MSG_PREFIX "%s camina hacia vos hasta 3 m (/caminar de nuevo lo corta).",
            target->GetName().c_str());
}

const char* CTestMode::GetHelpText()
{
    return TEST_MSG_PREFIX "/traer (trae al otro jugador), /ir [jugador], /tp <jugador> (sin preguntar, nombre o prefijo unico), /tp x y [z], /caminar (el otro camina hasta vos; otra vez lo corta)";
}

bool CTestMode::HandleChatCommand(const std::wstring& name, const std::wstring& args)
{
    if (name != L"traer" && name != L"ir" && name != L"tp" && name != L"caminar")
        return false;

    if (!ms_bEnabled)
    {
        CChat::AddMessage(TEST_MSG_PREFIX "Ese comando es solo del modo prueba (test-mode=true en CoopAndreas\\coopandreas.ini).");
        return true;
    }

    if (name == L"traer")
        CommandBring();
    else if (name == L"ir")
        CommandGoTo(args);
    else if (name == L"tp")
        CommandTeleport(args);
    else
        CommandWalk();
    return true;
}

void CTestMode::HandleTestCommand(Packets::Players::TestCommand* packet)
{
    if (!ms_bEnabled)
        return;

    CNetworkPlayer* sender = CNetworkPlayerManager::GetPlayer(packet->senderPlayerid);
    std::string senderName = sender ? sender->GetName() : "otro jugador";

    switch (packet->command)
    {
        case Packets::Players::TestCommand::TELEPORT:
            StopWalking(false);
            TeleportLocalPlayer(packet->pos, packet->heading.m_angle, packet->interior, false);
            CChat::AddMessage(TEST_MSG_PREFIX "%s te trajo.", senderName.c_str());
            break;
        case Packets::Players::TestCommand::WALK_START:
            if (!sender)
                return;
            ms_bWalking = true;
            ms_walkLeader = sender->m_playerId;
            ms_walkLastOrder = 0;
            CChat::AddMessage(TEST_MSG_PREFIX "Caminas solo hacia %s hasta 3 m.", senderName.c_str());
            break;
        case Packets::Players::TestCommand::WALK_STOP:
            StopWalking(true);
            CChat::AddMessage(TEST_MSG_PREFIX "Dejas de caminar hacia %s.", senderName.c_str());
            break;
        default:
            break;
    }
}

void CTestMode::Process()
{
    if (!ms_bEnabled)
        return;

    // walkers I sent that are gone
    ms_walkersSentByMe.erase(std::remove_if(ms_walkersSentByMe.begin(), ms_walkersSentByMe.end(),
                                 [](const playerid_t& id) { return CNetworkPlayerManager::GetPlayer(id) == nullptr; }),
        ms_walkersSentByMe.end());

    if (!ms_bWalking)
        return;

    CPlayerPed* ped = FindPlayerPed(0);
    CNetworkPlayer* leader = CNetworkPlayerManager::GetPlayer(ms_walkLeader);
    if (!ped || !leader || !leader->m_pPed)
    {
        StopWalking(true);
        CChat::AddMessage(TEST_MSG_PREFIX "El jugador se fue: dejas de caminar.");
        return;
    }
    if (ped->m_nPedFlags.bInVehicle || ped->m_fHealth <= 0.0f)
        return;

    uint32_t now = GetTickCount();
    CVector leaderPos = leader->m_pPed->GetPosition();
    CVector toLeader = leaderPos - ped->GetPosition();
    float distance = toLeader.Magnitude();
    if (distance <= WALK_STOP_DISTANCE + 0.5f)
        return;

    // new order when the leader moved or every few seconds (the task may have been replaced)
    bool leaderMoved = DistanceBetweenPoints(leaderPos, ms_walkLastTarget) > 1.0f;
    if (!leaderMoved && ms_walkLastOrder != 0 && now - ms_walkLastOrder < 4000)
        return;
    if (ms_walkLastOrder != 0 && now - ms_walkLastOrder < 300)
        return;

    ms_walkLastTarget = leaderPos;
    ms_walkLastOrder = now;

    // stop 2.5 m short of the leader
    toLeader.Normalise();
    CVector destination = leaderPos - toLeader * (WALK_STOP_DISTANCE - 0.5f);
    int moveState = 6;  // always run (was: walk when closer than WALK_RUN_DISTANCE): tests go faster
    plugin::Command<Commands::TASK_GO_STRAIGHT_TO_COORD>(
        CPools::GetPedRef(ped), destination.x, destination.y, destination.z, moveState, 20000);
}
