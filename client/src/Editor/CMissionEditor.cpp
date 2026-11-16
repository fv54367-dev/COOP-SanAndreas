#include "stdafx.h"
#include "CMissionEditor.h"
#include "imgui.h"
#include "Debug/CImGui.h"
#include "CConfigLoader.h"
#include "CTestMode.h"
#include "CMissionRulesData.h"
#include <CTimer.h>
#include <CMissionCleanup.h>
#include <fstream>
#include <sstream>
#include <deque>

// Pause: CPatch turns CTimer::StartUserPause into a ret (the world must never stop in coop) and a user pause would also
// stop TheCamera.Process (gta-reversed Game.cpp CGame::Process). Time scale 0 instead: CTimer::Update multiplies the
// frame delta by it, so the game clock (script waits) stops and the time step drops to its 0.01 floor (UpdateVariables),
// while scripts, camera and rendering keep running. Only allowed with no other player or in test mode.
// Free camera: the same calls as FIXED_CAMERA_POSITION + POINT_CAMERA_AT_POINT (CCamera::SetCamPositionForFixedMode,
// TakeControlNoEntity with a jump cut), every frame; RESTORE_CAMERA_JUMPCUT gives the camera back.

namespace
{
constexpr unsigned short CONTROLS_BIT = 0x800;  // CPad::DisablePlayerControls bit of the editor (0x200 ImGui, 0x400 cheats)
constexpr int MAX_OPCODES = 40;
constexpr unsigned RECORD_STEP_MS = 100;
constexpr float FREECAM_RANGE = 300.0f;  // farther than this from the player the world around the camera is not loaded

struct OpcodeEntry
{
    char script[8];
    uint16_t opcode;
    int offset;
    unsigned time;
};

struct PathPoint
{
    CVector pos;
    float heading;
};

struct Mark
{
    std::string type;
    std::string script;
    int offset = -1;
    CVector pos;
    float heading = 0.0f;
    int interior = 0;
    int model = -1;
    std::string note;
    std::vector<PathPoint> path;  // type "recorrido"
};

const char* const MARK_TYPES[] = {"auto_invitado", "esperar_a_todos", "no_esperar", "invitado_sube", "invitado_baja", "dar_arma", "nota"};
const char* const MARK_HELP[] = {
    "Aca aparece un auto para el invitado (se guarda el modelo del auto en que estes)",
    "En este marcador la mision espera a todos los jugadores",
    "En este marcador NO se espera a nadie",
    "Aca el invitado se sube solo al auto",
    "Aca el invitado se baja solo del auto",
    "Aca el invitado recibe un arma (escribila en la nota)",
    "Cualquier otra cosa: escribila en la nota"};

bool ms_bOpen = false;
bool ms_bWasKeyDown = false;
bool ms_bPaused = false;
float ms_fSavedTimeScale = 1.0f;
unsigned ms_nStepUntil = 0;  // real ms: "avanzar" runs the game until then
bool ms_bFreeCam = false;
CVector ms_vCamPos;
float ms_fCamYaw = 0.0f;    // radians, 0 = north (+Y)
float ms_fCamPitch = 0.0f;  // radians, + = up
POINT ms_lastCursor{};
bool ms_bLooking = false;
unsigned ms_nLastTick = 0;
bool ms_bShowMarks = true;

std::deque<OpcodeEntry> ms_opcodes;
std::string ms_sFileScript;  // the file whose marks are loaded
std::vector<Mark> ms_marks;
int ms_nNewType = 0;
char ms_szNote[256] = "";
bool ms_bRecording = false;
std::vector<PathPoint> ms_recording;
unsigned ms_nLastSample = 0;
std::string ms_sStatus;

unsigned Now()
{
    return GetTickCount();
}

CRunningScript* FindMission()
{
    for (CRunningScript* script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
    {
        if (script->m_bIsMission && script->m_pBaseIP == reinterpret_cast<uint8_t*>(CTheScripts::MissionBlock))
            return script;
    }
    return nullptr;
}

std::string ScriptName(const CRunningScript* script)
{
    if (!script)
        return "ninguno";
    std::string name(script->m_szName, strnlen(script->m_szName, 8));
    for (char& c : name)
        c = (char)tolower((unsigned char)c);
    return name.empty() ? "ninguno" : name;
}

int ScriptOffset(const CRunningScript* script)
{
    return script && script->m_pCurrentIP >= script->m_pBaseIP ? (int)(script->m_pCurrentIP - script->m_pBaseIP) : -1;
}

std::string EditorDir()
{
    char exe[MAX_PATH] = "";
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string dir = exe;
    dir = dir.substr(0, dir.find_last_of("\\/")) + "\\CoopAndreas\\editor";
    CreateDirectoryA((dir.substr(0, dir.find_last_of('\\'))).c_str(), nullptr);
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir;
}

std::string FilePath(const std::string& script)
{
    return EditorDir() + "\\" + script + ".txt";
}

std::string OneLine(std::string text)
{
    for (char& c : text)
    {
        if (c == '\r' || c == '\n' || c == '|')
            c = ' ';
    }
    return text;
}

// one mark per line: key=value pairs, the note last (it may have spaces) and the path points after "puntos="
void SaveMarks()
{
    if (ms_sFileScript.empty())
        return;
    std::ofstream out(FilePath(ms_sFileScript), std::ios::trunc);
    out << "# Editor de misiones COOP San Andreas: una marca por linea. offset = posicion en el script de la mision.\n";
    char buf[256];
    for (const Mark& mark : ms_marks)
    {
        snprintf(buf, sizeof(buf), "tipo=%s script=%s offset=%d pos=%.2f,%.2f,%.2f rumbo=%.0f interior=%d modelo=%d",
            mark.type.c_str(), mark.script.c_str(), mark.offset, mark.pos.x, mark.pos.y, mark.pos.z, mark.heading,
            mark.interior, mark.model);
        out << buf;
        if (!mark.path.empty())
        {
            out << " puntos=";
            for (size_t i = 0; i < mark.path.size(); ++i)
            {
                snprintf(buf, sizeof(buf), "%s%.2f,%.2f,%.2f,%.0f", i ? "|" : "", mark.path[i].pos.x, mark.path[i].pos.y,
                    mark.path[i].pos.z, mark.path[i].heading);
                out << buf;
            }
        }
        out << " nota=" << OneLine(mark.note) << "\n";
    }
}

void LoadMarks(const std::string& script)
{
    ms_sFileScript = script;
    ms_marks.clear();
    std::ifstream in(FilePath(script));
    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty() || line[0] == '#')
            continue;
        Mark mark;
        const size_t notePos = line.find(" nota=");
        if (notePos != std::string::npos)
        {
            mark.note = line.substr(notePos + 6);
            line = line.substr(0, notePos);
        }
        std::istringstream fields(line);
        std::string field;
        while (fields >> field)
        {
            const size_t eq = field.find('=');
            if (eq == std::string::npos)
                continue;
            const std::string key = field.substr(0, eq), value = field.substr(eq + 1);
            if (key == "tipo")
                mark.type = value;
            else if (key == "script")
                mark.script = value;
            else if (key == "offset")
                mark.offset = atoi(value.c_str());
            else if (key == "pos")
                sscanf_s(value.c_str(), "%f,%f,%f", &mark.pos.x, &mark.pos.y, &mark.pos.z);
            else if (key == "rumbo")
                mark.heading = (float)atof(value.c_str());
            else if (key == "interior")
                mark.interior = atoi(value.c_str());
            else if (key == "modelo")
                mark.model = atoi(value.c_str());
            else if (key == "puntos")
            {
                std::istringstream points(value);
                std::string point;
                while (std::getline(points, point, '|'))
                {
                    PathPoint p{};
                    if (sscanf_s(point.c_str(), "%f,%f,%f,%f", &p.pos.x, &p.pos.y, &p.pos.z, &p.heading) == 4)
                        mark.path.push_back(p);
                }
            }
        }
        if (!mark.type.empty())
            ms_marks.push_back(mark);
    }
}

