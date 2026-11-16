#include "CServerTime.h"
#include "network/packet.h"
#include "network/packet_types.h"
#include "network/packets/system.h"
#include "stdafx.h"
#include "CPacketBuffer.h"

void CPacketBuffer::Receive(Packet* pPacket)
{
    if (pPacket->GetChannel() == ePacketChannel::SYSTEM || pPacket->GetChannel() == ePacketChannel::VOICE)
    {
        GetPacketHandler().ProcessPacket(pPacket);
        delete pPacket;
        return;
    }

    if (pPacket->GetChannel() == ePacketChannel::SYNC)
    {
        AddTransitSample(pPacket);
    }

    // safety cap: if processing stalls (e.g. server time stopped) drop the oldest packets instead of growing forever
    static constexpr size_t MAX_BUFFERED_PACKETS = 20000;
    while (m_packets.size() >= MAX_BUFFERED_PACKETS)
    {
        delete m_packets.front();
        m_packets.pop_front();
    }

    if (m_packets.empty() || pPacket->serverTime >= m_packets.back()->serverTime)
    {
        m_packets.push_back(pPacket);
        return;
    }

    auto it = std::upper_bound(m_packets.begin(), m_packets.end(), pPacket->serverTime,
        [](server_time_t time, const Packet* snapshot) { return time < snapshot->serverTime; });

    m_packets.insert(it, pPacket);
}

void CPacketBuffer::AddTransitSample(Packet* pPacket)
{
    if (!CConfigLoader::ms_bAdaptiveJitterBuffer || pPacket->serverTime == 0)
        return;

    int32_t transit = static_cast<int32_t>(g_serverTime - pPacket->serverTime);
    if (transit < -1000 || transit > 5000)
        return;  // clock not synced yet

    m_transitSamples.emplace_back(g_serverTime, transit);
    if (m_transitSamples.size() > 1024)
        m_transitSamples.pop_front();
}

void CPacketBuffer::UpdateAdaptiveDelay()
{
    if (!CConfigLoader::ms_bAdaptiveJitterBuffer)
    {
        m_delay = m_fixedDelay;
        return;
    }

    if (g_serverTime - m_lastDelayUpdate < 250)
        return;
    m_lastDelayUpdate = g_serverTime;

    while (!m_transitSamples.empty() && g_serverTime - m_transitSamples.front().first > SAMPLE_WINDOW_MS)
        m_transitSamples.pop_front();

    if (m_transitSamples.size() < 10)
        return;

    std::vector<int32_t> transits;
    transits.reserve(m_transitSamples.size());
    for (auto& sample : m_transitSamples)
        transits.push_back(sample.second);

    size_t p95 = transits.size() * 95 / 100;
    std::nth_element(transits.begin(), transits.begin() + p95, transits.end());
    int32_t target = std::clamp<int32_t>(transits[p95] + (int32_t)MARGIN_MS, MIN_DELAY, MAX_DELAY);

    // grow at once (a spike would otherwise stutter), shrink slowly
    if ((uint32_t)target > m_delay)
        m_delay = target;
    else if ((uint32_t)target < m_delay)
        m_delay -= std::min<uint32_t>(m_delay - target, 2);
}

void CPacketBuffer::Process()
{
    UpdateAdaptiveDelay();

    uint32_t renderTime = GetRenderTime(g_serverTime);

    for (auto it = m_packets.begin(); it != m_packets.end();)
    {
        Packet* pPacket = *it;
        if (pPacket->serverTime > renderTime)
        {
            break;
        }

        it = m_packets.erase(it);

        GetPacketHandler().ProcessPacket(pPacket);

        SPacketRecord packetRecord{};
        packetRecord.m_nSize = pPacket->GetBytesRead();
        packetRecord.m_bInbound = true;
        packetRecord.m_bOutbound = false;
        packetRecord.m_packetType = pPacket->GetType();
        packetRecord.m_serverTime = pPacket->serverTime;
        packetRecord.m_sContent = pPacket->ToString();
        GetPacketFactory().AddPacketRecord(packetRecord, renderTime);

        delete pPacket;
    }
}