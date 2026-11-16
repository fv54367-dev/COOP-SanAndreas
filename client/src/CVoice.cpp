// WinMM polling and bounded queues adapted from Karimov-Akbar/sa-coop Voice.cpp (MIT; NOTICE.md).
#include "stdafx.h"
#include "CVoice.h"
#include "CVoiceAdpcm.h"
#include "CMissionMode.h"
#include "CTestMode.h"
#include "UI/CModeMenu.h"
#include "imgui.h"
#include <mmsystem.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <map>
#include <deque>
#include <algorithm>
#include <cmath>

namespace
{
using Pcm = std::array<int16_t, 320>;
std::mutex mutex;
std::deque<Pcm> captured;
std::map<playerid_t, std::deque<Pcm>> incoming;
struct Heard { uint16_t sequence = 0; bool hasSequence = false; DWORD time = 0; };
std::map<playerid_t, Heard> heard;
std::thread worker;
std::atomic<bool> running{false}, active{false}, wantMic{false}, micFailed{false};
std::atomic<DWORD> lastGameTick{0};
std::atomic<HWND> gameWindow{nullptr};
bool allowed = false, warned = false;
uint32_t configuredSession = 0;
uint16_t sequence = 0;
std::atomic<float> micLevel{0.0f};
std::atomic<bool> toggledOn{false};

// waveIn device id of the name saved by the voice menu (WAVE_MAPPER = Windows default when empty or not found)
UINT ResolveMicDevice()
{
    if (!CConfigLoader::ms_szVoiceMic[0]) return WAVE_MAPPER;
    const UINT count = waveInGetNumDevs();
    for (UINT i = 0; i < count; ++i)
    {
        WAVEINCAPSA caps{};
        if (waveInGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR && strcmp(caps.szPname, CConfigLoader::ms_szVoiceMic) == 0)
            return i;
    }
    return WAVE_MAPPER;
}

// mic volume (menu percent) applied to a captured frame; also feeds the level meter of the menu
void ApplyMicVolume(Pcm& pcm)
{
    const int gain = CConfigLoader::ms_nVoiceMicVolume;
    int peak = 0;
    for (auto& sample : pcm)
    {
        if (gain != 100) sample = static_cast<int16_t>(std::clamp<int>(sample * gain / 100, -32768, 32767));
        peak = (std::max)(peak, std::abs(static_cast<int>(sample)));
    }
    micLevel = peak / 32768.0f;
}

WAVEFORMATEX Format()
{
    WAVEFORMATEX f{};
    f.wFormatTag = WAVE_FORMAT_PCM;
    f.nChannels = 1;
    f.nSamplesPerSec = 16000;
    f.wBitsPerSample = 16;
    f.nBlockAlign = 2;
    f.nAvgBytesPerSec = 32000;
    return f;
}

void AudioThread()
{
    HWAVEIN input = nullptr;
    HWAVEOUT output = nullptr;
    WAVEHDR inHeaders[4]{}, outHeaders[8]{};
    Pcm inBuffers[4]{}, outBuffers[8]{};
    bool recording = false;
    DWORD lastMix = GetTickCount();
    auto closeInput = [&]
    {
        if (input)
        {
            waveInReset(input);
            for (auto& h : inHeaders)
                if (h.dwFlags & WHDR_PREPARED) waveInUnprepareHeader(input, &h, sizeof(h));
            waveInClose(input);
            input = nullptr;
        }
        recording = false;
        std::lock_guard<std::mutex> lock(mutex);
        captured.clear();
    };
    auto closeOutput = [&]
    {
        if (output)
        {
            waveOutReset(output);
            for (auto& h : outHeaders)
                if (h.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(output, &h, sizeof(h));
            waveOutClose(output);
            output = nullptr;
        }
    };
    while (running)
    {
        // Check the physical key here too: the game may stop processing while alt-tabbed.
        bool record = active && wantMic && GetTickCount() - lastGameTick.load() < 250 &&
                      GetForegroundWindow() == gameWindow.load() &&
                      (CConfigLoader::ms_bVoiceToggle ? toggledOn.load() : (GetAsyncKeyState(VK_F4) & 0x8000) != 0) && !micFailed;
        if (!record && input) closeInput();
        if (record && !input)
        {
            auto f = Format();
            if (waveInOpen(&input, ResolveMicDevice(), &f, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
            {
                input = nullptr;
                micFailed = true;
            }
            else
            {
                bool ok = true;
                for (int i = 0; i < 4; ++i)
                {
                    inHeaders[i] = {};
                    inHeaders[i].lpData = reinterpret_cast<char*>(inBuffers[i].data());
                    inHeaders[i].dwBufferLength = sizeof(Pcm);
                    if (waveInPrepareHeader(input, &inHeaders[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR ||
                        waveInAddBuffer(input, &inHeaders[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) ok = false;
                }
                if (!ok || waveInStart(input) != MMSYSERR_NOERROR)
                {
                    micFailed = true;
                    closeInput();
                }
                else recording = true;
            }
        }
        if (recording)
        {
            for (int i = 0; i < 4; ++i)
            {
                auto& h = inHeaders[i];
                if (!(h.dwFlags & WHDR_DONE)) continue;
                if (h.dwBytesRecorded == sizeof(Pcm))
                {
                    ApplyMicVolume(inBuffers[i]);
                    std::lock_guard<std::mutex> lock(mutex);
                    if (captured.size() >= 4) captured.pop_front();
                    captured.push_back(inBuffers[i]);
                }
                if (waveInAddBuffer(input, &h, sizeof(h)) != MMSYSERR_NOERROR) micFailed = true;
            }
        }
        if (!active)
        {
            if (output) closeOutput();
            std::lock_guard<std::mutex> lock(mutex);
            incoming.clear();
        }
        else if (GetTickCount() - lastMix >= 20)
        {
            lastMix = GetTickCount();
            std::array<int, 320> mix{};
            bool any = false;
            {
                std::lock_guard<std::mutex> lock(mutex);
                for (auto it = incoming.begin(); it != incoming.end();)
                {
                    auto& queue = it->second;
                    if (!queue.empty())
                    {
                        any = true;
                        for (int i = 0; i < 320; ++i) mix[i] += queue.front()[i];
                        queue.pop_front();
                    }
                    if (queue.empty()) it = incoming.erase(it); else ++it;
                }
            }
            if (any)
            {
                if (!output)
                {
                    auto f = Format();
                    if (waveOutOpen(&output, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
                        output = nullptr;
                    if (output)
                    {
                        for (int i = 0; i < 8; ++i)
                        {
                            outHeaders[i] = {};
                            outHeaders[i].lpData = reinterpret_cast<char*>(outBuffers[i].data());
                            outHeaders[i].dwBufferLength = sizeof(Pcm);
                            waveOutPrepareHeader(output, &outHeaders[i], sizeof(WAVEHDR));
                        }
                    }
                }
                if (output)
                {
                    for (int j = 0; j < 8; ++j)
                    {
                        auto& h = outHeaders[j];
                        if (!(h.dwFlags & WHDR_PREPARED) || (h.dwFlags & WHDR_INQUEUE)) continue;
                        for (int i = 0; i < 320; ++i)
                            outBuffers[j][i] = static_cast<int16_t>(std::clamp(mix[i], -32768, 32767));
                        waveOutWrite(output, &h, sizeof(h));
                        break;
                    }
                }
            }
        }
        Sleep(5);
    }
    closeInput();
    closeOutput();
}
}

void CVoice::Configure(const Packets::Voice::Settings& settings)
{
    micFailed = false;
    warned = false;
    allowed = settings.enabled;
    configuredSession = CNetwork::m_nSessionId.load();
}

void CVoice::Process()
{
    lastGameTick = GetTickCount();
    gameWindow = RsGlobal.ps ? RsGlobal.ps->window : nullptr;
    auto* ped = FindPlayerPed();
    static int lastArea = -1;
    const int area = ped ? ped->m_nAreaCode : -1;
    if (area != lastArea)
    {
        std::lock_guard<std::mutex> lock(mutex);
        incoming.clear();
        captured.clear();
        heard.clear();
        lastArea = area;
    }
    const bool enabled = allowed && configuredSession == CNetwork::m_nSessionId.load() &&
        CNetwork::m_bAuthenticated && CConfigLoader::ms_bVoiceChat && !ms_bMuted &&
        !CMissionMode::IsIndividual() && ped && ped->m_fHealth > 0.0f &&
        CWorld::Players[0].m_nPlayerState != PLAYERSTATE_HASDIED &&
        CWorld::Players[0].m_nPlayerState != PLAYERSTATE_HASBEENARRESTED;
    active = enabled;
    const bool canTalk = enabled && !CChat::m_bInputActive && !FrontEndMenuManager.m_bMenuActive &&
        !CModeMenu::ms_bOpen && !CCheatMenu::ms_bOpen && CTestMode::IsGameWindowFocused();
    const bool keyDown = (GetAsyncKeyState(VK_F4) & 0x8000) != 0;
    static bool keyWasDown = false;
    if (CConfigLoader::ms_bVoiceToggle)
    {
        // toggle mode: F4 starts/stops talking
        if (keyDown && !keyWasDown && canTalk) toggledOn = !toggledOn;
        if (!enabled) toggledOn = false;
    }
    else toggledOn = false;
    keyWasDown = keyDown;
    wantMic = canTalk && (CConfigLoader::ms_bVoiceToggle ? toggledOn.load() : keyDown);
    if (enabled && !worker.joinable())
    {
        gameWindow = RsGlobal.ps->window;
        running = true;
        worker = std::thread(AudioThread);
    }
    if (micFailed && !warned)
    {
        warned = true;
        CChat::AddMessage("Voz: no se pudo abrir el microfono. Revisa el dispositivo y los permisos de Windows.");
    }
    std::deque<Pcm> frames;
    {
        std::lock_guard<std::mutex> lock(mutex);
        frames.swap(captured);
        if (!enabled) incoming.clear();
    }
    if (!wantMic) frames.clear();
    static coop::adpcm::State state;
    if (!wantMic) state = {};
    for (const auto& pcm : frames)
    {
        auto bytes = coop::adpcm::Encode(pcm.data(), 320, state);
        Packets::Voice::Frame packet{};
        packet.sequence = sequence++;
        memcpy(packet.data, bytes.data(), sizeof(packet.data));
        GetPacketFactory().Send(packet);
    }
    if (!enabled) heard.clear();
    for (auto it = heard.begin(); it != heard.end();)
    {
        if (!CNetworkPlayerManager::GetPlayer(it->first))
        {
            std::lock_guard<std::mutex> lock(mutex);
            incoming.erase(it->first);
            it = heard.erase(it);
        }
        else ++it;
    }
}

void CVoice::Receive(const Packets::Voice::Frame& packet)
{
    if (!active || !allowed || configuredSession != CNetwork::m_nSessionId.load() ||
        !CNetwork::m_bAuthenticated || !CConfigLoader::ms_bVoiceChat || ms_bMuted) return;
    auto* remote = CNetworkPlayerManager::GetPlayer(packet.playerid);
    auto* local = FindPlayerPed();
    if (!remote || remote->m_bIndividualMode || remote->m_bDead || !remote->m_pPed || !local ||
        remote->m_pPed->m_nAreaCode != local->m_nAreaCode) return;
    auto& entry = heard[packet.playerid];
    const uint16_t delta = static_cast<uint16_t>(packet.sequence - entry.sequence);
    if (entry.hasSequence && GetTickCount() - entry.time < 1000 && (delta == 0 || delta >= 32768)) return;
    std::vector<int16_t> pcm;
    if (!coop::adpcm::Decode(packet.data, sizeof(packet.data), pcm)) return;
    entry.sequence = packet.sequence;
    entry.hasSequence = true;
    entry.time = GetTickCount();
    Pcm frame{};
    for (int i = 0; i < 320; ++i) frame[i] = static_cast<int16_t>(std::clamp<int>(pcm[i] * packet.volume / 255 * CConfigLoader::ms_nVoiceOutVolume / 100, -32768, 32767));
    std::lock_guard<std::mutex> lock(mutex);
    auto& queue = incoming[packet.playerid];
    if (queue.size() >= 6) queue.pop_front();
    queue.push_back(frame);
}

std::vector<std::string> CVoice::GetMicDevices()
{
    std::vector<std::string> names;
    const UINT count = waveInGetNumDevs();
    for (UINT i = 0; i < count; ++i)
    {
        WAVEINCAPSA caps{};
        if (waveInGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) names.push_back(caps.szPname);
    }
    return names;
}

float CVoice::GetMicLevel()
{
    return micLevel.load();
}

bool CVoice::IsTalking()
{
    return wantMic && !micFailed;
}

bool CVoice::ShouldDraw()
{
    if (!active) return false;
    if (wantMic && !micFailed) return true;
    for (const auto& item : heard) if (GetTickCount() - item.second.time < 300) return true;
    return false;
}

void CVoice::Draw()
{
    if (!ShouldDraw()) return;
    ImGui::SetNextWindowPos(ImVec2(20, ImGui::GetIO().DisplaySize.y * 0.62f));
    ImGui::SetNextWindowBgAlpha(0.35f);
    ImGui::Begin("Voz", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
    if (wantMic && !micFailed) ImGui::TextUnformatted("Hablando: vos (F4)");
    for (const auto& item : heard)
        if (GetTickCount() - item.second.time < 300)
            if (auto* player = CNetworkPlayerManager::GetPlayer(item.first))
                ImGui::Text("Hablando: %s", player->m_Name);
    ImGui::End();
}

void CVoice::Shutdown()
{
    running = false;
    active = false;
    wantMic = false;
    if (worker.joinable()) worker.join();
    allowed = false;
    heard.clear();
}
