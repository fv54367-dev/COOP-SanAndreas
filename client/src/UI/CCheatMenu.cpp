#include "stdafx.h"
#include "CCheatMenu.h"
#include "imgui.h"
#include "Debug/CImGui.h"
#include "CConfigLoader.h"
#include <CCheat.h>
#include <string>

// Every slot of CCheat::m_aCheatFunctions / m_aCheatsActive (92, GTA SA 1.0 US; indices checked against gta-reversed
// eCheats and CCheatSync.cpp). Slot 59 is CHEAT_UNUSED (no code in the game) and is not listed. Same as typing the code:
// the function of the slot runs, or, when the slot has none, the game only reads the flag, so the flag is flipped.
// Synced slots (CCheatSync) propagate by themselves when the slot changes.
enum eCategory : uint8_t { CAT_PLAYER, CAT_WEAPONS, CAT_WANTED, CAT_VEHICLES, CAT_TRAFFIC, CAT_PEDS, CAT_WEATHER, CAT_GAME, CAT_COUNT };
static const char* const ms_szCategories[CAT_COUNT] = {
    "Personaje", "Armas y habilidades", "Busqueda", "Vehiculos y manejo", "Trafico", "Peatones y pandillas", "Clima y hora", "Juego y modos"};

struct CheatEntry
{
    uint8_t index;
    uint8_t category;
    uint8_t flags;  // 1 = weather of the host (needs host in a session), 2 = asks for confirmation, 4 = in the short list
    const char* name;
};

