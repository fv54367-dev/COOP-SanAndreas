#include "CCheatSync.h"
#include "CCheat.h"
#include "CConfigLoader.h"
#include "UI/CCheatMenu.h"
#include "UI/CChat.h"
#include "CNetworkPlayerManager.h"

// Indices into CCheat::m_aCheatsActive[92] / m_aCheatFunctions (0x8A5B58) / m_aCheatHashKeys (0x8A5CC8), GTA SA 1.0.
// Source: read both tables from gta_sa.exe and matched every hash key against CRC32 (final xor 0xFFFFFFFF) of the
// reversed cheat string; function pointers matched against the plugin-sdk CCheat addresses. plugin-sdk has no
// eCheats enum. A null function means AddToCheatString just toggles m_aCheatsActive[i]; the listed toggle functions
// flip their own slot (checked in the disassembly), so the slot always reflects the state.
//
// NOT synced on purpose:
//  - money / wanted level (0-5 HESOYAM/wanted, 65 never wanted, 66 six stars): always per player
//  - weapons, health, armour, stamina, skills, oxygen, hunger, mega jump/punch, infinite ammo/health, adrenaline,
//    fat/muscle/skinny, suicide, parachute/jetpack, vehicle spawns, respect/sex appeal: player-only
//  - 28 perfect handling, 49 bunny hop, 52 smash n boom, 53 all cars nitro, 73 driveby: driving of the own vehicle
//  - 6-10, 57, 58 weather: already synced by CWeatherSync
//  - 12/13 faster/slower gameplay: they scale CTimer::ms_fTimeScale and keep no slot in m_aCheatsActive
//  - 26 blow up all cars: one-shot, the destroyed vehicles are synced by the vehicle sync
namespace
{
enum eSyncedCheat : uint8_t
{
    CHEAT_FASTER_CLOCK = 11,           // YSOHNUL (toggle)
    CHEAT_PEDS_RIOT_GOLFCLUB = 14,     // AJLOJYQY MayhemCheat
    CHEAT_EVERYONE_ATTACKS_YOU = 15,   // BAGOWPG EverybodyAttacksPlayerCheat
    CHEAT_PEDS_ARMED = 16,             // FOOOXFT (toggle)
    CHEAT_WHEELS_ONLY = 27,            // XICWMD (toggle)
    CHEAT_ALL_GREEN_LIGHTS = 30,       // ZEIIVG (toggle)
    CHEAT_AGGRESSIVE_DRIVERS = 31,     // YLTEICZ (toggle)
    CHEAT_PINK_TRAFFIC = 32,           // LLQPFBN PinkCarsCheat
    CHEAT_BLACK_TRAFFIC = 33,          // IOWDLAC BlackCarsCheat
    CHEAT_BOATS_FLY = 35,              // AFSNMSMW (toggle)
    CHEAT_ELVIS = 39,                  // ASBHGRB ElvisLivesCheat
    CHEAT_PEDS_ROCKETS = 40,           // BGLUAWML VillagePeopleCheat
    CHEAT_BEACH_PARTY = 41,            // CIKGCGX BeachPartyCheat
    CHEAT_GANG_MEMBERS = 42,           // MROEMZH GangsCheat
    CHEAT_GANG_LAND = 43,              // BIFBUZZ GangLandCheat
    CHEAT_NINJA = 44,                  // AFPHULTL NinjaCheat
    CHEAT_KINKY = 45,                  // BEKKNQV LoveConquersAllCheat
    CHEAT_CHEAP_CARS = 46,             // BGKGTJH AllCarsAreShitCheat
    CHEAT_FAST_CARS = 47,              // GUSNHDE AllCarsAreGreatCheat
    CHEAT_CARS_FLY = 48,               // RIPAZHA (toggle)
    CHEAT_CARS_FLOAT_AWAY = 54,        // BSXSGGC (toggle)
    CHEAT_ALWAYS_MIDNIGHT = 55,        // XJVSNAJ MidnightCheat
    CHEAT_ORANGE_SKY = 56,             // OFVIAC DuskCheat
    CHEAT_RIOT = 69,                   // IOJUFZN RiotCheat
    CHEAT_FUNHOUSE = 70,               // PRIEBJ FunhouseCheat
    CHEAT_REDUCED_TRAFFIC = 74,        // THGLOJ (toggle)
    CHEAT_COUNTRY_TRAFFIC = 75,        // FVTMNBZ CountrysideInvasionCheat
    CHEAT_RECRUIT_9MM = 76,            // SJMAHPE (toggle)
    CHEAT_COUNTRY_PEDS = 77,           // BMTPWHR (toggle)
    CHEAT_RECRUIT_ROCKETS = 78,        // ZSOXFSQ (toggle)
};

constexpr uint8_t SYNCED_CHEATS[] = {
    CHEAT_FASTER_CLOCK, CHEAT_PEDS_RIOT_GOLFCLUB, CHEAT_EVERYONE_ATTACKS_YOU, CHEAT_PEDS_ARMED, CHEAT_WHEELS_ONLY,
    CHEAT_ALL_GREEN_LIGHTS, CHEAT_AGGRESSIVE_DRIVERS, CHEAT_PINK_TRAFFIC, CHEAT_BLACK_TRAFFIC, CHEAT_BOATS_FLY,
    CHEAT_ELVIS, CHEAT_PEDS_ROCKETS, CHEAT_BEACH_PARTY, CHEAT_GANG_MEMBERS, CHEAT_GANG_LAND, CHEAT_NINJA,
    CHEAT_KINKY, CHEAT_CHEAP_CARS, CHEAT_FAST_CARS, CHEAT_CARS_FLY, CHEAT_CARS_FLOAT_AWAY, CHEAT_ALWAYS_MIDNIGHT,
    CHEAT_ORANGE_SKY, CHEAT_RIOT, CHEAT_FUNHOUSE, CHEAT_REDUCED_TRAFFIC, CHEAT_COUNTRY_TRAFFIC, CHEAT_RECRUIT_9MM,
    CHEAT_COUNTRY_PEDS, CHEAT_RECRUIT_ROCKETS,
};

constexpr int CHEAT_COUNT = 92;

bool s_abLastState[CHEAT_COUNT]{};
bool s_bInitialized = false;

// Cheat announce: the slots of CCheat::m_aCheatFunctions are wrapped, so a cheat used by typing the code (the game applies it
// through the table, also the ones that keep no slot) or from the F8 menu is announced. Remote cheats (HandlePacket) are not.
void (*s_apfnOriginal[CHEAT_COUNT])(){};
bool s_bAnnounceInstalled = false;
bool s_bSuppressAnnounce = false;

std::string CheatName(int index)
{
    const char* name = CCheatMenu::GetCheatName(index);
    if (!name)
        return "truco " + std::to_string(index);
    std::string text = name;
    auto cut = text.find(" (");
    if (cut != std::string::npos)
        text.erase(cut);
    return text;
}

void Announce(const char* who, int index, bool active)
{
    CChat::AddMessage("{c7a6ff}[Trucos]{d6dce8} %s %s: %s", who, active ? "activo" : "desactivo", CheatName(index).c_str());
}

template <int I>
void CheatThunk()
{
    const bool before = CCheat::m_aCheatsActive[I];
    if (s_apfnOriginal[I])
        s_apfnOriginal[I]();
    else
        CCheat::m_aCheatsActive[I] = !CCheat::m_aCheatsActive[I];  // no function: the game only toggles the slot
    if (s_bSuppressAnnounce || !CConfigLoader::ms_bCheatAnnounce)
        return;
    const bool active = !(before && !CCheat::m_aCheatsActive[I]);  // a slot that turned off = off; one-shot cheats = on
    Announce("Vos", I, active);
    if (CNetwork::m_bAuthenticated)
    {
        Packets::World::CheatAnnounce packet{};
        packet.index = I;
        packet.active = active;
        GetPacketFactory().Send(packet);
    }
}

template <int... Is>
void InstallThunks(std::integer_sequence<int, Is...>)
{
    void (*thunks[])() = {&CheatThunk<Is>...};
    for (int i = 0; i < CHEAT_COUNT; i++)
    {
        s_apfnOriginal[i] = CCheat::m_aCheatFunctions[i];
        CCheat::m_aCheatFunctions[i] = thunks[i];
    }
}

void InstallAnnounce()
{
    if (s_bAnnounceInstalled || !CConfigLoader::ms_bCheatAnnounce)
        return;
    s_bAnnounceInstalled = true;
    InstallThunks(std::make_integer_sequence<int, CHEAT_COUNT>{});
}

bool IsSynced(uint8_t index)
{
    for (uint8_t synced : SYNCED_CHEATS)
    {
        if (synced == index)
            return true;
    }
    return false;
}

// Establish a baseline outside a session or acknowledge the full state we sent.
void Snapshot()
{
    for (uint8_t index : SYNCED_CHEATS)
    {
        s_abLastState[index] = CCheat::m_aCheatsActive[index];
    }
    s_bInitialized = true;
}

void SendState(uint8_t index, bool active)
{
    Packets::World::CheatSync packet{};
    packet.index = index;
    packet.active = active;
    GetPacketFactory().Send(packet);
}
}  // namespace

