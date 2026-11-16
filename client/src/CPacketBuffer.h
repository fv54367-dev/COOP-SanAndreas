#pragma once

#include "network/packet.h"
#include <deque>

class CPacketBuffer
{
public:
    CPacketBuffer(uint32_t delay) : m_delay(delay), m_fixedDelay(delay) {}
    ~CPacketBuffer() {}

    void Receive(Packet* pPacket);
    void Process();

    uint32_t GetRenderTime(server_time_t serverTime) { return serverTime - m_delay; }
    uint32_t GetDelay() const { return m_delay; }

    std::deque<Packet*> m_packets;

private:
    // adaptive delay: covers the transit time (sender -> server -> us) of almost every sync packet,
    // so a late one still arrives before it's due
    static constexpr uint32_t MIN_DELAY = 30;
    static constexpr uint32_t MAX_DELAY = 300;
    static constexpr uint32_t SAMPLE_WINDOW_MS = 3000;
    static constexpr uint32_t MARGIN_MS = 10;

    void AddTransitSample(Packet* pPacket);
    void UpdateAdaptiveDelay();

    uint32_t m_delay;
    uint32_t m_fixedDelay;
    std::deque<std::pair<server_time_t, int32_t>> m_transitSamples;  // arrival time, transit
    server_time_t m_lastDelayUpdate = 0;
};

extern inline CPacketBuffer& GetPacketBuffer()
{
    static CPacketBuffer buffer(Config::INTERP_BUFFER_DELAY_MS);
    return buffer;
}
