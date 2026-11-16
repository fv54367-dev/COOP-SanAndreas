#include "stdafx.h"
#include "CMissionMode.h"
#include "COpCodeSync.h"
#include "CCoopMission.h"
#include "CMissionMirror.h"
#include "CConfigLoader.h"
#include "CMissionHints.h"
#include "CMissionSyncRules.h"
#include <network/packets/scripts.h>

namespace
{
// the story chains of the compiled main.scm: the global counter of each one (byte offset in ScriptSpace = index in
// CustomVariables.ini * 4, checked against main.scm) and the mission it starts next for each value
struct SStoryChain
{
    const char* szName;
    int nOffset;
    const char* const* apszMissions;  // nEndValue names, nullptr for a chain that only counts (Catalina)
    int nEndValue;                    // the value once the chain is over
    bool bAlwaysShown;                // a Los Santos chain: it starts at 0 with the game, shown also when 0
};

const char* const s_aszSweet[] = {"Tagging Up Turf", "Cleaning The Hood", "Drive-Thru", "Nines And AK's", "Drive-By", "Sweet's Girl",
    "Cesar Vialpando", "Doberman", "Los Sepulcros"};
const char* const s_aszRyder[] = {"Home Invasion", "Catalyst", "Robbing Uncle Sam"};
const char* const s_aszSmoke[] = {"OG Loc", "Running Dog", "Wrong Side of the Tracks", "Just Business"};
const char* const s_aszStrap[] = {"Life's a Beach", "Madd Dogg's Rhymes", "Management Issues", "House Party", "House Party"};
const char* const s_aszCrash[] = {"Burning Desire", "Gray Imports"};
const char* const s_aszCesar[] = {"High Stakes Lowrider"};
const char* const s_aszFinalLs[] = {"Reuniting The Families", "The Green Sabre"};
const char* const s_aszTruth[] = {"Body Harvest", "Are You Going To San Fierro?"};
const char* const s_aszBCrash[] = {"Badlands"};
const char* const s_aszGarage[] = {"Wear Flowers In Your Hair", "Deconstruction"};
const char* const s_aszZero[] = {"Air Raid", "Supply Lines", "New Model Army"};
const char* const s_aszWoozie[] = {"Mountain Cloud Boys", "Ran Fa Li", "Lure", "Amphibious Assault", "The Da Nang Thang"};
const char* const s_aszSteal[] = {"Zeroing In", "Test Drive", "Customs Fast Track", "Puncture Wounds"};
const char* const s_aszSyndicate[] = {"Photo Opportunity", "Jizzy", "Jizzy", "T-Bone Mendez", "Mike Toreno", "Outrider", "Ice Cold Killa",
    "Pier 69", "Toreno's Last Flight", "Yay Ka-Boom-Boom"};
const char* const s_aszSCrash[] = {"555 WE TIP", "Snail Trail"};
const char* const s_aszToreno[] = {"Monster", "Highjack", "Interdiction", "Verdant Meadows", "esperando la llamada de Toreno", "N.O.E.",
    "Stowaway", "Black Project", "Green Goo"};
const char* const s_aszCasino[] = {"Fender Ketchup", "Explosive Situation", "You've Had Your Chips", "Don Peyote", "Intensive Care",
    "The Meat Business", "Fish in a Barrel", "Freefall", "Saint Mark's Bistro"};
const char* const s_aszVCrash[] = {"Misappropriation", "High Noon"};
const char* const s_aszDoc[] = {"Madd Dogg"};
const char* const s_aszHeist[] = {"Architectural Espionage", "Key To Her Heart", "Dam And Blast", "Cop Wheels", "Up, Up and Away!",
    "Breaking the Bank at Caligula's"};
const char* const s_aszMansion[] = {"A Home In The Hills", "Vertical Bird", "Home Coming", "Cut Throat Business"};
const char* const s_aszGrove[] = {"Beat Down on B Dup", "Grove 4 Life"};
const char* const s_aszRiot[] = {"Riot", "Los Desperados", "End Of The Line", "End Of The Line", "End Of The Line"};

#define STORY_CHAIN(name, offset, missions, always) {name, offset, missions, (int)(sizeof(missions) / sizeof(missions[0])), always}

// index 0 must stay Sweet (sweetProgress, FindSweetMismatch)
const SStoryChain s_aChains[] = {
    STORY_CHAIN("Sweet", 1808, s_aszSweet, true),
    STORY_CHAIN("Ryder", 1812, s_aszRyder, true),
    STORY_CHAIN("Smoke", 1816, s_aszSmoke, true),
    STORY_CHAIN("OG Loc", 1820, s_aszStrap, true),
    STORY_CHAIN("Tenpenny LS", 1824, s_aszCrash, true),
    STORY_CHAIN("Cesar", 1828, s_aszCesar, true),
    STORY_CHAIN("Final LS", 1832, s_aszFinalLs, false),
    // $cat_counter (global 64): CAT1-4 each add 1 (original main.scm); 1956 was a debug-skip variable of CATALIN
    {"Catalina", 256, nullptr, 4, false},
    STORY_CHAIN("The Truth", 1964, s_aszTruth, false),
    STORY_CHAIN("Tenpenny campo", 1972, s_aszBCrash, false),
    STORY_CHAIN("Garaje SF", 2164, s_aszGarage, false),
    STORY_CHAIN("Zero", 2168, s_aszZero, false),
    STORY_CHAIN("Woozie", 2172, s_aszWoozie, false),
    STORY_CHAIN("Jethro y Dwaine", 2176, s_aszSteal, false),
    STORY_CHAIN("Sindicato SF", 2180, s_aszSyndicate, false),
    STORY_CHAIN("Tenpenny SF", 2184, s_aszSCrash, false),
    STORY_CHAIN("Toreno", 2372, s_aszToreno, false),
    STORY_CHAIN("Casino", 2388, s_aszCasino, false),
    STORY_CHAIN("Tenpenny LV", 2392, s_aszVCrash, false),
    STORY_CHAIN("Madd Dogg LV", 2396, s_aszDoc, false),
    STORY_CHAIN("Golpe al Caligula", 2400, s_aszHeist, false),
    STORY_CHAIN("Mansion", 2504, s_aszMansion, false),
    STORY_CHAIN("Grove", 2508, s_aszGrove, false),
    STORY_CHAIN("Riot", 2516, s_aszRiot, false),
};

#undef STORY_CHAIN

static_assert(sizeof(s_aChains) / sizeof(s_aChains[0]) == CMissionMode::STORY_CHAIN_COUNT, "one entry per story chain");

int& ChainCounter(int chain)
{
    return *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[s_aChains[chain].nOffset]);
}
}

