#pragma once
class CrashfixHooks
{
public:
	static void InjectHooks();
	// gameProcessEvent: logs the ColModel pool peak (leak vs saturation)
	static void ProcessPoolWatch();
};