static const CheatEntry ms_aCheats[] = {
    {3, CAT_PLAYER, 4, "Vida + chaleco + $250.000 (HESOYAM)"},
    {61, CAT_PLAYER, 0, "Salud infinita (on/off)"},
    {62, CAT_PLAYER, 0, "Oxigeno infinito (on/off)"},
    {64, CAT_PLAYER, 4, "Jetpack"},
    {63, CAT_PLAYER, 4, "Paracaidas"},
    {36, CAT_PLAYER, 4, "Gordo"},
    {37, CAT_PLAYER, 4, "Musculo maximo"},
    {38, CAT_PLAYER, 4, "Flaco"},
    {81, CAT_PLAYER, 4, "Stamina maxima"},
    {79, CAT_PLAYER, 0, "Respeto maximo"},
    {80, CAT_PLAYER, 0, "Atractivo sexual maximo"},
    {60, CAT_PLAYER, 0, "Mega salto (on/off)"},
    {67, CAT_PLAYER, 0, "Mega punetazo (on/off)"},
    {68, CAT_PLAYER, 0, "Nunca hambre (on/off)"},
    {29, CAT_PLAYER, 2, "Suicidio"},
    {0, CAT_WEAPONS, 4, "Armas 1 (pandilla)"},
    {1, CAT_WEAPONS, 4, "Armas 2 (profesional)"},
    {2, CAT_WEAPONS, 4, "Armas 3 (pesadas)"},
    {82, CAT_WEAPONS, 4, "Habilidad de armas (nivel hitman)"},
    {83, CAT_WEAPONS, 4, "Habilidad de manejo maxima"},
    {72, CAT_WEAPONS, 0, "Municion infinita (on/off)"},
    {71, CAT_WEAPONS, 0, "Modo adrenalina (on/off)"},
    {73, CAT_WEAPONS, 0, "Apuntar al manejar (on/off)"},
    {4, CAT_WANTED, 0, "Subir busqueda 2 estrellas"},
    {5, CAT_WANTED, 4, "Quitar busqueda"},
    {65, CAT_WANTED, 4, "Nunca buscado (on/off)"},
    {66, CAT_WANTED, 0, "Seis estrellas (on/off)"},
    {17, CAT_VEHICLES, 4, "Tanque Rhino"},
    {84, CAT_VEHICLES, 4, "Helicoptero Hunter"},
    {89, CAT_VEHICLES, 4, "Monster truck"},
    {85, CAT_VEHICLES, 4, "Cuatriciclo"},
    {88, CAT_VEHICLES, 4, "Avion de acrobacias"},
    {20, CAT_VEHICLES, 4, "Hotring Racer A"},
    {21, CAT_VEHICLES, 0, "Hotring Racer B"},
    {87, CAT_VEHICLES, 4, "Topadora"},
    {51, CAT_VEHICLES, 4, "Vortex (aerodeslizador)"},
    {25, CAT_VEHICLES, 4, "Carrito de golf"},
    {22, CAT_VEHICLES, 4, "Coche funebre (Romero)"},
    {24, CAT_VEHICLES, 4, "Camion de basura"},
    {18, CAT_VEHICLES, 0, "Bloodring Banger"},
    {19, CAT_VEHICLES, 0, "Rancher"},
    {23, CAT_VEHICLES, 0, "Limusina (Stretch)"},
    {50, CAT_VEHICLES, 0, "Hydra"},
    {86, CAT_VEHICLES, 0, "Camion cisterna (Tanker)"},
    {27, CAT_VEHICLES, 0, "Auto invisible, solo ruedas (on/off)"},
    {28, CAT_VEHICLES, 0, "Manejo perfecto (on/off)"},
    {34, CAT_VEHICLES, 0, "Autos sobre el agua (on/off)"},
    {35, CAT_VEHICLES, 0, "Barcos voladores (on/off)"},
    {48, CAT_VEHICLES, 0, "Autos voladores (on/off)"},
    {49, CAT_VEHICLES, 0, "Salto enorme de bunny hop (on/off)"},
    {52, CAT_VEHICLES, 0, "Smash n Boom (on/off)"},
    {53, CAT_VEHICLES, 0, "Todos los autos con nitro (on/off)"},
    {54, CAT_VEHICLES, 0, "Autos flotan al ser golpeados (on/off)"},
    {91, CAT_VEHICLES, 0, "Taxis con nitro (on/off)"},
    {26, CAT_VEHICLES, 2, "Explotar todos los autos"},
    {30, CAT_TRAFFIC, 0, "Semaforos siempre verdes (on/off)"},
    {31, CAT_TRAFFIC, 0, "Conductores agresivos (on/off)"},
    {32, CAT_TRAFFIC, 0, "Trafico rosa (on/off)"},
    {33, CAT_TRAFFIC, 0, "Trafico negro (on/off)"},
    {46, CAT_TRAFFIC, 0, "Trafico barato (on/off)"},
    {47, CAT_TRAFFIC, 0, "Trafico veloz y deportivo (on/off)"},
    {74, CAT_TRAFFIC, 0, "Menos trafico (on/off)"},
    {75, CAT_TRAFFIC, 0, "Trafico y peatones de campo (on/off)"},
    {14, CAT_PEDS, 0, "Peatones se pelean con palos de golf"},
    {15, CAT_PEDS, 0, "Todos te atacan"},
    {16, CAT_PEDS, 0, "Todos armados (on/off)"},
    {39, CAT_PEDS, 0, "Elvis en todas partes"},
    {40, CAT_PEDS, 0, "Peatones atacan con cohetes"},
    {41, CAT_PEDS, 0, "Fiesta en la playa"},
    {42, CAT_PEDS, 0, "Pandilleros en todas partes"},
    {43, CAT_PEDS, 0, "Pandillas controlan las calles"},
    {44, CAT_PEDS, 0, "Tema ninja"},
    {45, CAT_PEDS, 0, "El amor todo lo vence (iman de chicas)"},
    {69, CAT_PEDS, 0, "Modo disturbios (on/off)"},
    {70, CAT_PEDS, 0, "Tema casa loca"},
    {76, CAT_PEDS, 0, "Reclutar con 9mm (on/off)"},
    {78, CAT_PEDS, 0, "Reclutar con cohetes (on/off)"},
    {77, CAT_PEDS, 0, "Peatones de campo (on/off)"},
    {90, CAT_PEDS, 0, "Las prostitutas te pagan (on/off)"},
    {6, CAT_WEATHER, 5, "Soleado"},
    {7, CAT_WEATHER, 5, "Muy soleado"},
    {8, CAT_WEATHER, 5, "Nublado"},
    {9, CAT_WEATHER, 5, "Lluvia"},
    {10, CAT_WEATHER, 5, "Niebla"},
    {57, CAT_WEATHER, 5, "Tormenta"},
    {58, CAT_WEATHER, 5, "Tormenta de arena"},
    {55, CAT_WEATHER, 0, "Siempre medianoche (on/off)"},
    {56, CAT_WEATHER, 0, "Cielo naranja, reloj parado (on/off)"},
    {11, CAT_GAME, 0, "Reloj rapido (on/off)"},
    {12, CAT_GAME, 0, "Juego mas rapido"},
    {13, CAT_GAME, 0, "Juego mas lento"},
};

