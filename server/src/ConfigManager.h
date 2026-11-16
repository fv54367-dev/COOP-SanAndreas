#pragma once

#include <string>
#include <unordered_map>

#include "config.h"

class CConfigManager
{
public:
    inline static INIReader* ms_pReader = nullptr;
    inline static std::string ms_sConfigPath = "server-config.ini";

    inline static const std::unordered_map<std::string, uint16_t> ms_umDefaultConfig = {
        {"upnp", 1}, {"voice-chat", 1}, {"voice-range", 50}, {"port", Config::DEFAULT_PORT}, {"maxplayers", Config::MAX_SERVER_PLAYERS}, {"persistence", 1}, {"tp-request", 1},
        {"handover", 1}, {"host-leave-abort", 1}, {"autosave-minutes", 10}, {"save-backups", 5},
        {"coop-side-missions", 1}, {"mission-lock", 1}, {"server-log-file", 1}, {"packet-validation", 1}, {"clear-messages", 1}};

    static bool GetUpnp();
    static bool GetVoiceChat();
    static uint16_t GetVoiceRange();
    static void Init();
    static bool HasConfigExists();
    static void CreateConfig();
    static uint16_t GetConfigPort();
    static uint16_t GetConfigMaxPlayers();
    // keep each player's state (money, weapons, stats, clothes, health, position) between sessions
    static bool GetConfigPersistence();
    // shown in the launcher's server list
    static std::string GetConfigName();
    // test mode: relay the test chat commands (/traer, /caminar) between players. Default 0
    static bool GetConfigTestCommands();
    static bool GetConfigTpRequest();
    // lag simulator for outgoing packets (0 = off): fixed delay, random +/- jitter, and loss of unreliable packets
    static uint32_t GetConfigSimLatencyMs();
    static uint32_t GetConfigSimJitterMs();
    static uint32_t GetConfigSimLossPercent();
    // B1: when a player culls an ambient ped/vehicle, try to hand it to a nearby player instead of deleting it
    static bool GetConfigHandover();
    static uint32_t GetConfigHandoverPedRadius();
    static uint32_t GetConfigHandoverVehicleRadius();
    // a calm ambient ped on foot goes to the player standing next to it when its syncer is clearly farther (its
    // reactions to that player run in his own game, without the network delay). Default 1
    static bool GetConfigHandoverProximity();
    // mod-check: the cars / peds / weapons of each game against the host's. 0 off, 1 only tell, 2 a player whose
    // models differ cannot play (the rest is only told). Default 2
    static uint32_t GetConfigModCheck();
    // E: the host leaving in the middle of a coop mission aborts it for everyone (MISSION_ABORTED). Default 1
    static bool GetConfigHostLeaveAbort();
    // server-client-config: [clientes] section, switch = true/false of coopandreas.ini imposed on every player's game
    static std::vector<std::pair<std::string, bool>> GetClientOverrides();
    // E: minutes between automatic saves of all players (0 = off). Default 10
    static uint32_t GetConfigAutosaveMinutes();
    // E: how many backups (.1 ... .5) to keep of each save file before overwriting it (0 = none). Default 5
    static uint32_t GetConfigSaveBackups();
    // F: side missions can be played cooperatively. Off = today's behaviour: the blips a mission script creates are
    // treated as static world blips (the host sends them and a snapshot clears them). Default 1
    static bool GetConfigCoopSideMissions();
    static bool GetConfigMissionLock();
    // G1: also write everything the server prints to logs\server_AAAA-MM-DD.log. Off = console only, as before. Default 1
    static bool GetConfigServerLogFile();
    // G2: validate incoming packets (type/size/deserialize) and kick a player that sends too many invalid ones.
    // Off = packets are handled as before. Default 1
    static bool GetConfigPacketValidation();
    // G5: the server announces "<nick> se conectó" in the chat of everyone. Off = as before (no server announcement).
    // Default 1
    static bool GetConfigClearMessages();
};