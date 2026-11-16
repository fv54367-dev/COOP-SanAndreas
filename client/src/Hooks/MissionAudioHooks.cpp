#include "stdafx.h"
#include "MissionAudioHooks.h"
#include "COpCodeSync.h"
#include <CCoopMission.h>
#include <CConfigLoader.h>
#include <CMissionMirror.h>

static int aiLoadedAudioId[4];
static int aiNetworkPreloadedAudioId[4] = {-1, -1, -1, -1};

// mission-audio-guard (the guest): his own state per slot. The host's packets come at the pace of the host's playback, but ours
// ends later (load + latency): a preload of the next line used to land on a slot still talking (the line was cut or came
// back), and a status read right after a preload could still be the one of the previous audio.
static DWORD anPlayStartedAt[4];
static int aiPlayedAudioId[4] = {-1, -1, -1, -1};
static bool abQueuedPreload[4];
static int aiQueuedAudioId[4];
static DWORD anQueuedAt[4];
static unsigned int anPreloadFrame[4];
static constexpr DWORD GUARD_MIN_PLAY_MS = 400;     // the sample needs this long to report it plays
static constexpr DWORD GUARD_MAX_PLAY_MS = 30000;   // never trust a slot to be busy longer than this
static constexpr DWORD GUARD_MAX_QUEUE_MS = 4000;   // a queued preload goes anyway after this
static constexpr DWORD GUARD_REPEAT_MS = 2500;      // the same line asked again this soon is a repeat

static bool IsSlotBusy(uint8_t slot)
{
	if (!anPlayStartedAt[slot])
		return false;
	const DWORD elapsed = GetTickCount() - anPlayStartedAt[slot];
	if (elapsed > GUARD_MAX_PLAY_MS)
		return false;
	return elapsed < GUARD_MIN_PLAY_MS || !plugin::CallMethodAndReturn<bool, 0x5072C0>(&AudioEngine, slot); // IsMissionAudioSampleFinished
}

static void NetworkPreload(uint8_t slot, int audioid)
{
	plugin::CallMethod<0x507290>(&AudioEngine, slot, audioid); // CAudioEngine__PreloadMissionAudio
	aiNetworkPreloadedAudioId[slot] = audioid;
	anPreloadFrame[slot] = CTimer::m_FrameCounter;
	anPlayStartedAt[slot] = 0;
}

// only the dialogues of the coop mission go to the others, not the ones of a mission the host plays alone nor his phone
// calls (each player gets his own)
static bool ShouldSendMissionAudio()
{
	return CCoopMission::IsScriptAuthority() && CNetwork::m_bAuthenticated && COpCodeSync::IsSyncedMissionRunning();
}

void __fastcall CAudioEngine__PreloadMissionAudio_Hook(CAudioEngine* This, SKIP_EDX, uint8_t slot, int audioid)
{
	plugin::CallMethod<0x507290>(This, slot, audioid);

	if (CCoopMission::IsScriptAuthority() && slot < 4)
	{
		aiLoadedAudioId[slot] = audioid;

		// the others start loading it now, so it is ready when the host plays it
		if (ShouldSendMissionAudio())
		{
			Packets::Scripts::PlayMissionAudio packet{};
			packet.audioid = audioid;
			packet.slotid = slot;
			packet.preloadOnly = true;
			GetPacketFactory().Send(packet);
		}
	}
}

void __fastcall CAudioEngine__PlayLoadedMissionAudio_Hook(CAudioEngine* This, SKIP_EDX, uint8_t slot)
{
	plugin::CallMethod<0x5072B0>(This, slot);

	if (slot < 4 && ShouldSendMissionAudio())
	{
		Packets::Scripts::PlayMissionAudio packet{};
		packet.audioid = aiLoadedAudioId[slot];
		packet.slotid = slot;
		GetPacketFactory().Send(packet);
	}
}