static int ms_nPendingCheat = -1;
static int ms_nConfirmCheat = -1;
static bool ms_bRepairPending = false;
static char ms_szFilter[48] = "";
static bool ms_bInfiniteHealth = false;
static bool ms_bInfiniteAmmo = false;
static bool ms_bWasKeyDown = false;

static bool IsListed(const CheatEntry& entry)
{
    return CConfigLoader::ms_bCheatMenuAll || (entry.flags & 4);
}

static bool NameHasFilter(const char* name)
{
    if (!ms_szFilter[0])
        return true;
    std::string lowerName = name, lowerFilter = ms_szFilter;
    for (auto& c : lowerName) c = char(tolower((unsigned char)c));
    for (auto& c : lowerFilter) c = char(tolower((unsigned char)c));
    return lowerName.find(lowerFilter) != std::string::npos;
}

const char* CCheatMenu::GetCheatName(int index)
{
    for (const CheatEntry& entry : ms_aCheats)
    {
        if (entry.index == index)
            return entry.name;
    }
    return nullptr;
}

static void ApplyCheatSlot(int index)
{
    if (index < 0 || index >= 92)
        return;
    CPlayerPed* player = FindPlayerPed(0);
    if (!player)
    {
        CChat::AddMessage("{c7a6ff}[Trucos]{d6dce8} No se pudo aplicar: el jugador no esta disponible.");
        return;
    }

    if (auto pfnCheat = CCheat::m_aCheatFunctions[index])
        pfnCheat();
    else
        CCheat::m_aCheatsActive[index] = !CCheat::m_aCheatsActive[index];
    CCheat::m_bHasPlayerCheated = true;

    if ((index == 36 || index == 37 || index == 38) && player->m_ePedState == PEDSTATE_DRIVING)
        CChat::AddMessage("{c7a6ff}[Trucos]{d6dce8} Estadisticas aplicadas; baja del vehiculo y repeti para actualizar el aspecto.");
}

static void RepairVehicle()
{
    CPlayerPed* player = FindPlayerPed(0);
    if (player && player->m_nPedFlags.bInVehicle && player->m_pVehicle)
    {
        player->m_pVehicle->Fix();
        player->m_pVehicle->m_fHealth = 1000.0f;
    }
    else
        CChat::AddMessage("{c7a6ff}[Trucos]{d6dce8} Subite a un vehiculo para repararlo.");
}

static void ProcessToggles()
{
    CPlayerPed* player = FindPlayerPed(0);
    if (!player)
        return;

    if (ms_bInfiniteHealth && player->m_fHealth > 0.0f && player->m_fHealth < player->m_fMaxHealth)
        player->m_fHealth = player->m_fMaxHealth;

    if (ms_bInfiniteAmmo)
    {
        CWeapon& weapon = player->m_aWeapons[player->m_nActiveWeaponSlot];
        if (weapon.m_eWeaponType >= WEAPON_GRENADE && weapon.m_eWeaponType <= WEAPON_CAMERA &&
            weapon.m_eWeaponType != WEAPON_DETONATOR && weapon.m_nTotalAmmo < 9000)
            weapon.m_nTotalAmmo = 9999;
    }
}

void CCheatMenu::SetOpen(bool open)
{
    ms_bOpen = open;

    if (open)
        CPad::GetPad(0)->DisablePlayerControls |= 0x400;
    else
    {
        CPad::GetPad(0)->DisablePlayerControls &= ~0x400;
        // CImGui sets 0x200 while the mouse hovers one of its windows and only clears it while it draws: after closing
        // the menu nothing draws any more, so the bit stayed set and the player could not move or turn the camera
        if (!CImGui::ms_bActive)
            CPad::GetPad(0)->DisablePlayerControls &= ~0x200;
    }

    static_cast<IDirect3DDevice9*>(RwD3D9GetCurrentD3DDevice())->ShowCursor(open || CImGui::ms_bActive);
    ImGui::GetIO().MouseDrawCursor = open || CImGui::ms_bActive;
}

