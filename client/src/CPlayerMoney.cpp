#include "stdafx.h"
#include "CPlayerMoney.h"

static constexpr uint32_t CHECK_INTERVAL_MS = 500;
static constexpr uint32_t RESEND_INTERVAL_MS = 5000;

void CPlayerMoney::Process()
{
    static uint32_t s_nLastCheck = 0;
    static uint32_t s_nLastSent = 0;
    static int32_t s_nSentMoney = 0;
    static bool s_bSent = false;

    if (!CConfigLoader::ms_bPlayerListCoopSa || !CNetwork::m_bAuthenticated)
    {
        s_bSent = false;
        return;
    }

    const uint32_t now = GetTickCount();
    if (now - s_nLastCheck < CHECK_INTERVAL_MS)
        return;
    s_nLastCheck = now;

    const int32_t money = CWorld::Players[0].m_nMoney;
    if (s_bSent && money == s_nSentMoney && now - s_nLastSent < RESEND_INTERVAL_MS)
        return;

    Packets::Players::PlayerMoney packet{};
    packet.money = money;
    GetPacketFactory().Send(packet);

    s_nSentMoney = money;
    s_nLastSent = now;
    s_bSent = true;
}

PACKET_HANDLER(ePacketType::PLAYER_MONEY, Packets::Players::PlayerMoney* pPlayerMoney)
{
    if (CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pPlayerMoney->senderPlayerid))
    {
        pNetworkPlayer->m_nMoney = pPlayerMoney->money;
        pNetworkPlayer->m_bHasMoney = true;
    }
}
