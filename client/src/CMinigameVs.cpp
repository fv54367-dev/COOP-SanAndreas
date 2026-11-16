#include "CSocialActions.h"
#include "stdafx.h"
#include "CActivityChallenge.h"
#include "CMinigameVs.h"

#include "CLocalPlayer.h"
#include "CNetworkPlayer.h"
#include "CNetworkPlayerManager.h"
#include "UI/CChat.h"
#include "network/packets/scripts.h"

#include "imgui.h"

#include <algorithm>

namespace
{
constexpr int GAME_COUNT = 3;  // ids 1 (dance) and 2 (lowrider)
constexpr uint32_t SEND_INTERVAL_MS = 1000;
constexpr uint32_t STALE_PLAYING_MS = 15000;
constexpr uint32_t PANEL_AFTER_FINISH_MS = 10000;
constexpr uint32_t RESULT_WAIT_MS = 20000;

struct Entry
{
    playerid_t playerId{};
    std::string nick;
    int score = 0;
    uint8_t state = 0;  // 0 playing, 1 finished, 2 abandoned
    uint32_t lastUpdate = 0;
    bool local = false;
};

struct GameState
{
    std::vector<Entry> entries;
    uint8_t localState = 2;  // not playing
    int localScore = 0;
    uint32_t localFinishedAt = 0;
    uint32_t lastSendAt = 0;
    int lastSentScore = 0;
    uint8_t lastSentState = 255;
    bool resultShown = false;
    std::vector<uint32_t> waitingFor;
};

GameState g_games[GAME_COUNT];

Entry* FindLocal(GameState& game)
{
    for (auto& entry : game.entries)
    {
        if (entry.local)
            return &entry;
    }
    return nullptr;
}

Entry* FindById(GameState& game, playerid_t playerId)
{
    for (auto& entry : game.entries)
    {
        if (!entry.local && entry.playerId == playerId)
            return &entry;
    }
    return nullptr;
}

Entry* FindByValue(GameState& game, uint32_t playerId)
{
    for (auto& entry : game.entries)
    {
        if (!entry.local && entry.playerId.value() == playerId)
            return &entry;
    }
    return nullptr;
}

bool HasRival(GameState& game)
{
    for (auto& entry : game.entries)
    {
        if (!entry.local && entry.state != 2)
            return true;
    }
    return false;
}

const char* GameTitle(int gameId)
{
    switch (gameId)
    {
    case 1:
        return "BAILE VS";
    case 2:
        return "LOWRIDER VS";
    default:
        return "MINIJUEGO VS";
    }
}

std::string RemoteNick(playerid_t playerId)
{
    if (CNetworkPlayer* networkPlayer = CNetworkPlayerManager::GetPlayer(playerId))
    {
        std::string name = networkPlayer->GetName();
        if (!name.empty())
            return name;
    }
    return "Jugador " + std::to_string(playerId.value());
}

std::vector<Entry*> SortedEntries(GameState& game)
{
    std::vector<Entry*> rows;
    for (auto& entry : game.entries)
    {
        if (entry.state != 2)
            rows.push_back(&entry);
    }

    std::sort(rows.begin(), rows.end(), [](const Entry* lhs, const Entry* rhs) { return lhs->score > rhs->score; });
    return rows;
}

void ShowResult(GameState& game)
{
    if (!HasRival(game))
        return;

    std::vector<Entry*> rows = SortedEntries(game);
    if (rows.empty())
        return;

    Entry* winner = rows.front();
    CChat::AddMessage("{c7a6ff}[VS]{d6dce8} Gano %s con %d puntos", winner->nick.c_str(), winner->score);

    std::string table;
    for (Entry* entry : rows)
    {
        if (!table.empty())
            table += " | ";

        char row[80];
        snprintf(row, sizeof(row), "%.12s %d", entry->nick.c_str(), entry->score);
        table += row;
    }
    CChat::AddMessage("{c7a6ff}[VS]{d6dce8} %s", table.c_str());
}
}  // namespace

void CMinigameVs::Report(int gameId, int score, int state)
{
    if (gameId <= 0 || gameId >= GAME_COUNT)
        return;

    if (state < 0 || state > 2)
        state = 0;

    const uint32_t now = GetTickCount();
    GameState& game = g_games[gameId];

    // a fresh state 0 after finishing starts a new match: drop the old table
    if (state == 0 && game.localState != 0)
    {
        game.entries.clear();
        game.localFinishedAt = 0;
        game.resultShown = false;
        game.waitingFor.clear();
        game.lastSentScore = 0;
        game.lastSentState = 255;
    }
    // the local run just finished: wait for the rivals that were playing
    else if (state == 1 && game.localState != 1)
    {
        game.localFinishedAt = now;
        game.resultShown = false;
        game.waitingFor.clear();
        for (auto& entry : game.entries)
        {
            if (!entry.local && entry.state == 0)
                game.waitingFor.push_back(entry.playerId.value());
        }
    }

    Entry* local = FindLocal(game);
    if (local == nullptr)
    {
        game.entries.push_back(Entry{});
        local = &game.entries.back();
        local->local = true;
        local->playerId = CNetworkPlayerManager::m_localPlayerId;
    }

    local->nick = CLocalPlayer::m_Name[0] != '\0' ? CLocalPlayer::m_Name : "Vos";
    local->score = score;
    local->state = (uint8_t)state;
    local->lastUpdate = now;

    game.localState = (uint8_t)state;
    game.localScore = score;

    const bool changed = score != game.lastSentScore || (uint8_t)state != game.lastSentState;
    if (changed || now - game.lastSendAt >= SEND_INTERVAL_MS)
    {
        Packets::Scripts::MinigameVsScore packet{};
        packet.gameId = (uint8_t)gameId;
        packet.score = score;
        packet.state = (uint8_t)state;
        GetPacketFactory().Send(packet);

        game.lastSendAt = now;
        game.lastSentScore = score;
        game.lastSentState = (uint8_t)state;
    }
}

