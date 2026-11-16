#include "stdafx.h"
#include "CModeMenu.h"
#include "CMissionMode.h"
#include "CConfigLoader.h"
#include "CVoice.h"
#include "imgui.h"
#include "Debug/CImGui.h"

static bool ms_bWasKeyDown = false;

void CModeMenu::SetOpen(bool open)
{
    ms_bOpen = open;

    if (open)
        CPad::GetPad(0)->DisablePlayerControls |= 0x400;
    else
    {
        CPad::GetPad(0)->DisablePlayerControls &= ~0x400;
        // see CCheatMenu::SetOpen: the hover bit of the ImGui windows must not stay set once nothing draws any more
        if (!CImGui::ms_bActive && !CCheatMenu::ms_bOpen)
            CPad::GetPad(0)->DisablePlayerControls &= ~0x200;
    }

    const bool bCursor = open || CImGui::ms_bActive || CCheatMenu::ms_bOpen;
    static_cast<IDirect3DDevice9*>(RwD3D9GetCurrentD3DDevice())->ShowCursor(bCursor);
    ImGui::GetIO().MouseDrawCursor = bCursor;
}

void CModeMenu::Init()
{
    Events::gameProcessEvent.after += []
    {
        const bool keyDown = (GetAsyncKeyState(VK_F1) & 0x8000) != 0;
        const bool canToggle = GetForegroundWindow() == RsGlobal.ps->window && !CChat::m_bInputActive &&
                               !FrontEndMenuManager.m_bMenuActive && !CCheatMenu::ms_bOpen;

        if (keyDown && !ms_bWasKeyDown && canToggle)
            SetOpen(!ms_bOpen);
        ms_bWasKeyDown = keyDown;

        if (ms_bOpen && FrontEndMenuManager.m_bMenuActive)
            SetOpen(false);
    };
}

void CModeMenu::Draw()
{
    ImGui::SetNextWindowPos(ImVec2(40.0f, 120.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Modo de misiones", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

    ImGui::TextUnformatted("Como queres jugar las misiones. F1 cierra el menu.");
    ImGui::Separator();

    bool bIndividual = CMissionMode::IsIndividual();
    if (ImGui::RadioButton("Cooperativo (con los demas jugadores en coop)", !bIndividual))
        CMissionMode::SetIndividual(false);
    if (ImGui::RadioButton("Individual (solo vos, sin esperar a nadie)", bIndividual))
        CMissionMode::SetIndividual(true);

    ImGui::Separator();
    bool bGpsRoute = CConfigLoader::ms_bGpsRoute;
    if (ImGui::Checkbox("GPS: ruta por calles hasta tu punto del mapa", &bGpsRoute) &&
        bGpsRoute != CConfigLoader::ms_bGpsRoute)
    {
        CConfigLoader::ms_bGpsRoute = bGpsRoute;
        CConfigLoader::Save();
    }

    ImGui::Separator();
    if (ImGui::Checkbox("Saltar tutorial de muerte y arresto", &CConfigLoader::ms_bSkipDeathArrestHelp))
        CConfigLoader::Save();

    ImGui::Separator();
    int cutsceneCjs = CConfigLoader::ms_bCutsceneShowOthers ? 1 : 0;
    ImGui::TextUnformatted("Cinematicas (solo en tu pantalla):");
    if (ImGui::RadioButton("Un solo CJ (solo vos)", cutsceneCjs == 0)) { CConfigLoader::ms_bCutsceneShowOthers = false; CConfigLoader::Save(); }
    if (ImGui::RadioButton("Varios CJ (vos y los demas jugadores)", cutsceneCjs == 1)) { CConfigLoader::ms_bCutsceneShowOthers = true; CConfigLoader::Save(); }

    ImGui::Separator();
    ImGui::TextUnformatted("Cooperativo: para iniciar hace falta que los demas jugadores en coop esten en el marcador.");
    ImGui::TextUnformatted("Individual: inicias vos solo; los demas no se enteran.");

    ImGui::Separator();
    ImGui::TextUnformatted("Voz por cercania: mantene F4 para hablar (solo cooperativo).");
    ImGui::Checkbox("Silenciar a todos (tambien cierra tu microfono)", &CVoice::ms_bMuted);

    if (ImGui::CollapsingHeader("Configurar chat de voz"))
    {
        bool bChanged = false;

        // input device: saved by name, "" = Windows default
        const std::vector<std::string> mics = CVoice::GetMicDevices();
        const char* szCurrent = CConfigLoader::ms_szVoiceMic[0] ? CConfigLoader::ms_szVoiceMic : "Predeterminado de Windows";
        if (ImGui::BeginCombo("Microfono", szCurrent))
        {
            if (ImGui::Selectable("Predeterminado de Windows", !CConfigLoader::ms_szVoiceMic[0]))
            {
                CConfigLoader::ms_szVoiceMic[0] = '\0';
                bChanged = true;
            }
            for (const std::string& mic : mics)
            {
                if (ImGui::Selectable(mic.c_str(), mic == CConfigLoader::ms_szVoiceMic))
                {
                    strncpy_s(CConfigLoader::ms_szVoiceMic, mic.c_str(), _TRUNCATE);
                    bChanged = true;
                }
            }
            ImGui::EndCombo();
        }
        if (mics.empty())
            ImGui::TextUnformatted("No se encontro ningun microfono.");

        bChanged |= ImGui::SliderInt("Volumen del microfono", &CConfigLoader::ms_nVoiceMicVolume, 0, 200, "%d%%");
        bChanged |= ImGui::SliderInt("Volumen de las voces", &CConfigLoader::ms_nVoiceOutVolume, 0, 200, "%d%%");

        int mode = CConfigLoader::ms_bVoiceToggle ? 1 : 0;
        if (ImGui::RadioButton("Mantener F4 para hablar", mode == 0)) { CConfigLoader::ms_bVoiceToggle = false; bChanged = true; }
        if (ImGui::RadioButton("F4 enciende y apaga el microfono", mode == 1)) { CConfigLoader::ms_bVoiceToggle = true; bChanged = true; }

        ImGui::ProgressBar(CVoice::IsTalking() ? CVoice::GetMicLevel() : 0.0f, ImVec2(-1.0f, 0.0f),
                           CVoice::IsTalking() ? "Nivel del microfono" : "Nivel (habla con F4 para probar)");
        ImGui::TextUnformatted("El microfono nuevo se usa la proxima vez que hables.");

        if (bChanged)
            CConfigLoader::Save();
    }

    ImGui::End();
}
