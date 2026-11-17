#pragma once

// Internal entry points; public progress queries stay on CMissionSyncRules.
class CMissionRulesCounters
{
public:
    static bool __cdecl SaveOwnProgress(int arg);
    static void Process();
};
