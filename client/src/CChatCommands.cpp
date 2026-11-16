#include "CSocialActions.h"
#include "stdafx.h"
#include "CChatCommands.h"
#include "CTestMode.h"
#include "CMissionMode.h"
#include "CSaveMenu.h"
#include "CVehicleSurf.h"
#include <mutex>

static std::mutex ms_queueMutex;
static std::vector<std::wstring> ms_queuedCommands;

static constexpr float GIVE_AMMO_MAX_DISTANCE = 5.0f;
static constexpr unsigned int MAX_TOTAL_AMMO = 99999;

bool CChatCommands::Queue(const std::wstring& text)
{
    if (text.empty() || text[0] != L'/')
        return false;

    std::lock_guard<std::mutex> lock(ms_queueMutex);
    if (ms_queuedCommands.size() < 8)
        ms_queuedCommands.push_back(text);

    return true;
}

static bool IsAmmoWeapon(eWeaponType weaponType)
{
    // firearms, thrown weapons and sprays; melee, gifts, detonator, goggles and parachute have no ammo to share
    return weaponType >= WEAPON_GRENADE && weaponType <= WEAPON_CAMERA && weaponType != WEAPON_DETONATOR;
}

static void LoadWeaponModel(eWeaponType weaponType)
{
    int model = CWeaponInfo::GetWeaponInfo(weaponType, 1)->m_nModelId1;
    if (model > 0 && CStreaming::ms_aInfoForModel[model].m_nLoadState != LOADSTATE_LOADED)
    {
        CStreaming::RequestModel(model, eStreamingFlags::GAME_REQUIRED);
        CStreaming::LoadAllRequestedModels(false);
    }
}

static void CommandGiveAmmo(const std::wstring& args)
{
    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (!localPlayer)
        return;

    CWeapon& weapon = localPlayer->m_aWeapons[localPlayer->m_nActiveWeaponSlot];
    if (!IsAmmoWeapon(weapon.m_eWeaponType) || weapon.m_nTotalAmmo == 0)
    {
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Pone en la mano el arma cuya municion queres dar.");
        return;
    }

    unsigned int amount = weapon.m_nTotalAmmo / 2;
    if (!args.empty())
    {
        int requested = _wtoi(args.c_str());
        if (requested <= 0)
        {
            CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Uso: /municion [cantidad] (sin cantidad da la mitad)");
            return;
        }
        amount = std::min((unsigned int)requested, weapon.m_nTotalAmmo);
    }

    if (amount == 0)
    {
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} No tenes municion suficiente para dar.");
        return;
    }

    CNetworkPlayer* target = nullptr;
    float bestDistance = GIVE_AMMO_MAX_DISTANCE;
    for (auto player : CNetworkPlayerManager::m_pPlayers)
    {
        if (!player->m_pPed)
            continue;

        float distance = DistanceBetweenPoints(localPlayer->GetPosition(), player->m_pPed->GetPosition());
        if (distance <= bestDistance)
        {
            bestDistance = distance;
            target = player;
        }
    }

    if (!target)
    {
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} No hay ningun jugador cerca (acercate a menos de 5 metros).");
        return;
    }

    Packets::Players::GiveAmmo packet{};
    packet.targetPlayerid = target->m_playerId;
    packet.weaponType = weapon.m_eWeaponType;
    packet.ammo = amount;
    packet.refund = false;
    GetPacketFactory().Send(packet);

    weapon.m_nTotalAmmo -= amount;
    if (weapon.m_nAmmoInClip > weapon.m_nTotalAmmo)
        weapon.m_nAmmoInClip = weapon.m_nTotalAmmo;

    CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Le diste %u balas a %s.", amount, target->GetName().c_str());
}

