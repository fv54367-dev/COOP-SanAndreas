#pragma once

#include <cstdint>
#include <string>

class CRunningScript;

// Stage 1 of the mission editor (docs/PLAN_EDITOR_MISIONES.md, tareas/TANDA_2026-11-16_claude_reglas_datos.md): coop
// rules written as data, one per line in the format of the editor's marks (Editor/CMissionEditor.cpp), read from
// <game>\CoopAndreas\reglas_misiones.txt (shipped) and, with mission-editor, reglas_prueba.txt (local tests). Only the
// script authority acts, through what the hand-written rules already use. Switch `mission-rules-data`.
class CMissionRulesData
{
public:
    static void Init();
    // every command a script runs (COpCodeSync's FinishedOpcodeProcessing_Hook): the "offset" triggers
    static void BeforeCommand(CRunningScript* script, uint16_t opcode);
    // read the files again (the editor's "probar como reglas")
    static void Reload();
    static std::string TestFilePath();
};
