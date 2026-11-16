#pragma once

class DamageHooks
{
public:
	static void InjectHooks();
	static bool ShouldDiscardAllyEvent(void* event, class CPed* ped);
};