bool CMissionMode::AnyOtherCoop()
{
    for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (pNetworkPlayer && !pNetworkPlayer->m_bIndividualMode && !pNetworkPlayer->m_bOtherScripts)
            return true;
    }

    return false;
}

bool CMissionMode::IsPlayingAlone()
{
    return ms_bIndividual || !AnyOtherCoop();
}

void CMissionMode::ReadLocalChains(uint8_t* pOut)
{
    for (int i = 0; i < STORY_CHAIN_COUNT; i++)
    {
        const int counter = CMissionSyncRules::OwnChainCounter(i, ChainCounter(i));
        pOut[i] = counter >= 0 && counter < CHAIN_UNKNOWN ? (uint8_t)counter : CHAIN_UNKNOWN;
    }
}

uint32_t CMissionMode::ReadLocalLaunchers()
{
    static const char* const aszLaunchers[] = {"SWEET", "RYDER", "SMOKE", "STRAP"};
    uint32_t mask = 0;
    for (auto script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
        for (int i = 0; i < static_cast<int>(std::size(aszLaunchers)); i++)
            if (!script->m_bIsMission && _strnicmp(script->m_szName, aszLaunchers[i], 8) == 0)
                mask |= 1u << i;
    return mask;
}

const char* CMissionMode::GetChainName(int chain)
{
    return chain >= 0 && chain < STORY_CHAIN_COUNT ? s_aChains[chain].szName : "?";
}

std::string CMissionMode::GetChainStateName(int chain, int value)
{
    if (chain < 0 || chain >= STORY_CHAIN_COUNT || value < 0 || value >= CHAIN_UNKNOWN)
        return "?";

    const SStoryChain& info = s_aChains[chain];
    if (info.apszMissions == nullptr)
        return std::to_string(value < info.nEndValue ? value : info.nEndValue) + "/" + std::to_string(info.nEndValue) + " misiones" +
               (value >= info.nEndValue ? " (terminada)" : "");

    if (value >= info.nEndValue)
        return "terminada";

    return info.apszMissions[value];
}

bool CMissionMode::IsChainFinished(int chain, int value)
{
    return value != CHAIN_UNKNOWN && value >= s_aChains[chain].nEndValue;
}

void CMissionMode::SendLocal()
{
    if (!CNetwork::m_bAuthenticated)
        return;

    ms_nLastSendTick = GetTickCount();

    Packets::Scripts::PlayerMissionMode packet{};
    packet.individual = ms_bIndividual;
    packet.sweetProgress = ms_nLocalSweetProgress;
    packet.runningMission = ms_nLocalRunningMission;
    packet.vanillaScripts = CConfigLoader::ms_bVanillaScripts;
    ReadLocalChains(packet.chainProgress);
    memcpy(ms_anLocalChainProgress, packet.chainProgress, sizeof(ms_anLocalChainProgress));
    packet.storyLaunchers = ms_nLocalLaunchers = ReadLocalLaunchers();
    GetPacketFactory().Send(packet);
}

void CMissionMode::Init()
{
    Events::gameProcessEvent.after += []
    {
        if (!CNetwork::m_bAuthenticated)
            return;

        // $flag_sweet_mission_counter of the compiled main.scm (see CCommandLocateAllPlayersOnFoot3D); main.scm is loaded
        // once the game runs, so the value is always valid here
        const int counter = CMissionSyncRules::OwnChainCounter(0, ChainCounter(0));
        if (counter >= 0 && counter <= 255)
            ms_nLocalSweetProgress = counter;

        // every story chain: the others learn a change at once (/mision, /igualar)
        uint8_t anChains[STORY_CHAIN_COUNT];
        ReadLocalChains(anChains);
        const bool bChainsChanged = memcmp(anChains, ms_anLocalChainProgress, sizeof(anChains)) != 0;

        // the mission this game runs: the others need it at once (a VS race starts when two players run the same)
        const int missionId = CMissionMirror::GetRunningMissionId();
        const bool bMissionChanged = missionId != ms_nLocalRunningMission;
        ms_nLocalRunningMission = missionId;

        const bool bLaunchersChanged = ReadLocalLaunchers() != ms_nLocalLaunchers;

        if (bMissionChanged || bChainsChanged || bLaunchersChanged || GetTickCount() - ms_nLastSendTick > 5000)
            SendLocal();
    };
}

void CMissionMode::UpdateSweetProgress(int counter)
{
    if (counter == ms_nLocalSweetProgress)
        return;

    ms_nLocalSweetProgress = counter;
    SendLocal();
}

bool CMissionMode::FindSweetMismatch(int localCounter, std::string& outName, int& outTheirCounter)
{
    for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (pNetworkPlayer == nullptr || pNetworkPlayer->m_bIndividualMode || pNetworkPlayer->m_nSweetProgress < 0)
            continue;

        if (pNetworkPlayer->m_nSweetProgress != localCounter)
        {
            outName = pNetworkPlayer->GetName();
            outTheirCounter = pNetworkPlayer->m_nSweetProgress;
            return true;
        }
    }

    return false;
}

