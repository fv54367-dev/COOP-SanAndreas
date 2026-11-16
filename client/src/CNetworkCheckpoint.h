#pragma once
class CNetworkCheckpoint
{
public:
	static inline bool ms_bEnabled{};
	static inline CVector ms_vecPosition{};
	static inline CVector ms_vecRadius{};
	// blips are local to each client: the owner keeps the one created by his own script
	static inline int ms_nBlipHandle = -1;

	static void Process();
	static void Update(CVector& vecPosition, CVector& vecRadius);
	static void Remove();

private:
	static void ClearBlip();
	static inline CVector ms_vecBlipPosition{};
};