bool CanPause()
{
    return CNetworkPlayerManager::m_pPlayers.empty() || CTestMode::ms_bEnabled;
}

void SetPaused(bool paused)
{
    if (paused == ms_bPaused)
        return;
    ms_bPaused = paused;
    if (paused)
        ms_fSavedTimeScale = CTimer::ms_fTimeScale > 0.0f ? CTimer::ms_fTimeScale : 1.0f;
    else
        CTimer::ms_fTimeScale = ms_fSavedTimeScale;
    ms_nStepUntil = 0;
}

void UpdateControls()
{
    CPad* pad = CPad::GetPad(0);
    if (ms_bOpen || ms_bPaused || ms_bFreeCam)
        pad->DisablePlayerControls |= CONTROLS_BIT;
    else
        pad->DisablePlayerControls &= ~CONTROLS_BIT;
}

CVector CamFront()
{
    return CVector(-sinf(ms_fCamYaw) * cosf(ms_fCamPitch), cosf(ms_fCamYaw) * cosf(ms_fCamPitch), sinf(ms_fCamPitch));
}

void SetFreeCam(bool on)
{
    if (on == ms_bFreeCam)
        return;
    ms_bFreeCam = on;
    if (on)
    {
        ms_vCamPos = TheCamera.GetPosition();
        const CVector front = TheCamera.m_aCams[TheCamera.m_nActiveCam].m_vecFront;
        ms_fCamYaw = atan2f(-front.x, front.y);
        ms_fCamPitch = asinf(std::clamp(front.z, -1.0f, 1.0f));
    }
    else
    {
        TheCamera.RestoreWithJumpCut();
    }
    ms_bLooking = false;
    UpdateControls();
}