void CCheatMenu::Init()
{
    Events::gameProcessEvent.after += []
    {
        bool keyDown = (GetAsyncKeyState(ms_nToggleKey) & 0x8000) != 0;
        bool canToggle = GetForegroundWindow() == RsGlobal.ps->window && !CChat::m_bInputActive &&
                         !FrontEndMenuManager.m_bMenuActive;

        if (keyDown && !ms_bWasKeyDown && canToggle)
            SetOpen(!ms_bOpen);
        ms_bWasKeyDown = keyDown;

        if (ms_bOpen && FrontEndMenuManager.m_bMenuActive)
            SetOpen(false);

        if (ms_nPendingCheat >= 0)
        {
            ApplyCheatSlot(ms_nPendingCheat);
            ms_nPendingCheat = -1;
        }
        if (ms_bRepairPending)
        {
            RepairVehicle();
            ms_bRepairPending = false;
        }

        ProcessToggles();
    };
}


static void CheatButton(const CheatEntry& entry)
{
    const bool active = CCheat::m_aCheatsActive[entry.index];
    ImGui::PushID(entry.index);
    if (active)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.28f, 1.0f));
    if (ImGui::Button(entry.name, ImVec2(-FLT_MIN, 0.0f)))
    {
        if (entry.flags & 2)
            ms_nConfirmCheat = entry.index;
        else
            ms_nPendingCheat = entry.index;
    }
    if (active)
        ImGui::PopStyleColor();
    ImGui::PopID();
}

void CCheatMenu::Draw()
{
    ImGui::SetNextWindowPos(ImVec2(40.0f, 120.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(430.0f, 520.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Trucos", nullptr, ImGuiWindowFlags_NoCollapse);

    ImGui::TextUnformatted("Personaje: solo vos. Clima del host: todos. F8 cierra.");
    ImGui::Checkbox("Vida infinita (continua)", &ms_bInfiniteHealth);
    ImGui::SameLine();
    ImGui::Checkbox("Municion infinita (continua)", &ms_bInfiniteAmmo);
    if (ImGui::Button("Reparar mi vehiculo", ImVec2(-FLT_MIN, 0.0f)))
        ms_bRepairPending = true;
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##cheatfilter", "Buscar truco...", ms_szFilter, sizeof(ms_szFilter));
    ImGui::TextDisabled("Verde = activo. Los (on/off) se apagan volviendo a apretar.");
    ImGui::Separator();

    ImGui::BeginChild("##cheatlist", ImVec2(0.0f, 0.0f), false);
    const bool canWeather = CLocalPlayer::m_bIsHost || !CNetwork::m_bAuthenticated;
    for (int cat = 0; cat < CAT_COUNT; ++cat)
    {
        bool any = false;
        for (const auto& entry : ms_aCheats)
            any = any || (entry.category == cat && IsListed(entry) && NameHasFilter(entry.name));
        if (!any)
            continue;
        if (!ImGui::CollapsingHeader(ms_szCategories[cat], ms_szFilter[0] ? ImGuiTreeNodeFlags_DefaultOpen : 0))
            continue;
        for (const auto& entry : ms_aCheats)
        {
            if (entry.category != cat || !IsListed(entry) || !NameHasFilter(entry.name))
                continue;
            if ((entry.flags & 1) && !canWeather)
            {
                ImGui::TextDisabled("%s (solo el host)", entry.name);
                continue;
            }
            CheatButton(entry);
        }
    }
    ImGui::EndChild();

    if (ms_nConfirmCheat >= 0)
        ImGui::OpenPopup("Confirmar truco");
    if (ImGui::BeginPopupModal("Confirmar truco", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Este truco es destructivo. Aplicarlo?");
        if (ImGui::Button("Aplicar", ImVec2(120.0f, 0.0f)))
        {
            ms_nPendingCheat = ms_nConfirmCheat;
            ms_nConfirmCheat = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(120.0f, 0.0f)))
        {
            ms_nConfirmCheat = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}
