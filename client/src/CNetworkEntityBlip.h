#pragma once
class CNetworkEntityBlip
{
public:
	static void UpdateEntityBlip(Packets::Blips::UpdateEntityBlip* packet);
	static void RemoveEntityBlip(Packets::Blips::RemoveEntityBlip* packet);
	static void ClearEntityBlips();
	static void Update();

private:
	static bool Apply(const Packets::Blips::UpdateEntityBlip& packet);
	static void DropPending(const CNetworkEntitySerializer& entity);

	// a blip for an entity we don't have yet: a script creates a car and puts its blip on it in one frame, the blip
	// came before the vehicle's spawn and was lost
	struct SPending
	{
		Packets::Blips::UpdateEntityBlip packet;
		unsigned int receivedTime;
	};
	static inline std::vector<SPending> ms_pending;

	// every blip the authority asked for and has not removed: the entity's copy here can be destroyed and created again (hand
	// over, streaming) and the new copy has no blip, so a guest saw the Vigilante criminals' blip for a second and never again
	static inline std::vector<Packets::Blips::UpdateEntityBlip> ms_active;
	static bool IsAliveWithoutBlip(const Packets::Blips::UpdateEntityBlip& packet);
	static void ForgetActive(const CNetworkEntitySerializer& entity);
};