void SetOpen(bool open)
{
    ms_bOpen = open;
    if (!open)
    {
        // closed: the player walks again (a recording keeps going)
        SetFreeCam(false);
        SetPaused(false);
        // CImGui only clears its hover bit while something draws (see CCheatMenu::SetOpen)
        if (!CImGui::ms_bActive && !CCheatMenu::ms_bOpen)
            CPad::GetPad(0)->DisablePlayerControls &= ~0x200;
    }
    UpdateControls();
    const bool cursor = open || CImGui::ms_bActive || CCheatMenu::ms_bOpen;
    static_cast<IDirect3DDevice9*>(RwD3D9GetCurrentD3DDevice())->ShowCursor(cursor);
    ImGui::GetIO().MouseDrawCursor = cursor;
}

void LookAt(const CVector& target)
{
    if (!ms_bFreeCam)
        SetFreeCam(true);
    // keep the heading, stand 12 m back and 6 m up, and aim at the point
    const CVector back(sinf(ms_fCamYaw), -cosf(ms_fCamYaw), 0.0f);
    ms_vCamPos = target + back * 12.0f + CVector(0.0f, 0.0f, 6.0f);
    const CVector dir = target - ms_vCamPos;
    ms_fCamYaw = atan2f(-dir.x, dir.y);
    ms_fCamPitch = atan2f(dir.z, sqrtf(dir.x * dir.x + dir.y * dir.y));
}

bool KeyDown(int key)
{
    return (GetAsyncKeyState(key) & 0x8000) != 0;
}

void ProcessFreeCam()
{
    const unsigned now = Now();
    const float dt = std::min((now - ms_nLastTick) / 1000.0f, 0.1f);
    ms_nLastTick = now;
    if (!ms_bFreeCam)
        return;

    const bool focused = GetForegroundWindow() == RsGlobal.ps->window && !CChat::m_bInputActive &&
                         !ImGui::GetIO().WantTextInput;
    if (focused)
    {
        // right mouse held over no window: look around
        POINT cursor;
        GetCursorPos(&cursor);
        const bool rmb = KeyDown(VK_RBUTTON) && !ImGui::GetIO().WantCaptureMouse;
        if (rmb && ms_bLooking)
        {
            ms_fCamYaw -= (cursor.x - ms_lastCursor.x) * 0.005f;
            ms_fCamPitch = std::clamp(ms_fCamPitch - (cursor.y - ms_lastCursor.y) * 0.005f, -1.5f, 1.5f);
        }
        ms_bLooking = rmb;
        ms_lastCursor = cursor;

        const CVector front = CamFront();
        const CVector right(cosf(ms_fCamYaw), sinf(ms_fCamYaw), 0.0f);
        CVector move;
        if (KeyDown('W')) move += front;
        if (KeyDown('S')) move -= front;
        if (KeyDown('D')) move += right;
        if (KeyDown('A')) move -= right;
        if (KeyDown('E')) move.z += 1.0f;
        if (KeyDown('Q')) move.z -= 1.0f;
        const float speed = KeyDown(VK_SHIFT) ? 40.0f : KeyDown(VK_CONTROL) ? 3.0f : 12.0f;
        ms_vCamPos += move * (speed * dt);

        if (CPlayerPed* player = FindPlayerPed(0))
        {
            CVector fromPlayer = ms_vCamPos - player->GetPosition();
            const float dist = fromPlayer.Magnitude();
            if (dist > FREECAM_RANGE)
                ms_vCamPos = player->GetPosition() + fromPlayer * (FREECAM_RANGE / dist);
        }
    }

    CVector target = ms_vCamPos + CamFront() * 5.0f;
    CVector up(0.0f, 0.0f, 0.0f);
    TheCamera.SetCamPositionForFixedMode(&ms_vCamPos, &up);
    TheCamera.TakeControlNoEntity(&target, 2 /*JUMP_CUT*/, 1 /*script*/);
}