std::string CMissionMode::GetSweetMissionName(int counter)
{
    const SStoryChain& sweet = s_aChains[0];
    if (counter >= 0 && counter < sweet.nEndValue)
        return sweet.apszMissions[counter];

    return "otra mision";
}

int CMissionMode::LauncherChain(const char* szLauncher)
{
    // the launchers of CMissionSyncRules' aszStoryLaunchers, by the counter they check (s_aChains)
    static const struct { const char* szName; int nChain; } aLaunchers[] = {
        {"SWEET", 0}, {"RYDER", 1}, {"SMOKE", 2}, {"STRAP", 3}, {"CRASH", 4}, {"LA1FIN", 6}, {"CAT", 7}, {"TRU", 8},
        {"BCRASH", 9}, {"GARAGE", 10}, {"ZERO", 11}, {"WUZI", 12}, {"STEAL", 13}, {"SYND", 14}, {"SCRASH", 15},
        {"DESERT", 16}, {"CASINO", 17}, {"VCRASH", 18}, {"DOC", 19}, {"HEIST", 20}, {"MANSION", 21}, {"GROVE", 22},
        {"RIOT", 23}};
    for (const auto& launcher : aLaunchers)
        if (_strnicmp(szLauncher, launcher.szName, 8) == 0)
            return launcher.nChain;
    return -1;
}

