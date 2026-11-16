#pragma once

// F1 menu (ImGui): how the local player plays the missions, together with the others (coop) or alone (individual).
// Same structure as CCheatMenu; the choice itself lives in CMissionMode.
class CModeMenu
{
public:
    static inline bool ms_bOpen = false;

    static void Init();
    static void SetOpen(bool open);
    static void Draw();  // inside an ImGui frame
};