void ProcessRecording()
{
    if (!ms_bRecording)
        return;
    CPlayerPed* player = FindPlayerPed(0);
    if (!player)
        return;
    const unsigned now = Now();
    if (now - ms_nLastSample < RECORD_STEP_MS)
        return;
    ms_nLastSample = now;
    CEntity* entity = player->m_nPedFlags.bInVehicle && player->m_pVehicle ? (CEntity*)player->m_pVehicle : (CEntity*)player;
    ms_recording.push_back({entity->GetPosition(), entity->GetHeading() * 57.29578f});
}

Mark MakeMark(const std::string& type)
{
    Mark mark;
    mark.type = type;
    CRunningScript* mission = FindMission();
    mark.script = ScriptName(mission);
    mark.offset = ScriptOffset(mission);
    mark.note = ms_szNote;
    CPlayerPed* player = FindPlayerPed(0);
    if (player)
    {
        mark.interior = player->m_nAreaCode;
        if (player->m_nPedFlags.bInVehicle && player->m_pVehicle)
            mark.model = player->m_pVehicle->m_nModelIndex;
    }
    if (ms_bFreeCam)
    {
        // the point the camera looks at, on the ground
        mark.pos = ms_vCamPos + CamFront() * 8.0f;
        bool found = false;
        const float ground = CWorld::FindGroundZFor3DCoord(mark.pos.x, mark.pos.y, mark.pos.z + 5.0f, &found, nullptr);
        if (found)
            mark.pos.z = ground + 1.0f;
        mark.heading = ms_fCamYaw * 57.29578f;
    }
    else if (player)
    {
        CEntity* entity = player->m_nPedFlags.bInVehicle && player->m_pVehicle ? (CEntity*)player->m_pVehicle : (CEntity*)player;
        mark.pos = entity->GetPosition();
        mark.heading = entity->GetHeading() * 57.29578f;
    }
    return mark;
}

void StopRecording(bool keep)
{
    ms_bRecording = false;
    if (keep && ms_recording.size() >= 2)
    {
        Mark mark = MakeMark("recorrido");
        mark.pos = ms_recording.front().pos;
        mark.heading = ms_recording.front().heading;
        mark.path = ms_recording;
        ms_marks.push_back(mark);
        SaveMarks();
        ms_sStatus = "Recorrido guardado (" + std::to_string(ms_recording.size()) + " puntos)";
    }
    ms_recording.clear();
}

// the marks of this mission replace its lines in reglas_prueba.txt (read by CMissionRulesData with mission-editor on)
void UseMarksAsTestRules()
{
    if (ms_sFileScript.empty() || ms_sFileScript == "ninguno")
    {
        ms_sStatus = "No hay mision andando: las reglas son por mision";
        return;
    }
    SaveMarks();
    std::vector<std::string> lines;
    {
        std::ifstream in(CMissionRulesData::TestFilePath());
        std::string line;
        while (std::getline(in, line))
        {
            if (line.find(" script=" + ms_sFileScript + " ") == std::string::npos)
                lines.push_back(line);
        }
    }
    {
        std::ifstream in(FilePath(ms_sFileScript));
        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty() && line[0] != '#')
                lines.push_back(line);
        }
    }
    std::ofstream out(CMissionRulesData::TestFilePath(), std::ios::trunc);
    for (const std::string& line : lines)
        out << line << "\n";
    CMissionRulesData::Reload();
    ms_sStatus = "Marcas activas como reglas en esta PC (reglas_prueba.txt). Reintenta la mision para verlas.";
}

