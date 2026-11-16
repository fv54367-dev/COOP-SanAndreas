#pragma once
class MissionAudioHooks
{
public:
	static void InjectHooks();

	// not host: dialogue of the host's coop mission (see Packets::Scripts::PlayMissionAudio)
	static void HandleNetworkAudio(uint8_t slot, int audioid, bool preloadOnly);
	static void HandleNetworkClear(uint8_t slot);
	// every frame on a guest: plays what the host played once it is loaded (mission-audio-guard)
	static void ProcessNetwork();
};