bool CMissionMode::FindChainMismatch(int chain, std::string& outName, int& outTheirs, int& outMine)
{
    if (chain < 0 || chain >= STORY_CHAIN_COUNT)
        return false;
    outMine = CMissionSyncRules::OwnChainCounter(chain, ChainCounter(chain));
    for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (pNetworkPlayer == nullptr || pNetworkPlayer->m_bIndividualMode || !CCoopMission::ShouldIncludeNetworkPlayer(pNetworkPlayer))
            continue;
        const uint8_t theirs = pNetworkPlayer->m_anChainProgress[chain];
        if (theirs == CHAIN_UNKNOWN || theirs == outMine)
            continue;
        outName = pNetworkPlayer->GetName();
        outTheirs = theirs;
        return true;
    }
    return false;
}

void CMissionMode::ShowChainMismatch(int chain, const std::string& otherName, int theirs, int mine)
{
    const uint32_t now = GetTickCount();
    if (now - ms_nLastMismatchTick < 5000)
        return;
    ms_nLastMismatchTick = now;
    CMissionHints::Help("Progreso distinto: " + otherName + " va por " + GetChainStateName(chain, theirs) + " y vos por " +
        GetChainStateName(chain, mine) + ". Para jugarla juntos tienen que ir por la misma mision, o uno pasa a individual (F1).");
    CMissionMirror::LogEventf("rule-start-progress chain=%s mine=%d other=%s theirs=%d", GetChainName(chain), mine,
        otherName.c_str(), theirs);
}

void CMissionMode::ShowProgressMismatch(const std::string& otherName, int theirCounter, int localCounter)
{
    // the marker is asked every frame while the player stands in it
    const uint32_t now = GetTickCount();
    if (now - ms_nLastMismatchTick < 5000)
        return;
    ms_nLastMismatchTick = now;

    static char szMessage[160];
    snprintf(szMessage, sizeof(szMessage), "Progreso distinto: %s va por %s y vos por %s. Uno pasa a individual (F1).", otherName.c_str(),
        GetSweetMissionName(theirCounter).c_str(), GetSweetMissionName(localCounter).c_str());
    CMessages::AddMessageJumpQ(szMessage, 5000, 0, false);
    CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} No pueden empezar en coop: " + otherName + " va por " + GetSweetMissionName(theirCounter) +
        " y vos por " + GetSweetMissionName(localCounter) + ". Para jugar juntos tienen que ir por la misma mision: alguno pasa a modo individual (F1).");
}

void CMissionMode::ShowIndividualOnlyHint()
{
    const uint32_t now = GetTickCount();
    if (now - ms_nLastMismatchTick < 5000)
        return;
    ms_nLastMismatchTick = now;

    CMessages::AddMessageJumpQ("Esta mision es solo individual. Cambia tu modo con F1 a individual para empezarla.", 5000, 0, false);
    CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Esta mision no se juega en coop. Cambia tu modo a individual (F1) para empezarla.");
}