void MissionAudioHooks::HandleNetworkAudio(uint8_t slot, int audioid, bool preloadOnly)
{
	if (CCoopMission::IsScriptAuthority() || slot >= 4)
		return;

	if (CConfigLoader::ms_bMissionAudioGuard)
	{
		if (preloadOnly)
		{
			// the slot still plays the previous line: wait for it to end (ProcessNetwork), do not cut it
			if (IsSlotBusy(slot))
			{
				abQueuedPreload[slot] = true;
				aiQueuedAudioId[slot] = audioid;
				anQueuedAt[slot] = GetTickCount();
				COpCodeSync::ms_abLoadingMissionAudio[slot] = false;
				CMissionMirror::LogEventf("audio-guard slot=%d preload=%d queued (slot plays %d)", slot, audioid, aiPlayedAudioId[slot]);
				return;
			}
			abQueuedPreload[slot] = false;
			NetworkPreload(slot, audioid);
			COpCodeSync::ms_abLoadingMissionAudio[slot] = false;
			return;
		}

		// the same line asked again right after it started: a repeat, not a new line
		if (aiPlayedAudioId[slot] == audioid && anPlayStartedAt[slot] && GetTickCount() - anPlayStartedAt[slot] < GUARD_REPEAT_MS)
		{
			CMissionMirror::LogEventf("audio-guard slot=%d play=%d ignored (repeat)", slot, audioid);
			return;
		}
		if (abQueuedPreload[slot] && aiQueuedAudioId[slot] == audioid)
		{
			// its preload is waiting for the slot: ProcessNetwork plays it once loaded
			COpCodeSync::ms_abLoadingMissionAudio[slot] = true;
			return;
		}
		abQueuedPreload[slot] = false;
		if (aiNetworkPreloadedAudioId[slot] != audioid)
			NetworkPreload(slot, audioid);
		aiNetworkPreloadedAudioId[slot] = -1;
		COpCodeSync::ms_abLoadingMissionAudio[slot] = true;
		aiPlayedAudioId[slot] = audioid;
		return;
	}

	if (preloadOnly)
	{
		plugin::CallMethod<0x507290>(&AudioEngine, slot, audioid); // CAudioEngine__PreloadMissionAudio
		aiNetworkPreloadedAudioId[slot] = audioid;
		COpCodeSync::ms_abLoadingMissionAudio[slot] = false;
		return;
	}

	// not preloaded yet (or replaced): load it now, it plays as soon as it is loaded
	if (aiNetworkPreloadedAudioId[slot] != audioid)
	{
		plugin::CallMethod<0x507290>(&AudioEngine, slot, audioid); // CAudioEngine__PreloadMissionAudio
	}
	aiNetworkPreloadedAudioId[slot] = -1;
	COpCodeSync::ms_abLoadingMissionAudio[slot] = true;
}

void MissionAudioHooks::HandleNetworkClear(uint8_t slot)
{
	if (slot >= 4)
		return;

	aiNetworkPreloadedAudioId[slot] = -1;
	COpCodeSync::ms_abLoadingMissionAudio[slot] = false;
	abQueuedPreload[slot] = false;
	anPlayStartedAt[slot] = 0;
	aiPlayedAudioId[slot] = -1;
}

// every frame on a guest of a coop mission: starts the audio the host played once it is loaded
void MissionAudioHooks::ProcessNetwork()
{
	const bool guard = CConfigLoader::ms_bMissionAudioGuard;
	for (uint8_t i = 0; i < 4; i++)
	{
		if (guard && abQueuedPreload[i] && (!IsSlotBusy(i) || GetTickCount() - anQueuedAt[i] > GUARD_MAX_QUEUE_MS))
		{
			abQueuedPreload[i] = false;
			NetworkPreload(i, aiQueuedAudioId[i]);
		}
		if (!COpCodeSync::ms_abLoadingMissionAudio[i] || (guard && abQueuedPreload[i]))
			continue;
		// a status read in the frames right after a preload can still be the previous audio's
		if (guard && anPreloadFrame[i] && CTimer::m_FrameCounter - anPreloadFrame[i] < 3)
			continue;
		if (plugin::CallMethodAndReturn<int8_t, 0x5072A0>(&AudioEngine, i) == 1) // CAudioEngine__GetMissionAudioLoadingStatus
		{
			plugin::CallMethod<0x5072B0>(&AudioEngine, i); // CAudioEngine__PlayLoadedMissionAudio
			COpCodeSync::ms_abLoadingMissionAudio[i] = false;
			anPlayStartedAt[i] = GetTickCount();
		}
	}
}

void MissionAudioHooks::InjectHooks()
{
	patch::RedirectCall(0x4851BB, CAudioEngine__PreloadMissionAudio_Hook);
	patch::RedirectCall(0x48521C, CAudioEngine__PlayLoadedMissionAudio_Hook);
}
