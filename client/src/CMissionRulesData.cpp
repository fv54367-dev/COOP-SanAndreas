#include "stdafx.h"
#include "CMissionRulesData.h"
#include "CConfigLoader.h"
#include "CCoopMission.h"
#include "CMissionMirror.h"
#include "CMissionHints.h"
#include "Commands/Commands/CCommandPutNetworkPlayersInCar.h"
#include "network/packets/scripts.h"
#include "network/packets/mission_wait_notice.h"
#include <CTimer.h>
#include <fstream>
#include <sstream>
#include <vector>

namespace
{
constexpr float ZONE_RADIUS = 6.0f;   // "cuando entra aca" without a radius
constexpr float WAIT_RADIUS = 12.0f;  // the waits of the hand-written rules (feedback: 12 m in every mission)
constexpr unsigned WAIT_LIMIT_MS = 120000;
constexpr int FOUR_DOOR_GUEST_MODEL = 492;  // Greenwood, as the hand-written guest cars

struct SRule
{
    std::string type;
    std::string script;
    int offset = -1;
    CVector pos;
    float heading = 0.0f;
    int model = -1;
    float radius = 0.0f;
    int weapon = 0;
    int ammo = 0;
    std::string note;
    int line = 0;
    std::string file;
    bool fired = false;
    bool pending = false;  // its command ran: acted on after the scripts (Command<> inside a command would reuse its state)
};

std::vector<SRule> ms_rules;      // the rules of the running mission
std::string ms_sMission;          // whose rules are loaded ("" = none running)
SRule* ms_pWaiting = nullptr;     // esperar_a_todos holding the script
unsigned ms_nWaitStart = 0;
bool ms_bReloadPending = false;

std::string ModDir()
{
    char exe[MAX_PATH] = "";
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string dir = exe;
    return dir.substr(0, dir.find_last_of("\\/")) + "\\CoopAndreas";
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
    std::string name(script->m_szName, strnlen(script->m_szName, 8));
    for (char& c : name)
        c = (char)tolower((unsigned char)c);
    return name;
}

bool ParseLine(std::string line, SRule& rule)
{
    if (line.empty() || line[0] == '#')
        return false;
    const size_t notePos = line.find(" nota=");
    if (notePos != std::string::npos)
    {
        rule.note = line.substr(notePos + 6);
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
        if (key == "tipo") rule.type = value;
        else if (key == "script") rule.script = value;
        else if (key == "offset") rule.offset = atoi(value.c_str());
        else if (key == "pos") sscanf_s(value.c_str(), "%f,%f,%f", &rule.pos.x, &rule.pos.y, &rule.pos.z);
        else if (key == "rumbo") rule.heading = (float)atof(value.c_str());
        else if (key == "modelo") rule.model = atoi(value.c_str());
        else if (key == "radio") rule.radius = (float)atof(value.c_str());
        else if (key == "arma") rule.weapon = atoi(value.c_str());
        else if (key == "balas") rule.ammo = atoi(value.c_str());
    }
    for (char& c : rule.script)
        c = (char)tolower((unsigned char)c);
    return !rule.type.empty() && !rule.script.empty();
}

void LoadFile(const std::string& path, const std::string& mission)
{
    std::ifstream in(path);
    std::string line;
    int number = 0;
    while (std::getline(in, line))
    {
        ++number;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        SRule rule;
        if (!ParseLine(line, rule) || rule.script != mission)
            continue;
        rule.line = number;
        rule.file = path.substr(path.find_last_of('\\') + 1);
        ms_rules.push_back(rule);
    }
}

void Load(const std::string& mission)
{
    ms_rules.clear();
    ms_pWaiting = nullptr;
    ms_sMission = mission;
    if (mission.empty())
        return;
    LoadFile(ModDir() + "\\reglas_misiones.txt", mission);
    if (CConfigLoader::ms_bMissionEditor)
        LoadFile(CMissionRulesData::TestFilePath(), mission);
    if (!ms_rules.empty())
        CMissionMirror::LogEventf("rule-data-load mission=%s rules=%d", mission.c_str(), (int)ms_rules.size());
}

std::vector<CNetworkPlayer*> MissionGuests()
{
    std::vector<CNetworkPlayer*> guests;
    for (auto player : CNetworkPlayerManager::m_pPlayers)
    {
        if (player && player->m_pPed && !player->m_bDead && CCoopMission::ShouldIncludeNetworkPlayer(player))
            guests.push_back(player);
    }
    return guests;
}

void SpawnGuestCar(const SRule& rule, CRunningScript* mission)
{
    if (MissionGuests().empty())
        return;
    const int model = rule.model > 0 ? rule.model : FOUR_DOOR_GUEST_MODEL;
    if (model < 400 || model > 611)
        return;
    if (CStreaming::ms_aInfoForModel[model].m_nLoadState != LOADSTATE_LOADED)
    {
        CStreaming::RequestModel(model, eStreamingFlags::GAME_REQUIRED);
        CStreaming::LoadAllRequestedModels(false);
    }
    int handle = 0;
    Command<Commands::CREATE_CAR>(model, rule.pos.x, rule.pos.y, rule.pos.z, &handle);
    if (!CPools::GetVehicle(handle))
        return;
    Command<Commands::SET_CAR_HEADING>(handle, rule.heading);
    CCommandPutNetworkPlayersInCar::AddParkedCopy(handle, mission);
}

void Fire(SRule& rule, CRunningScript* mission)
{
    rule.fired = true;
    int guests = 0;
    if (rule.type == "auto_invitado")
    {
        SpawnGuestCar(rule, mission);
    }
    else if (rule.type == "invitado_baja")
    {
        for (auto guest : MissionGuests())
        {
            Packets::Scripts::MissionScriptedExit packet{};
            packet.playerid = guest->m_playerId;
            packet.opcode = 0x0633;  // TASK_LEAVE_ANY_CAR, as the hand-written exits
            GetPacketFactory().Send(packet);
            ++guests;
        }
    }
    else if (rule.type == "invitado_sube")
    {
        CPlayerPed* host = FindPlayerPed(0);
        CVehicle* car = host && host->m_nPedFlags.bInVehicle ? host->m_pVehicle : nullptr;
        int seat = 0;
        for (auto guest : MissionGuests())
        {
            if (!car || seat >= car->m_nMaxPassengers)
                break;
            CCommandPutNetworkPlayersInCar::QueueGuestPassenger(guest->m_playerId, car, seat++, mission,
                &CConfigLoader::ms_bMissionRulesData);
            ++guests;
        }
    }
    else if (rule.type == "dar_arma")
    {
        if (rule.weapon > 0 && rule.weapon <= 46)
        {
            for (auto guest : MissionGuests())
            {
                Packets::Scripts::MissionPlayerEquipment equipment{};
                equipment.playerid = guest->m_playerId;
                equipment.weaponType = static_cast<uint8_t>(rule.weapon);
                equipment.ammo = rule.ammo > 0 ? rule.ammo : 100;
                GetPacketFactory().Send(equipment);
                ++guests;
            }
        }
    }
    else if (rule.type == "esperar_a_todos")
    {
        ms_pWaiting = &rule;
        ms_nWaitStart = GetTickCount();
    }
    CMissionMirror::LogEventf("rule-data-fire type=%s mission=%s offset=%d %s:%d guests=%d", rule.type.c_str(),
        rule.script.c_str(), rule.offset, rule.file.c_str(), rule.line, guests);
}

bool CanAct()
{
    return CConfigLoader::ms_bMissionRulesData && CNetwork::m_bConnected && CCoopMission::IsScriptAuthority();
}

bool IsHandled(const SRule& rule)
{
    return rule.type == "auto_invitado" || rule.type == "invitado_baja" || rule.type == "invitado_sube" ||
           rule.type == "dar_arma" || rule.type == "esperar_a_todos";
}

void ProcessWait(CRunningScript* mission)
{
    if (!ms_pWaiting)
        return;
    const float radius = ms_pWaiting->radius > 0.0f ? ms_pWaiting->radius : WAIT_RADIUS;
    std::string missing;
    for (auto guest : MissionGuests())
    {
        CVector d = guest->m_pPed->GetPosition() - ms_pWaiting->pos;
        if (d.Magnitude() > radius)
            missing += (missing.empty() ? "" : ", ") + guest->GetName();
    }
    if (missing.empty() || GetTickCount() - ms_nWaitStart > WAIT_LIMIT_MS || !mission)
    {
        CMissionMirror::LogEventf("rule-data-wait-end mission=%s %s:%d timeout=%d", ms_sMission.c_str(),
            ms_pWaiting->file.c_str(), ms_pWaiting->line, missing.empty() ? 0 : 1);
        ms_pWaiting = nullptr;
        return;
    }
    // the script is not run while its wake time is ahead (CRunningScript::Process 0x469F00, gta-reversed)
    mission->m_nWakeTime = CTimer::m_snTimeInMilliseconds + 250;
    CMissionHints::AuthorityWait(Packets::Scripts::MissionWaitNotice::MARKER, missing);
}

void Process()
{
    if (!CConfigLoader::ms_bMissionRulesData)
        return;
    CRunningScript* mission = FindMission();
    const std::string name = mission ? ScriptName(mission) : "";
    if (name != ms_sMission || ms_bReloadPending)
    {
        ms_bReloadPending = false;
        Load(name);
    }
    if (ms_rules.empty() || !CanAct())
    {
        ms_pWaiting = nullptr;
        return;
    }

    ProcessWait(mission);

    CPlayerPed* host = FindPlayerPed(0);
    if (!host)
        return;
    for (SRule& rule : ms_rules)
    {
        if (rule.pending && !rule.fired)
        {
            rule.pending = false;
            Fire(rule, mission);
            continue;
        }
        if (rule.fired || rule.offset >= 0 || !IsHandled(rule))
            continue;
        // a guest car with no moment and no radius: as soon as the mission runs
        if (rule.type == "auto_invitado" && rule.radius <= 0.0f)
        {
            if (!MissionGuests().empty())
                Fire(rule, mission);
            continue;
        }
        const float radius = rule.radius > 0.0f ? rule.radius : ZONE_RADIUS;
        if ((host->GetPosition() - rule.pos).Magnitude() <= radius)
            Fire(rule, mission);
    }
}
}  // namespace

void CMissionRulesData::Init()
{
    Events::gameProcessEvent.after += [] { Process(); };
}

void CMissionRulesData::BeforeCommand(CRunningScript* script, uint16_t opcode)
{
    if (ms_rules.empty() || !script || !script->m_bIsMission ||
        script->m_pBaseIP != reinterpret_cast<uint8_t*>(CTheScripts::MissionBlock) || !CanAct())
        return;
    // the IP is past the 2-byte opcode here (the same offset the editor writes)
    const int offset = (int)(script->m_pCurrentIP - script->m_pBaseIP) - 2;
    for (SRule& rule : ms_rules)
    {
        if (!rule.fired && rule.offset == offset && IsHandled(rule))
            rule.pending = true;
    }
}

void CMissionRulesData::Reload()
{
    ms_bReloadPending = true;
}

std::string CMissionRulesData::TestFilePath()
{
    return ModDir() + "\\reglas_prueba.txt";
}
