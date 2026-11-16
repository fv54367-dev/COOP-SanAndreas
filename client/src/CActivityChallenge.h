#pragma once
namespace Packets::Scripts { class ActivityChallenge; }
class CRunningScript;
class CActivityChallenge {
public:
    static void Process();
    static void BeforeCommand(CRunningScript* s, unsigned short op);
    static void AfterCommand(CRunningScript* s, unsigned short op);
    static void Reset();
    static bool Independent(CRunningScript* s);
    static void Draw();
    static bool Visible();
    static void OnPacket(const Packets::Scripts::ActivityChallenge& p);
    static bool FilterCompare(CRunningScript* s, unsigned short op, bool state);
};
