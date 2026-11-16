#include "stdafx.h"
#include "CModCheck.h"

#include "network/packets/mods.h"
#include "network/packets/system.h"
#include "CStringUtil.h"

#include <map>
#include <set>
#include <string>
#include <unordered_map>

namespace
{
struct SManifest
{
    std::map<uint32_t, Packets::Mods::ModelEntry> models;  // (kind << 16) | id
    uint32_t handlingHash = 0;
    uint32_t weaponHash = 0;
    std::set<std::string> names;
    bool complete = false;
    bool checked = false;
};

std::unordered_map<CNetworkPlayer*, SManifest> s_manifests;

bool IsConnected(CNetworkPlayer* pPlayer)
{
    return std::find(CNetworkPlayerManager::m_pPlayers.begin(), CNetworkPlayerManager::m_pPlayers.end(), pPlayer) !=
        CNetworkPlayerManager::m_pPlayers.end();
}

void SendChat(CNetworkPlayer* pTarget, const std::string& text)
{
    Packets::System::ServerChat packet{};
    ServerTextToWide(("[Mods] " + text).c_str(), packet.message, ARRAY_SIZE(packet.message));
    GetPacketFactory().Send(packet, pTarget);
}

// the player against the host; blocks him (mod-check = 2) when the models differ
void Compare(CNetworkPlayer* pPlayer, SManifest& player, CNetworkPlayer* pHost, const SManifest& host)
{
    player.checked = true;
    const uint32_t mode = CConfigManager::GetConfigModCheck();

    Packets::Mods::ModCheckResult result{};
    snprintf(result.hostName, sizeof(result.hostName), "%s", pHost->m_Name);

    auto addModel = [&result](const Packets::Mods::ModelEntry& entry, uint8_t status) {
        if (result.count < Packets::Mods::ModCheckResult::MAX_MODELS)
        {
            result.models[result.count] = entry;
            result.status[result.count] = status;
            result.count++;
        }
        result.totalModels++;
    };
    for (const auto& [key, entry] : host.models)
    {
        auto it = player.models.find(key);
        if (it == player.models.end())
            addModel(entry, Packets::Mods::ModCheckResult::MODEL_HOST_ONLY);
        else if (it->second.key != entry.key)
            addModel(entry, Packets::Mods::ModCheckResult::MODEL_DIFFERENT);
    }
    for (const auto& [key, entry] : player.models)
    {
        if (host.models.find(key) == host.models.end())
            addModel(entry, Packets::Mods::ModCheckResult::MODEL_PLAYER_ONLY);
    }
    result.handlingDiffers = player.handlingHash != host.handlingHash;
    result.weaponsDiffer = player.weaponHash != host.weaponHash;
    for (const auto& name : host.names)
    {
        if (!player.names.count(name) && result.hostOnlyCount < Packets::Mods::ModCheckResult::MAX_NAMES)
            snprintf(result.hostOnly[result.hostOnlyCount++], 32, "%s", name.c_str());
    }
    for (const auto& name : player.names)
    {
        if (!host.names.count(name) && result.playerOnlyCount < Packets::Mods::ModCheckResult::MAX_NAMES)
            snprintf(result.playerOnly[result.playerOnlyCount++], 32, "%s", name.c_str());
    }

    const bool anything = result.totalModels > 0 || result.handlingDiffers || result.weaponsDiffer ||
        result.hostOnlyCount > 0 || result.playerOnlyCount > 0;
    if (!anything)
    {
        CServerLog::Printf("[mod-check] %s: same mods as the host\n", pPlayer->m_Name);
        return;
    }

    result.blocked = mode >= 2 && result.totalModels > 0;
    CServerLog::Printf("[mod-check] %s vs host %s: %d models, handling %d, weapons %d, plugins %d/%d%s\n",
        pPlayer->m_Name, pHost->m_Name, result.totalModels, result.handlingDiffers ? 1 : 0, result.weaponsDiffer ? 1 : 0,
        result.hostOnlyCount, result.playerOnlyCount, result.blocked ? " -> blocked" : "");
    GetPacketFactory().Send(result, pPlayer);

    // the host hears it in one line
    std::string text = std::string(pPlayer->m_Name) + (result.blocked ? " no pudo entrar: " : " tiene mods distintos: ");
    if (result.totalModels > 0)
        text += std::to_string(result.totalModels) + " autos/personajes/armas distintos. ";
    if (result.handlingDiffers)
        text += "otro handling. ";
    if (result.weaponsDiffer)
        text += "otros datos de armas. ";
    if (result.hostOnlyCount + result.playerOnlyCount > 0)
        text += "otros plugins/CLEO.";
    SendChat(pHost, text);

    if (result.blocked)
        CNetwork::KickPlayer(pPlayer, Packets::System::PlayerDisconnected::DISCONNECTION_REASON_MODS_MISMATCH);
}

void CheckPending()
{
    CNetworkPlayer* pHost = CNetworkPlayerManager::GetHost();
    auto hostIt = pHost ? s_manifests.find(pHost) : s_manifests.end();
    if (hostIt == s_manifests.end() || !hostIt->second.complete)
        return;
    for (auto& [pPlayer, manifest] : s_manifests)
    {
        if (pPlayer != pHost && manifest.complete && !manifest.checked && IsConnected(pPlayer))
            Compare(pPlayer, manifest, pHost, hostIt->second);
    }
}
}  // namespace

void CModCheck::ForgetPlayer(CNetworkPlayer* pPlayer)
{
    s_manifests.erase(pPlayer);
}

PACKET_HANDLER(ePacketType::MOD_MANIFEST, Packets::Mods::ModManifest* pManifest, CNetworkPlayer* pNetworkPlayer)
{
    if (CConfigManager::GetConfigModCheck() == 0)
        return;

    SManifest& manifest = s_manifests[pNetworkPlayer];
    if (manifest.complete)
        return;  // already compared: a resent manifest only spammed the log and the host's chat
    if (pManifest->part == 0)
        manifest = SManifest{};

    for (int i = 0; i < pManifest->count; i++)
    {
        auto entry = pManifest->models[i];
        entry.gameName[sizeof(entry.gameName) - 1] = '\0';
        manifest.models[((uint32_t)entry.kind << 16) | entry.id] = entry;
    }
    if (!pManifest->last)
        return;

    manifest.handlingHash = pManifest->handlingHash;
    manifest.weaponHash = pManifest->weaponHash;
    for (int i = 0; i < pManifest->nameCount; i++)
    {
        pManifest->names[i][31] = '\0';
        manifest.names.insert(pManifest->names[i]);
    }
    manifest.complete = true;
    CheckPending();
}
