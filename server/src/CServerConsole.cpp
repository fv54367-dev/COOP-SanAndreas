#include "stdafx.h"
#include "CServerConsole.h"
#include "CSaveManager.h"
#include "CCoopMission.h"
#include "CStringUtil.h"
#include "network/packets/system.h"
#include <chrono>

#if defined(_WIN32)
#include <io.h>
#include <stdio.h>
#else
#include <unistd.h>
#endif

std::mutex CServerConsole::ms_mutex;
std::vector<std::string> CServerConsole::ms_queue;
std::thread CServerConsole::ms_thread;
uint32_t CServerConsole::ms_nStartTime = 0;

static uint32_t ConsoleNowMs()
{
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void CServerConsole::Start()
{
    ms_nStartTime = ConsoleNowMs();
    CServerLog::Printf("[!] : Console ready, type 'help' for the command list\n");

    ms_thread = std::thread(ReaderThread);
    // E bug 9: not detached and never joined: std::getline cannot be interrupted portably, so the process is left
    // through std::quick_exit (main/console handler), which does not destroy std::cin or the queue/mutex under it
}

void CServerConsole::ReaderThread()
{
#if defined(_WIN32)
    if (!_isatty(_fileno(stdin)))
        return;  // started without a console
#else
    if (!isatty(fileno(stdin)))
        return;
#endif

    std::string line;
    while (std::getline(std::cin, line))
    {
        // whatever the console/pipe left around
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
        {
            line.pop_back();
        }

        if (line.empty())
            continue;

        std::lock_guard<std::mutex> lock(ms_mutex);
        if (ms_queue.size() < 64)
        {
            ms_queue.push_back(line);
        }
    }
}

void CServerConsole::Process()
{
    std::vector<std::string> commands;
    {
        std::lock_guard<std::mutex> lock(ms_mutex);
        commands.swap(ms_queue);
    }

    for (const auto& command : commands)
    {
        Execute(command);
    }
}

void CServerConsole::Execute(const std::string& line)
{
    size_t space = line.find(' ');
    std::string name = space == std::string::npos ? line : line.substr(0, space);
    std::string args = space == std::string::npos ? std::string() : line.substr(space + 1);

    for (auto& c : name)
    {
        c = (char)tolower((unsigned char)c);
    }

    if (name == "help")
    {
        PrintHelp();
    }
    else if (name == "status")
    {
        PrintStatus();
    }
    else if (name == "players")
    {
        PrintPlayers();
    }
    else if (name == "save")
    {
        CServerLog::Printf("[Console] : forcing a save of every player...\n");
        CSaveManager::RequestSaveAll(nullptr);
    }
    else if (name == "say")
    {
        Say(args);
    }
    else if (name == "kick")
    {
        Kick(args);
    }
    else
    {
        CServerLog::Printf("[Console] : unknown command '%s' (type 'help')\n", name.c_str());
    }
}

void CServerConsole::PrintHelp()
{
    CServerLog::Printf("[Console] : commands\n");
    CServerLog::Printf("  help            this list\n");
    CServerLog::Printf("  status          players, host, mission, entities and uptime\n");
    CServerLog::Printf("  players         id, nickname, ping and position of every player\n");
    CServerLog::Printf("  save            force the persistence save of every player (same as /guardar)\n");
    CServerLog::Printf("  say <text>      send a chat message from the server to everyone\n");
    CServerLog::Printf("  kick <id>       disconnect a player\n");
}

void CServerConsole::PrintStatus()
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetHost();
    std::string hostName = pNetworkPlayer != nullptr ? pNetworkPlayer->GetName() : "-";
    bool bOnMission = pNetworkPlayer != nullptr && pNetworkPlayer->m_bOnMission;

    // F: a coop side mission with an owner, if one is active
    char missionText[96];
    if (CCoopMission::IsActive())
    {
        CNetworkPlayer* pOwner = CNetworkPlayerManager::GetPlayer(CCoopMission::GetOwnerId());
        snprintf(missionText, sizeof(missionText), "%s %s (%s) by %s",
            CCoopMission::IsRunning() ? "coop" : "coop offer", CCoopMission::GetTitle(),
            CCoopMission::GetPhase() == CCoopMission::PHASE_RUNNING ? "running" : "deciding",
            pOwner != nullptr ? pOwner->GetName().c_str() : "-");
    }
    else
    {
        snprintf(missionText, sizeof(missionText), "%s", bOnMission ? "active (host)" : "none");
    }

    uint32_t uptime = (ConsoleNowMs() - ms_nStartTime) / 1000;

    CServerLog::Printf("[Console] : status\n");
    CServerLog::Printf("  players     %u\n", (unsigned)CNetworkPlayerManager::m_pPlayers.size());
    CServerLog::Printf("  host        %s\n", hostName.c_str());
    CServerLog::Printf("  mission     %s\n", missionText);
    CServerLog::Printf("  entities    %u peds, %u vehicles\n", (unsigned)CNetworkPedManager::m_pPeds.size(),
        (unsigned)CNetworkVehicleManager::m_pVehicles.size());
    CServerLog::Printf("  uptime      %02u:%02u:%02u\n", uptime / 3600, (uptime / 60) % 60, uptime % 60);
}

void CServerConsole::PrintPlayers()
{
    if (CNetworkPlayerManager::m_pPlayers.empty())
    {
        CServerLog::Printf("[Console] : no players connected\n");
        return;
    }

    CServerLog::Printf("[Console] : players\n");
    for (auto* pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        CServerLog::Printf("  p%-3u %-16s ping %4u ms   pos %.1f %.1f %.1f (area %u)%s\n", pNetworkPlayer->m_playerId.value(),
            pNetworkPlayer->GetName().c_str(), pNetworkPlayer->m_pPeer != nullptr ? pNetworkPlayer->m_pPeer->roundTripTime : 0,
            pNetworkPlayer->m_vecPos.x, pNetworkPlayer->m_vecPos.y, pNetworkPlayer->m_vecPos.z, pNetworkPlayer->m_nAreaCode,
            pNetworkPlayer->m_bIsHost ? "  [host]" : "");
    }
}

void CServerConsole::Say(const std::string& args)
{
    if (args.empty())
    {
        CServerLog::Printf("[Console] : usage: say <text>\n");
        return;
    }

    Packets::System::ServerChat chat{};
    ServerTextToWide(args.c_str(), chat.message, ARRAY_SIZE(chat.message));
    GetPacketFactory().SendToAll(chat);

    CServerLog::Printf("[Console] : said: %s\n", args.c_str());
}

void CServerConsole::Kick(const std::string& args)
{
    if (args.empty())
    {
        CServerLog::Printf("[Console] : usage: kick <id>\n");
        return;
    }

    uint32_t id = (uint32_t)strtoul(args.c_str(), nullptr, 10);
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(playerid_t(id));
    if (pNetworkPlayer == nullptr)
    {
        CServerLog::Printf("[Console] : no player with id %u\n", id);
        return;
    }

    CServerLog::Printf("[Console] : kicking %s (id %u)\n", pNetworkPlayer->GetName().c_str(), id);
    CNetwork::KickPlayer(pNetworkPlayer, Packets::System::PlayerDisconnected::DISCONNECTION_REASON_KICKED);
}
