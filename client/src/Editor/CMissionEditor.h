#pragma once

// Mission editor, stage 1 (tareas/TANDA_2026-11-16_claude_editor_etapa1.md): F5 opens a panel that shows the running
// mission (script, offset, last opcodes, what it created), freezes the world, moves a free camera and saves marks and
// recorded paths to <game>\CoopAndreas\editor\<script>.txt. Nothing is applied to the game yet: the files are for
// whoever fixes the mission. Local only, nothing goes over the network. Switch: "mission-editor" (off by default).
class CMissionEditor
{
public:
    static inline int ms_nToggleKey = VK_F5;

    static void Init();
    static bool ShouldDraw();
    static void Draw();  // inside an ImGui frame
    static bool IsOpen();
    static bool IsFreeCam();
    // every opcode a script runs (COpCodeSync's FinishedOpcodeProcessing_Hook); cheap when the editor is off
    static void BeforeCommand(CRunningScript* script, uint16_t opcode);
};