void CMissionMode::SetIndividual(bool bIndividual)
{
    if (ms_bIndividual == bIndividual)
        return;

    // from now on none of the authority's mission reaches this game: a scene it started here would never get its end
    const bool bGuest = !CCoopMission::IsScriptAuthority() && !COpCodeSync::IsLocalMissionRunning();
    if (bIndividual && bGuest && CCutsceneMgr::ms_running)
    {
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Espera a que termine la cinematica para pasar a modo individual.");
        return;
    }

    ms_bIndividual = bIndividual;
    SendLocal();

    // the host's mission leaves this game: its flag, markers, blips, widescreen and texts go
    if (bIndividual && bGuest)
    {
        CCoopMission::LocalCleanup();
        CMessages::ClearMessages(false);
    }

    CChat::AddMessage(bIndividual ? "{c7a6ff}[Coop]{d6dce8} Misiones en modo INDIVIDUAL: las jugas solo, sin esperar a nadie."
                                  : "{c7a6ff}[Coop]{d6dce8} Misiones en modo COOPERATIVO: las jugas con los demas jugadores en coop.");
}

static bool HasKnownChains(const CNetworkPlayer* pNetworkPlayer)
{
    for (int i = 0; i < CMissionMode::STORY_CHAIN_COUNT; i++)
    {
        if (pNetworkPlayer->m_anChainProgress[i] != CMissionMode::CHAIN_UNKNOWN)
            return true;
    }

    return false;
}

void CMissionMode::ShowNextMissions()
{
    uint8_t anLocal[STORY_CHAIN_COUNT];
    ReadLocalChains(anLocal);

    // the local chains going on: Sweet and the Los Santos ones always, the others once started; the finished ones in a count
    int nFinished = 0;
    CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Tus siguientes misiones:");
    for (int i = 0; i < STORY_CHAIN_COUNT; i++)
    {
        if (anLocal[i] == CHAIN_UNKNOWN)
            continue;

        if (IsChainFinished(i, anLocal[i]))
        {
            nFinished++;
            continue;
        }

        if (anLocal[i] == 0 && !s_aChains[i].bAlwaysShown)
            continue;

        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8}   " + std::string(GetChainName(i)) + ": " + GetChainStateName(i, anLocal[i]));
    }
    if (nFinished > 0)
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8}   (" + std::to_string(nFinished) + " cadenas terminadas)");

    // the others: Sweet always, then only the chains where he differs from the local player
    for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (pNetworkPlayer == nullptr)
            continue;

        const std::string name = pNetworkPlayer->GetName();
        std::string header = "{c7a6ff}[Coop]{d6dce8} " + name + " (" + (pNetworkPlayer->m_bIndividualMode ? "individual" : "coop") + "): ";
        if (!HasKnownChains(pNetworkPlayer))
        {
            CChat::AddMessage(header + "todavia no avisa su progreso (unos segundos)");
            continue;
        }

        int nEqual = 0;
        std::vector<std::string> lines;
        for (int i = 0; i < STORY_CHAIN_COUNT; i++)
        {
            const uint8_t theirs = pNetworkPlayer->m_anChainProgress[i];
            if (theirs == CHAIN_UNKNOWN || anLocal[i] == CHAIN_UNKNOWN)
                continue;

            if (theirs == anLocal[i])
            {
                if (i == 0)
                    lines.push_back("{c7a6ff}[Coop]{d6dce8}   Sweet: " + GetChainStateName(i, theirs) + " (igual que vos)");
                else
                    nEqual++;
                continue;
            }

            lines.push_back("{c7a6ff}[Coop]{d6dce8}   " + std::string(GetChainName(i)) + ": " + name + " " + GetChainStateName(i, theirs) +
                            ", vos " + GetChainStateName(i, anLocal[i]));
        }

        CChat::AddMessage(header + "(" + std::to_string(nEqual) + " cadenas iguales a las tuyas)");
        for (const auto& line : lines)
            CChat::AddMessage(line);
    }

    CChat::AddMessage(std::string("{c7a6ff}[Coop]{d6dce8} Tu modo: ") + (ms_bIndividual ? "individual" : "cooperativo") + " (F1 lo cambia).");
}