// live = editorivo.txt, rewritten every second for tools/editor/mision_ariane.py (Ariane shows it)
void WriteSnapshot(bool live = false)
{
    CRunningScript* mission = FindMission();
    const std::string name = ScriptName(mission);
    std::ofstream out(EditorDir() + "\\" + (live ? std::string("vivo") : name + "_estado") + ".txt", std::ios::trunc);
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[256];
    snprintf(buf, sizeof(buf), "Foto del editor %02d/%02d/%04d %02d:%02d:%02d\n", st.wDay, st.wMonth, st.wYear, st.wHour,
        st.wMinute, st.wSecond);
    out << buf;
    out << "mision=" << name << " offset=" << ScriptOffset(mission);
    if (mission)
        out << " espera_ms=" << std::max(0, (int)(mission->m_nWakeTime - (int)CTimer::m_snTimeInMilliseconds));
    out << " pausa=" << (ms_bPaused ? 1 : 0) << " jugadores_remotos=" << CNetworkPlayerManager::m_pPlayers.size() << "\n";
    if (CPlayerPed* player = FindPlayerPed(0))
    {
        const CVector p = player->GetPosition();
        snprintf(buf, sizeof(buf), "jugador pos=%.2f,%.2f,%.2f rumbo=%.0f interior=%d auto=%d\n", p.x, p.y, p.z,
            player->GetHeading() * 57.29578f, player->m_nAreaCode,
            player->m_nPedFlags.bInVehicle && player->m_pVehicle ? player->m_pVehicle->m_nModelIndex : -1);
        out << buf;
    }
    const CVector cam = TheCamera.GetPosition();
    snprintf(buf, sizeof(buf), "camara pos=%.2f,%.2f,%.2f libre=%d\n", cam.x, cam.y, cam.z, ms_bFreeCam ? 1 : 0);
    out << buf;

    out << "\n# Ultimos opcodes de scripts de mision (viejo -> nuevo): script opcode offset\n";
    for (const OpcodeEntry& e : ms_opcodes)
    {
        snprintf(buf, sizeof(buf), "%.8s %04X %d\n", e.script, e.opcode, e.offset);
        out << buf;
    }

    out << "\n# Lo que creo la mision (CTheScripts::MissionCleanUp)\n";
    const CMissionCleanup& cleanup = CTheScripts::MissionCleanUp;
    for (const tMissionCleanupEntity& e : cleanup.m_Objects)
    {
        CEntity* entity = nullptr;
        const char* kind = nullptr;
        switch (e.type)
        {
        case MISSION_CLEANUP_ENTITY_TYPE_VEHICLE: entity = CPools::GetVehicle(e.handle); kind = "auto"; break;
        case MISSION_CLEANUP_ENTITY_TYPE_PED: entity = CPools::GetPed(e.handle); kind = "personaje"; break;
        case MISSION_CLEANUP_ENTITY_TYPE_OBJECT: entity = CPools::GetObject(e.handle); kind = "objeto"; break;
        default: break;
        }
        if (!entity)
            continue;
        const CVector p = entity->GetPosition();
        snprintf(buf, sizeof(buf), "%s handle=%d modelo=%d pos=%.2f,%.2f,%.2f\n", kind, e.handle, entity->m_nModelIndex, p.x, p.y, p.z);
        out << buf;
    }

    out << "\n# Iconos y marcadores (CRadar::ms_RadarTrace en uso)\n";
    for (int i = 0; i < 175; ++i)
    {
        const tRadarTrace& t = CRadar::ms_RadarTrace[i];
        if (!t.m_bInUse)
            continue;
        snprintf(buf, sizeof(buf), "blip=%d tipo=%d sprite=%d entidad=%u pos=%.2f,%.2f,%.2f\n", i, t.m_nBlipType, t.m_nRadarSprite,
            t.m_nEntityHandle, t.m_vecPos.x, t.m_vecPos.y, t.m_vecPos.z);
        out << buf;
    }
    if (!live)
        ms_sStatus = "Foto guardada: editor\\" + name + "_estado.txt";
}