void CMinigameVs::OnPacket(const Packets::Scripts::MinigameVsScore& packet)
{
    if (packet.gameId <= 0 || packet.gameId >= GAME_COUNT || !packet.playerid.valid())
        return;

    if (packet.playerid == CNetworkPlayerManager::m_localPlayerId)
        return;

    GameState& game = g_games[packet.gameId];

    Entry* entry = FindById(game, packet.playerid);
    if (entry == nullptr)
    {
        game.entries.push_back(Entry{});
        entry = &game.entries.back();
        entry->local = false;
        entry->playerId = packet.playerid;
    }

    entry->nick = RemoteNick(packet.playerid);
    entry->score = packet.score;
    entry->state = packet.state;
    entry->lastUpdate = GetTickCount();
}

void CMinigameVs::Process()
{
    const uint32_t now = GetTickCount();

    for (int gameId = 1; gameId < GAME_COUNT; gameId++)
    {
        GameState& game = g_games[gameId];

        // a player that is "playing" but stopped reporting has left the minigame
        for (auto it = game.entries.begin(); it != game.entries.end();)
        {
            if (it->state == 0 && now - it->lastUpdate > STALE_PLAYING_MS)
            {
                if (it->local)
                {
                    game.localState = 2;
                    game.localFinishedAt = 0;
                    game.resultShown = false;
                    game.waitingFor.clear();
                }
                it = game.entries.erase(it);
            }
            else
            {
                ++it;
            }
        }

        if (game.localState != 1 || game.resultShown)
            continue;

        if (game.localFinishedAt == 0)
            game.localFinishedAt = now;

        bool waiting = false;
        for (uint32_t playerId : game.waitingFor)
        {
            Entry* entry = FindByValue(game, playerId);
            if (entry != nullptr && entry->state == 0)
            {
                waiting = true;
                break;
            }
        }

        if (!waiting || now - game.localFinishedAt >= RESULT_WAIT_MS)
        {
            game.resultShown = true;
            ShowResult(game);
        }
    }
}

bool CMinigameVs::ShouldDraw()
{
    if (CSocialActions::Visible() || CActivityChallenge::Visible()) return true;
    const uint32_t now = GetTickCount();

    for (int gameId = 1; gameId < GAME_COUNT; gameId++)
    {
        GameState& game = g_games[gameId];

        bool localVisible = game.localState == 0;
        if (!localVisible && game.localState == 1 && game.localFinishedAt != 0)
            localVisible = now - game.localFinishedAt <= PANEL_AFTER_FINISH_MS;

        if (localVisible && HasRival(game))
            return true;
    }

    return false;
}

void CMinigameVs::Draw()
{
    CActivityChallenge::Draw();
    CSocialActions::Draw();
    GameState* game = nullptr;
    int gameId = 0;
    const uint32_t now = GetTickCount();

    for (int i = 1; i < GAME_COUNT; i++)
    {
        GameState& candidate = g_games[i];

        bool localVisible = candidate.localState == 0;
        if (!localVisible && candidate.localState == 1 && candidate.localFinishedAt != 0)
            localVisible = now - candidate.localFinishedAt <= PANEL_AFTER_FINISH_MS;

        if (localVisible && HasRival(candidate))
        {
            game = &candidate;
            gameId = i;
            break;
        }
    }

    if (game == nullptr)
        return;

    std::vector<Entry*> rows = SortedEntries(*game);

    // readable at 1080p and above without covering the screen (1.0 at 720p, ~1.65 at 1080p)
    const float scale = (std::max)(1.0f, ImGui::GetIO().DisplaySize.y / 720.0f) * 1.1f;
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 10.0f * scale, 50.0f * scale), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::Begin(GameTitle(gameId), nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
    ImGui::SetWindowFontScale(scale);

    for (Entry* entry : rows)
    {
        const char* suffix = entry->state == 1 ? " (termino)" : "";
        if (entry->local)
            ImGui::TextColored(ImVec4(0.79f, 0.65f, 1.0f, 1.0f), "%s  %d%s", entry->nick.c_str(), entry->score, suffix);
        else
            ImGui::TextUnformatted((entry->nick + "  " + std::to_string(entry->score) + suffix).c_str());
    }

    ImGui::End();
}
