#pragma once
class CommandHooks
{
public:
	static void InjectHooks();

	// script running the opcode being processed (CRunningScript::Process), or nullptr. Unlike `COpCodeSync`'s
	// `lastProcessedScript` it is set before the opcode runs, so it is the script that created the blip (see RadarHooks)
	static CRunningScript* GetCurrentScript();
	// the opcode being processed, with its NOT bit (0x8000)
	static uint16_t GetCurrentCommand();
    static const uint8_t* GetCurrentCommandIP();
};