void CCheatSync::Process()
{
    InstallAnnounce();
    if (!CNetwork::m_bAuthenticated || !s_bInitialized)
    {
        Snapshot();
        return;
    }

    for (uint8_t index : SYNCED_CHEATS)
    {
        bool active = CCheat::m_aCheatsActive[index];
        if (active != s_abLastState[index])
        {
            s_abLastState[index] = active;
            SendState(index, active);
        }
    }
}

// host -> everyone (called when a player connects): full state of the synced cheats
void CCheatSync::SyncCurrentState()
{
    for (uint8_t index : SYNCED_CHEATS)
    {
        SendState(index, CCheat::m_aCheatsActive[index]);
    }
    Snapshot();
}

void CCheatSync::HandlePacket(Packets::World::CheatSync* pCheatSync)
{
    uint8_t index = pCheatSync->index;
    if (index >= CHEAT_COUNT || !IsSynced(index))
        return;

    if (!s_bInitialized)
        Snapshot();

    bool active = pCheatSync->active;
    if (CCheat::m_aCheatsActive[index] != active)
    {
        // Each packet is an absolute slot state. The sender sends related slots separately.
        bool before[CHEAT_COUNT]{};
        for (uint8_t synced : SYNCED_CHEATS)
            before[synced] = CCheat::m_aCheatsActive[synced];

        // Keep native side effects, but do not toggle other slots a second time on receipt.
        if (auto pfnToggle = CCheat::m_aCheatFunctions[index])
        {
            s_bSuppressAnnounce = true;  // a remote cheat: its owner announces it
            pfnToggle();
            s_bSuppressAnnounce = false;
            for (uint8_t synced : SYNCED_CHEATS)
            {
                if (synced != index)
                    CCheat::m_aCheatsActive[synced] = before[synced];
            }
        }
        else
        {
            // plain toggle: the game only reads the slot
            CCheat::m_aCheatsActive[index] = active;
        }
    }

    // Acknowledge only this slot; unrelated local changes still need to be sent.
    s_abLastState[index] = CCheat::m_aCheatsActive[index];
}

void CCheatSync::HandleAnnounce(Packets::World::CheatAnnounce* pAnnounce)
{
    if (!CConfigLoader::ms_bCheatAnnounce || pAnnounce->index >= CHEAT_COUNT)
        return;
    CNetworkPlayer* pPlayer = CNetworkPlayerManager::GetPlayer(pAnnounce->senderPlayerid);
    Announce(pPlayer ? pPlayer->GetName().c_str() : "Un jugador", pAnnounce->index, pAnnounce->active);
}
