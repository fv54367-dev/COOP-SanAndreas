#pragma once

// Cheat menu (ImGui), opened with a key that neither the game nor the mod uses (F8 by default, "cheat-menu-key" in
// coopandreas.ini). Cheats only affect the local player; vehicles spawned with it are synced like any other vehicle and
// weather cheats are only offered to the host (the host's weather is synced to everyone).
class CCheatMenu
{
public:
    static inline bool ms_bOpen = false;
    static inline int ms_nToggleKey = VK_F8;

    static void Init();
    static void SetOpen(bool open);
    static void Draw();  // inside an ImGui frame
    static const char* GetCheatName(int index);  // nullptr when the menu does not list the slot
};
