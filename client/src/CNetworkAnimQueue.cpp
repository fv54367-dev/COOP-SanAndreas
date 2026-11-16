#include "stdafx.h"
#include "CNetworkAnimQueue.h"
#include <COpCodeSync.h>
#include <CTaskSequenceSync.h>

void CNetworkAnimQueue::AddOpCode(const uint8_t* buffer, int bufferSize)
{
	//CChat::AddMessage("AddOpCode %x %d", buffer, bufferSize);

	if (buffer && bufferSize > 0)
		ms_vQueue.emplace_back(buffer, buffer + bufferSize);
}

void CNetworkAnimQueue::Process()
{
	// handlers may re-queue entries (e.g. anim block not loaded yet), so iterate over a local copy
	// instead of the vector that can be reallocated underneath us
	auto vOpCodes = std::move(ms_vQueue);
	ms_vQueue.clear();
	for (const auto& v : vOpCodes)
	{
		COpCodeSync::HandlePacket(v.data(), static_cast<int>(v.size()));
	}

	auto vSequences = std::move(CTaskSequenceSync::ms_vTaskSequences);
	CTaskSequenceSync::ms_vTaskSequences.clear();
	for (auto& v : vSequences)
	{
		CTaskSequenceSync::HandlePacket(v.data(), static_cast<int>(v.size()));
	}

	// safety cap so a never-loading anim cannot make the retry queues grow forever
	constexpr size_t MAX_RETRY_ENTRIES = 256;
	if (ms_vQueue.size() > MAX_RETRY_ENTRIES)
		ms_vQueue.erase(ms_vQueue.begin(), ms_vQueue.end() - MAX_RETRY_ENTRIES);
	if (CTaskSequenceSync::ms_vTaskSequences.size() > MAX_RETRY_ENTRIES)
		CTaskSequenceSync::ms_vTaskSequences.erase(CTaskSequenceSync::ms_vTaskSequences.begin(),
			CTaskSequenceSync::ms_vTaskSequences.end() - MAX_RETRY_ENTRIES);
}