void Process()
{
    const bool keyDown = KeyDown(CMissionEditor::ms_nToggleKey);
    const bool canToggle = GetForegroundWindow() == RsGlobal.ps->window && !CChat::m_bInputActive &&
                           !FrontEndMenuManager.m_bMenuActive;
    if (keyDown && !ms_bWasKeyDown && canToggle)
        SetOpen(!ms_bOpen);
    ms_bWasKeyDown = keyDown;
    if (ms_bOpen && FrontEndMenuManager.m_bMenuActive)
        SetOpen(false);

    // the mission changed: keep the marks of the old one on disk and load the new one's
    const std::string script = ScriptName(FindMission());
    if (script != ms_sFileScript)
    {
        if (ms_bRecording)
            StopRecording(true);
        LoadMarks(script);
    }

    if (ms_bPaused)
    {
        if (!CanPause())
        {
            SetPaused(false);
            ms_sStatus = "Pausa cortada: se conecto otro jugador";
        }
        else if (ms_nStepUntil && Now() >= ms_nStepUntil)
        {
            ms_nStepUntil = 0;
        }
        if (ms_bPaused)
            CTimer::ms_fTimeScale = ms_nStepUntil ? ms_fSavedTimeScale : 0.0f;  // scripts may set it: forced every frame
    }

    ProcessFreeCam();
    ProcessRecording();

    static unsigned lastLive = 0;
    if (Now() - lastLive >= 1000)
    {
        lastLive = Now();
        WriteSnapshot(true);
    }
    UpdateControls();
}

void DrawMarksInWorld()
{
    if (!ms_bShowMarks || (ms_marks.empty() && ms_recording.empty()))
        return;
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    auto toScreen = [](const CVector& pos, ImVec2& out)
    {
        RwV3d in{pos.x, pos.y, pos.z}, screen;
        float w, h;
        if (!CSprite::CalcScreenCoors(in, &screen, &w, &h, true, true))
            return false;
        out = ImVec2(screen.x, screen.y);
        return true;
    };
    auto drawPath = [&](const std::vector<PathPoint>& path, ImU32 color)
    {
        for (size_t i = 1; i < path.size(); ++i)
        {
            ImVec2 a, b;
            if (toScreen(path[i - 1].pos, a) && toScreen(path[i].pos, b))
                draw->AddLine(a, b, color, 2.0f);
        }
    };
    for (size_t i = 0; i < ms_marks.size(); ++i)
    {
        const Mark& mark = ms_marks[i];
        if (!mark.path.empty())
            drawPath(mark.path, IM_COL32(120, 200, 255, 220));
        ImVec2 at;
        if (!toScreen(mark.pos, at))
            continue;
        draw->AddCircleFilled(at, 6.0f, IM_COL32(255, 200, 40, 230));
        const std::string label = std::to_string(i + 1) + ". " + mark.type + (mark.note.empty() ? "" : ": " + mark.note);
        draw->AddText(ImVec2(at.x + 9.0f, at.y - 8.0f), IM_COL32(255, 255, 255, 240), label.c_str());
    }
    drawPath(ms_recording, IM_COL32(255, 80, 80, 230));
}
}  // namespace

void CMissionEditor::Init()
{
    // the switch is read later (CConfigLoader::Init runs from CCustomMenuManager, after CCore::Init): checked each frame
    Events::gameProcessEvent.after += []
    {
        if (CConfigLoader::ms_bMissionEditor)
            Process();
    };
}

bool CMissionEditor::IsOpen()
{
    return ms_bOpen;
}

bool CMissionEditor::IsFreeCam()
{
    return ms_bFreeCam;
}

bool CMissionEditor::ShouldDraw()
{
    return CConfigLoader::ms_bMissionEditor && (ms_bOpen || ms_bRecording || (ms_bShowMarks && !ms_marks.empty()));
}