// the player's own map waypoint (the pause menu's target blip), while it is still in use (as CGpsRoute reads it)
static bool GetMapWaypoint(CVector2D& out)
{
    const int handle = FrontEndMenuManager.m_nTargetBlipIndex;
    if (!handle)
        return false;
    const uint32_t index = static_cast<uint32_t>(handle) & 0xFFFF;
    if (index >= MAX_RADAR_TRACES)
        return false;
    const tRadarTrace& trace = CRadar::ms_RadarTrace[index];
    if (!trace.m_bInUse || trace.m_nCounter != static_cast<uint16_t>(static_cast<uint32_t>(handle) >> 16))
        return false;
    out = CVector2D(trace.m_vecPos.x, trace.m_vecPos.y);
    return true;
}

// /tpm: to the map waypoint, on foot or with the vehicle the player drives; the ground is found as the scripts do
// (a z of -100 makes SET_CHAR_COORDINATES / SET_CAR_COORDINATES look for it)
static void CommandTeleportToMarker()
{
    CPlayerPed* ped = FindPlayerPed(0);
    if (!ped || ped->m_fHealth <= 0.0f || CCutsceneMgr::ms_running)
    {
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Ahora no podes teletransportarte.");
        return;
    }

    CVector2D target;
    if (!GetMapWaypoint(target))
    {
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Pone un marcador en el mapa (menu de pausa) y usa /tpm.");
        return;
    }

    const int pedRef = CPools::GetPedRef(ped);
    if (CGame::currArea != 0)
        plugin::Command<Commands::SET_AREA_VISIBLE>(0);
    plugin::Command<Commands::SET_CHAR_AREA_VISIBLE>(pedRef, 0);
    plugin::Command<Commands::LOAD_SCENE>(target.x, target.y, 20.0f);

    CVehicle* vehicle = ped->m_nPedFlags.bInVehicle ? ped->m_pVehicle : nullptr;
    if (vehicle && vehicle->m_pDriver == ped)
    {
        const int vehicleRef = CPools::GetVehicleRef(vehicle);
        plugin::Command<Commands::SET_CAR_COORDINATES>(vehicleRef, target.x, target.y, -100.0f);
        vehicle->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
        vehicle->m_vecTurnSpeed = CVector(0.0f, 0.0f, 0.0f);
    }
    else
    {
        plugin::Command<Commands::SET_CHAR_COORDINATES>(pedRef, target.x, target.y, -100.0f);
    }
    CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Teletransportado al marcador.");
}

static void ExecuteCommand(const std::wstring& text)
{
    size_t space = text.find(L' ');
    std::wstring name = text.substr(1, space == std::wstring::npos ? std::wstring::npos : space - 1);
    std::wstring args = space == std::wstring::npos ? L"" : text.substr(space + 1);

    for (auto& c : name)
        c = towlower(c);

    if (name == L"municion" || name == L"ammo" || name == L"balas")
    {
        CommandGiveAmmo(args);
    }
    else if (name == L"guardar" || name == L"save")
    {
        // the game's save menu (the one of the save pickups): choose the slot
        CSaveMenu::Request();
    }
    else if (name == L"igualar")
    {
        char szName[64] = {0};
        WideCharToMultiByte(CP_ACP, 0, args.c_str(), -1, szName, sizeof(szName) - 1, nullptr, nullptr);
        std::string playerName = szName;
        while (!playerName.empty() && playerName.back() == ' ')
            playerName.pop_back();
        CMissionMode::EqualizeWith(playerName);
    }
    else if (name == L"actualizar" || name == L"refrescar")
    {
        std::vector<std::string> names;
        int count = CNetworkPlayerManager::RefreshLookingDead(&names);
        if (count == 0)
        {
            CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} No hay nadie que se vea muerto estando vivo (o estas en una cinematica).");
        }
        else
        {
            std::string list;
            for (auto& n : names)
                list += (list.empty() ? "" : ", ") + n;
            CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Actualizado: %s.", list.c_str());
        }
    }
    else if (name == L"guardarsv" || name == L"guardarserver")
    {
        if (!CNetwork::m_bAuthenticated)
        {
            CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} No estas conectado al servidor.");
            return;
        }

        Packets::Players::SaveAllRequest packet{};
        GetPacketFactory().Send(packet);
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Guardando la partida en el servidor...");
    }
    else if (name == L"mision" || name == L"siguiente" || name == L"progreso")
    {
        CMissionMode::ShowNextMissions();
    }
    else if (CConfigLoader::ms_bChatTpmFijar && name == L"tpm")
    {
        CommandTeleportToMarker();
    }
    else if (CConfigLoader::ms_bChatTpmFijar && name == L"fijar")
    {
        CVehicleSurf::Toggle();
    }
    else if (name == L"ayuda" || name == L"help")
    {
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Comandos: /municion [cantidad] - da municion del arma en mano al jugador mas cercano.");
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} /guardar - abre el menu de guardado del juego (elegis el slot).");
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} /guardarsv - guarda ya el estado de todos los jugadores en el servidor.");
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} /igualar <jugador> - tu progreso de todas las cadenas de historia pasa a ser el suyo.");
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} /actualizar (o /refrescar) - arregla a los jugadores que ves muertos o caidos estando vivos.");
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} /mision - tus siguientes misiones de cada cadena y en cuales difieren los demas.");
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} /tp <jugador>, /ir <jugador>, /traer <jugador>: pedido Y/N (15 s); en modo prueba, sin preguntar. F9 o /anims: animaciones.");
        if (CConfigLoader::ms_bChatTpmFijar)
            CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} /tpm - te lleva al marcador del mapa. /fijar - parado arriba de un vehiculo, quedas pegado (otra vez o saltar: te soltas).");
        if (CTestMode::ms_bEnabled)
            CChat::AddMessage(CTestMode::GetHelpText());
    }
    else if (CSocialActions::Command(name, args)) {}
    else if (CTestMode::HandleChatCommand(name, args))
    {
    }
    else
    {
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Comando desconocido. Escribi /ayuda");
    }
}