void CMissionMode::EqualizeWith(const std::string& playerName)
{
    if (playerName.empty())
    {
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Uso: /igualar <nombre del jugador>. Pone tu progreso de todas las misiones de historia igual al suyo.");
        return;
    }

    // a name or the start of it, any case
    auto lower = [](std::string text)
    {
        for (auto& c : text)
            c = (char)tolower((unsigned char)c);
        return text;
    };

    CNetworkPlayer* pTarget = nullptr;
    for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (pNetworkPlayer && lower(pNetworkPlayer->GetName()).rfind(lower(playerName), 0) == 0)
        {
            pTarget = pNetworkPlayer;
            break;
        }
    }

    if (pTarget == nullptr)
    {
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} No hay ningun jugador conectado que se llame asi.");
        return;
    }

    if (!HasKnownChains(pTarget))
    {
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} " + pTarget->GetName() + " todavia no avisa su progreso: espera unos segundos y repeti el comando.");
        return;
    }

    if (COpCodeSync::IsLocalMissionRunning() || CCoopMission::IsActive())
    {
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} No se puede igualar durante una mision.");
        return;
    }

    uint8_t anLocal[STORY_CHAIN_COUNT];
    ReadLocalChains(anLocal);

    // only the chains he told (not unknown) that differ from the local ones
    int nChanged = 0;
    for (int i = 0; i < STORY_CHAIN_COUNT; i++)
    {
        const uint8_t theirs = pTarget->m_anChainProgress[i];
        if (theirs != CHAIN_UNKNOWN && theirs != anLocal[i])
            nChanged++;
    }

    if (nChanged == 0)
    {
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Tu progreso ya es igual al de " + pTarget->GetName() + " en todas las cadenas de misiones.");
        return;
    }

    // it changes what the story markers start for him and skips (or repeats) missions without what they give: ask twice
    const uint32_t now = GetTickCount();
    if (!(ms_equalizeTarget == pTarget->m_playerId) || now - ms_nEqualizeTick > 15000)
    {
        ms_equalizeTarget = pTarget->m_playerId;
        ms_nEqualizeTick = now;
        std::string message = "{c7a6ff}[Coop]{d6dce8} Cambiarian " + std::to_string(nChanged) + " cadenas de misiones a las de " +
                              pTarget->GetName() + " (/mision las muestra).";
        const uint8_t theirSweet = pTarget->m_anChainProgress[0];
        if (theirSweet != CHAIN_UNKNOWN && theirSweet != anLocal[0])
            message += " Sweet pasaria de " + GetChainStateName(0, anLocal[0]) + " a " + GetChainStateName(0, theirSweet) + ".";
        message += " Si es adelante, te saltea misiones y lo que dan. Repeti /igualar " + playerName + " en 15 segundos para confirmar.";
        CChat::AddMessage(message);
        return;
    }

    ms_equalizeTarget = playerid_t{};
    CMissionSyncRules::ForgetOwnCounters();
    for (int i = 0; i < STORY_CHAIN_COUNT; i++)
    {
        const uint8_t theirs = pTarget->m_anChainProgress[i];
        if (theirs != CHAIN_UNKNOWN)
            ChainCounter(i) = theirs;
    }

    const int sweet = ChainCounter(0);
    ms_nLocalSweetProgress = sweet;
    SendLocal();
    CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Listo: " + std::to_string(nChanged) + " cadenas igualadas; tu siguiente mision de Sweet es " +
                      GetChainStateName(0, sweet) + ". Guarda la partida con /guardar.");
}