void CMissionEditor::BeforeCommand(CRunningScript* script, uint16_t opcode)
{
    if (!CConfigLoader::ms_bMissionEditor || !script || !script->m_bIsMission)
        return;
    OpcodeEntry entry{};
    memcpy(entry.script, script->m_szName, sizeof(entry.script));
    entry.opcode = opcode & 0x7FFF;
    // the IP is past the 2-byte opcode here
    entry.offset = script->m_pCurrentIP >= script->m_pBaseIP + 2 ? (int)(script->m_pCurrentIP - script->m_pBaseIP) - 2 : -1;
    entry.time = CTimer::m_snTimeInMilliseconds;
    // a loop that only waits repeats the same few commands: keep one copy of each in a row
    if (!ms_opcodes.empty() && ms_opcodes.back().offset == entry.offset && !memcmp(ms_opcodes.back().script, entry.script, 8))
        return;
    ms_opcodes.push_back(entry);
    if (ms_opcodes.size() > MAX_OPCODES)
        ms_opcodes.pop_front();
}

void CMissionEditor::Draw()
{
    DrawMarksInWorld();
    if (!ms_bOpen)
        return;

    ImGui::SetNextWindowPos(ImVec2(40.0f, 80.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(470.0f, 600.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Editor de misiones", nullptr, ImGuiWindowFlags_NoCollapse);

    CRunningScript* mission = FindMission();
    const std::string name = ScriptName(mission);
    ImGui::Text("Mision: %s   offset: %d", name.c_str(), ScriptOffset(mission));
    if (mission && mission->m_nWakeTime > (int)CTimer::m_snTimeInMilliseconds)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(esperando %d ms)", mission->m_nWakeTime - (int)CTimer::m_snTimeInMilliseconds);
    }
    ImGui::TextDisabled("F5 cierra. Archivo: CoopAndreas\\editor\\%s.txt", ms_sFileScript.c_str());
    if (!ms_sStatus.empty())
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.0f), "%s", ms_sStatus.c_str());
    ImGui::Separator();

    // pause
    if (CanPause())
    {
        if (ImGui::Button(ms_bPaused ? "Seguir" : "Pausar"))
            SetPaused(!ms_bPaused);
        if (ms_bPaused)
        {
            ImGui::SameLine();
            if (ImGui::Button("Avanzar 1 s"))
                ms_nStepUntil = Now() + 1000;
            ImGui::SameLine();
            if (ImGui::Button("Avanzar 5 s"))
                ms_nStepUntil = Now() + 5000;
        }
    }
    else
    {
        if (ms_bPaused)
            SetPaused(false);
        ImGui::TextDisabled("Pausa: solo jugando solo o en modo prueba");
    }

    // free camera
    bool freeCam = ms_bFreeCam;
    if (ImGui::Checkbox("Camara libre (WASD, Q/E bajar/subir, Shift rapido, Ctrl lento, boton derecho mira)", &freeCam))
        SetFreeCam(freeCam);
    ImGui::Checkbox("Mostrar marcas en el mundo", &ms_bShowMarks);
    ImGui::Separator();

    // marks
    if (ImGui::CollapsingHeader("Marcar", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SetNextItemWidth(200.0f);
        ImGui::Combo("Tipo", &ms_nNewType, MARK_TYPES, IM_ARRAYSIZE(MARK_TYPES));
        ImGui::TextWrapped("%s", MARK_HELP[ms_nNewType]);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##nota", "Nota (que tiene que pasar aca)", ms_szNote, sizeof(ms_szNote));
        if (ImGui::Button(ms_bFreeCam ? "Marcar donde mira la camara" : "Marcar donde estoy"))
        {
            ms_marks.push_back(MakeMark(MARK_TYPES[ms_nNewType]));
            SaveMarks();
            ms_szNote[0] = '\0';
            ms_sStatus = "Marca " + std::to_string(ms_marks.size()) + " guardada";
        }
        ImGui::SameLine();
        if (!ms_bRecording)
        {
            if (ImGui::Button("Grabar recorrido"))
            {
                ms_bRecording = true;
                ms_recording.clear();
                ms_nLastSample = 0;
                ms_sStatus = "Grabando: cerra el panel (F5), camina o maneja, y volve a abrirlo para parar";
            }
        }
        else
        {
            if (ImGui::Button("Parar y guardar"))
                StopRecording(true);
            ImGui::SameLine();
            if (ImGui::Button("Descartar"))
                StopRecording(false);
            ImGui::Text("Grabando: %d puntos", (int)ms_recording.size());
        }
    }

    if (ImGui::CollapsingHeader("Marcas de esta mision", ImGuiTreeNodeFlags_DefaultOpen))
    {
        int remove = -1;
        for (size_t i = 0; i < ms_marks.size(); ++i)
        {
            Mark& mark = ms_marks[i];
            ImGui::PushID((int)i);
            if (ImGui::SmallButton("Ir"))
                LookAt(mark.pos);
            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
                remove = (int)i;
            ImGui::SameLine();
            ImGui::Text("%d. %s  offset %d%s", (int)i + 1, mark.type.c_str(), mark.offset,
                mark.path.empty() ? "" : ("  (" + std::to_string(mark.path.size()) + " puntos)").c_str());
            if (!mark.note.empty())
                ImGui::TextDisabled("    %s", mark.note.c_str());
            ImGui::PopID();
        }
        if (remove >= 0)
        {
            ms_marks.erase(ms_marks.begin() + remove);
            SaveMarks();
        }
        if (ms_marks.empty())
            ImGui::TextDisabled("Todavia no hay marcas.");
        else if (ImGui::Button("Probar estas marcas como reglas (solo en esta PC)"))
            UseMarksAsTestRules();
    }

    if (ImGui::CollapsingHeader("Lo que creo la mision"))
    {
        const CMissionCleanup& cleanup = CTheScripts::MissionCleanUp;
        int shown = 0;
        for (const tMissionCleanupEntity& e : cleanup.m_Objects)
        {
            CEntity* entity = nullptr;
            const char* kind = nullptr;
            switch (e.type)
            {
            case MISSION_CLEANUP_ENTITY_TYPE_VEHICLE: entity = CPools::GetVehicle(e.handle); kind = "Auto"; break;
            case MISSION_CLEANUP_ENTITY_TYPE_PED: entity = CPools::GetPed(e.handle); kind = "Personaje"; break;
            case MISSION_CLEANUP_ENTITY_TYPE_OBJECT: entity = CPools::GetObject(e.handle); kind = "Objeto"; break;
            default: break;
            }
            if (!entity)
                continue;
            ImGui::PushID(e.handle * 16 + e.type);
            if (ImGui::SmallButton("Ir"))
                LookAt(entity->GetPosition());
            ImGui::SameLine();
            const CVector p = entity->GetPosition();
            ImGui::Text("%s modelo %d  (%.0f, %.0f, %.0f)", kind, entity->m_nModelIndex, p.x, p.y, p.z);
            ImGui::PopID();
            ++shown;
        }
        if (!shown)
            ImGui::TextDisabled("Nada (o no hay mision).");
    }

    if (ImGui::CollapsingHeader("Marcadores e iconos"))
    {
        int shown = 0;
        for (int i = 0; i < 175; ++i)
        {
            const tRadarTrace& t = CRadar::ms_RadarTrace[i];
            if (!t.m_bInUse || (t.m_nBlipType != BLIP_COORD && t.m_nBlipType != BLIP_CONTACTPOINT))
                continue;
            ImGui::PushID(i);
            if (ImGui::SmallButton("Ir"))
                LookAt(t.m_vecPos);
            ImGui::SameLine();
            ImGui::Text("%s  (%.0f, %.0f, %.0f)", t.m_nBlipType == BLIP_COORD ? "Marcador" : "Contacto", t.m_vecPos.x,
                t.m_vecPos.y, t.m_vecPos.z);
            ImGui::PopID();
            ++shown;
        }
        if (!shown)
            ImGui::TextDisabled("Ninguno.");
    }

    if (ImGui::CollapsingHeader("Ultimos opcodes"))
    {
        for (auto it = ms_opcodes.rbegin(); it != ms_opcodes.rend(); ++it)
            ImGui::Text("%.8s  %04X  offset %d", it->script, it->opcode, it->offset);
    }

    ImGui::Separator();
    if (ImGui::Button("Foto para la IA"))
        WriteSnapshot();
    ImGui::SameLine();
    ImGui::TextDisabled("guarda todo esto en editor\\%s_estado.txt", name.c_str());

    ImGui::End();
}
