#include "stdafx.h"
#include "CStoryProgress.h"
#include "CCoopMission.h"
#include "COpCodeSync.h"
#include "CMissionMode.h"

int CStoryProgress::GetGlobalsEnd()
{
    // 02 00 | 01 | int32: the goto at the start of the main script
    const uint8_t* pSpace = reinterpret_cast<const uint8_t*>(CTheScripts::ScriptSpace);
    if (pSpace[0] != 0x02 || pSpace[1] != 0x00 || pSpace[2] != 0x01)
        return 0;

    const int end = *reinterpret_cast<const int*>(pSpace + 3);
    return (end > 8 && end < 200000) ? end : 0;
}

void CStoryProgress::OnMissionLaunched()
{
    ms_bTracking = false;
    ms_bPassed = false;

    if (!CCoopMission::IsScriptAuthority())
        return;

    const int end = GetGlobalsEnd();
    if (end == 0)
        return;

    ms_base.assign(reinterpret_cast<const uint8_t*>(CTheScripts::ScriptSpace),
        reinterpret_cast<const uint8_t*>(CTheScripts::ScriptSpace) + end);
    ms_bTracking = true;
}

void CStoryProgress::OnMissionPassed()
{
    if (ms_bTracking)
        ms_bPassed = true;
}

void CStoryProgress::OnMissionEnded()
{
    const bool bSend = ms_bTracking && ms_bPassed && CCoopMission::IsScriptAuthority() && CNetwork::m_bAuthenticated;
    ms_bTracking = false;
    ms_bPassed = false;

    if (!bSend)
        return;

    const uint8_t* pSpace = reinterpret_cast<const uint8_t*>(CTheScripts::ScriptSpace);
    const int end = static_cast<int>(ms_base.size());

    Packets::Scripts::StoryProgressSync packet{};
    for (int offset = 8; offset + 4 <= end; offset += 4)
    {
        const uint32_t oldValue = *reinterpret_cast<const uint32_t*>(ms_base.data() + offset);
        const uint32_t newValue = *reinterpret_cast<const uint32_t*>(pSpace + offset);

        // only flags and counters: handles, floats and timers are different on every client
        if (oldValue == newValue || oldValue > 255 || newValue > 255)
            continue;

        if (offset == static_cast<int>(CTheScripts::OnAMissionFlag))
            continue;

        packet.entries[packet.count].offset = offset;
        packet.entries[packet.count].oldValue = static_cast<uint8_t>(oldValue);
        packet.entries[packet.count].newValue = static_cast<uint8_t>(newValue);

        if (++packet.count == Packets::Scripts::StoryProgressSync::MAX_ENTRIES)
        {
            GetPacketFactory().Send(packet);
            packet = Packets::Scripts::StoryProgressSync{};
        }
    }

    if (packet.count > 0)
        GetPacketFactory().Send(packet);

    ms_base.clear();
}

void CStoryProgress::Apply(const Packets::Scripts::StoryProgressSync& packet)
{
    // a player that plays alone does not share the progress of the others' missions, and the authority sent it
    if (CCoopMission::IsScriptAuthority() || CMissionMode::IsIndividual() || COpCodeSync::IsLocalMissionRunning())
        return;

    const int end = GetGlobalsEnd();
    int applied = 0;
    for (int i = 0; i < packet.count; i++)
    {
        const auto& entry = packet.entries[i];
        if (entry.offset < 8 || entry.offset + 4 > end || entry.offset == static_cast<int>(CTheScripts::OnAMissionFlag))
            continue;

        uint32_t& current = *reinterpret_cast<uint32_t*>(&CTheScripts::ScriptSpace[entry.offset]);
        if (current != entry.oldValue)
            continue;

        current = entry.newValue;
        ++applied;
    }

    if (applied > 0)
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Progreso de la historia actualizado (" + std::to_string(applied) + ").");
}