void CChatCommands::Process()
{
    std::vector<std::wstring> commands;
    {
        std::lock_guard<std::mutex> lock(ms_queueMutex);
        commands.swap(ms_queuedCommands);
    }

    for (auto& command : commands)
        ExecuteCommand(command);
}

void CChatCommands::HandleGiveAmmo(Packets::Players::GiveAmmo* packet)
{
    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (!localPlayer)
        return;

    eWeaponType weaponType = (eWeaponType)packet->weaponType;
    if (!IsAmmoWeapon(weaponType) || packet->ammo <= 0)
        return;

    CNetworkPlayer* sender = CNetworkPlayerManager::GetPlayer(packet->senderPlayerid);
    std::string senderName = sender ? sender->GetName() : "otro jugador";

    int slot = CWeaponInfo::GetWeaponInfo(weaponType, 1)->m_nSlot;
    if (slot < 0 || slot >= 13)
        return;

    CWeapon& weapon = localPlayer->m_aWeapons[slot];

    if (packet->refund)
    {
        if (weapon.m_eWeaponType == weaponType)
            weapon.m_nTotalAmmo = std::min(weapon.m_nTotalAmmo + (unsigned int)packet->ammo, MAX_TOTAL_AMMO);

        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} %s no pudo recibir la municion (tiene otra arma de ese tipo). Te la devolvimos.", senderName.c_str());
        return;
    }

    if (weapon.m_eWeaponType == weaponType)
    {
        weapon.m_nTotalAmmo = std::min(weapon.m_nTotalAmmo + (unsigned int)packet->ammo, MAX_TOTAL_AMMO);
    }
    else if (weapon.m_eWeaponType == WEAPON_UNARMED || weapon.m_nTotalAmmo == 0)
    {
        LoadWeaponModel(weaponType);
        localPlayer->GiveWeapon(weaponType, packet->ammo, false);
    }
    else
    {
        // another weapon in that slot would be replaced, send the ammo back
        Packets::Players::GiveAmmo refund{};
        refund.targetPlayerid = sender ? sender->m_playerId : playerid_t();
        refund.weaponType = packet->weaponType;
        refund.ammo = packet->ammo;
        refund.refund = true;
        if (sender)
            GetPacketFactory().Send(refund);

        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} %s te quiso dar municion, pero tenes otra arma en ese lugar.", senderName.c_str());
        return;
    }

    CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} %s te dio %d balas.", senderName.c_str(), packet->ammo);
}
