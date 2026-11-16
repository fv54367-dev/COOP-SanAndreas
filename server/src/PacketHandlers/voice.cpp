#include "stdafx.h"
#include "network/packet_handler.h"
#include "network/packets/voice.h"
#include "ConfigManager.h"
#include <cmath>

PACKET_HANDLER(ePacketType::VOICE_FRAME, Packets::Voice::Frame* packet, CNetworkPlayer* sender)
{
    if (!CConfigManager::GetVoiceChat() || sender->m_bIndividualMode || !sender->m_nPosTime ||
        g_serverTime - sender->m_nPosTime > 2000) return;
    if (g_serverTime - sender->m_nVoiceWindow >= 1000)
    {
        sender->m_nVoiceWindow = g_serverTime;
        sender->m_nVoiceFrames = 0;
    }
    if (++sender->m_nVoiceFrames > 60) return;
    packet->playerid = sender->m_playerId;
    packet->serverTime = g_serverTime;
    const float range = CConfigManager::GetVoiceRange();
    for (auto* target : CNetworkPlayerManager::m_pPlayers)
    {
        if (target == sender || target->m_bIndividualMode || target->m_nAreaCode != sender->m_nAreaCode ||
            !target->m_nPosTime || g_serverTime - target->m_nPosTime > 2000) continue;
        auto delta = target->m_vecPos - sender->m_vecPos;
        const float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        if (!std::isfinite(distance) || distance >= range) continue;
        packet->volume = static_cast<uint8_t>(255.0f * (1.0f - distance / range));
        GetPacketFactory().Send(*packet, target);
    }
}
