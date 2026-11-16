#include "ConfigManager.h"

bool CConfigManager::GetUpnp() { return ms_pReader->GetBoolean("", "upnp", true); }

void CConfigManager::Init()
{
	if (!HasConfigExists())
	{
		CreateConfig();
	}

	ms_pReader = new INIReader(ms_sConfigPath);
}

bool CConfigManager::HasConfigExists()
{
	std::ifstream file(ms_sConfigPath);
	return file.good();
}

void CConfigManager::CreateConfig()
{
	std::ofstream file(ms_sConfigPath);

	if (!file)
	{
		CServerLog::Printf("[ERROR]: Couldn't create a server config file!\n");
		return;
	}

	for (const auto& [key, value] : ms_umDefaultConfig)
	{
		file << key << " = " << value << "\n";
	}
	file << "name = Servidor COOP San Andreas\n";
	file << "test-commands = 0\n";
	file << "sim-latency-ms = 0\n";
	file << "sim-jitter-ms = 0\n";
	file << "sim-loss-percent = 0\n";
	file << "handover = 1\n";
	file << "handover-ped-radius = 100\n";
	file << "handover-vehicle-radius = 200\n";
	file << "handover-proximity = 0\n";
	file << "mod-check = 2\n";
	file << "host-leave-abort = 1\n";
	file << "autosave-minutes = 10\n";
	file << "save-backups = 5\n";
	file << "coop-side-missions = 1\n";
	file << "server-log-file = 1\n";
	file << "packet-validation = 1\n";
	file << "clear-messages = 1\n";
	file << "\n[clientes]\n";
	file << "; interruptores de coopandreas.ini que este server impone a todos los jugadores, ej.: lowrider-keep-car = false\n";
}

uint16_t CConfigManager::GetConfigPort()
{
	return (uint16_t)ms_pReader->GetUnsigned("", "port", ms_umDefaultConfig.at("port"));
}

std::string CConfigManager::GetConfigName()
{
	return ms_pReader->Get("", "name", "Servidor COOP San Andreas");
}

bool CConfigManager::GetConfigPersistence()
{
	return ms_pReader->GetBoolean("", "persistence", ms_umDefaultConfig.at("persistence") != 0);
}

uint16_t CConfigManager::GetConfigMaxPlayers()
{
	return (uint16_t)ms_pReader->GetUnsigned("", "maxplayers", ms_umDefaultConfig.at("maxplayers"));
}

bool CConfigManager::GetConfigTestCommands()
{
	return ms_pReader->GetBoolean("", "test-commands", false);
}

uint32_t CConfigManager::GetConfigSimLatencyMs()
{
	return (uint32_t)std::min<unsigned long>(ms_pReader->GetUnsigned("", "sim-latency-ms", 0), 5000);
}

uint32_t CConfigManager::GetConfigSimJitterMs()
{
	return (uint32_t)std::min<unsigned long>(ms_pReader->GetUnsigned("", "sim-jitter-ms", 0), 2000);
}

uint32_t CConfigManager::GetConfigSimLossPercent()
{
	return (uint32_t)std::min<unsigned long>(ms_pReader->GetUnsigned("", "sim-loss-percent", 0), 100);
}

bool CConfigManager::GetConfigHandover()
{
	return ms_pReader->GetBoolean("", "handover", ms_umDefaultConfig.at("handover") != 0);
}

uint32_t CConfigManager::GetConfigHandoverPedRadius()
{
	return (uint32_t)std::min<unsigned long>(ms_pReader->GetUnsigned("", "handover-ped-radius", 100), 1000);
}

uint32_t CConfigManager::GetConfigHandoverVehicleRadius()
{
	return (uint32_t)std::min<unsigned long>(ms_pReader->GetUnsigned("", "handover-vehicle-radius", 200), 2000);
}

bool CConfigManager::GetConfigHandoverProximity()
{
    // off by default since 2026-11-15: suspected of the NPC/car jumps of High Stakes (testeo 15-11)
    return ms_pReader->GetBoolean("", "handover-proximity", false);
}

uint32_t CConfigManager::GetConfigModCheck()
{
    return (uint32_t)std::min<unsigned long>(ms_pReader->GetUnsigned("", "mod-check", 2), 2);
}

bool CConfigManager::GetConfigHostLeaveAbort()
{
	return ms_pReader->GetBoolean("", "host-leave-abort", ms_umDefaultConfig.at("host-leave-abort") != 0);
}

uint32_t CConfigManager::GetConfigAutosaveMinutes()
{
	return (uint32_t)std::min<unsigned long>(ms_pReader->GetUnsigned("", "autosave-minutes", 10), 1440);
}

uint32_t CConfigManager::GetConfigSaveBackups()
{
	return (uint32_t)std::min<unsigned long>(ms_pReader->GetUnsigned("", "save-backups", 5), 5);
}

std::vector<std::pair<std::string, bool>> CConfigManager::GetClientOverrides()
{
    std::vector<std::pair<std::string, bool>> overrides;
    for (const std::string& key : ms_pReader->Keys("clientes"))
        overrides.emplace_back(key, ms_pReader->GetBoolean("clientes", key, false));
    return overrides;
}

bool CConfigManager::GetConfigCoopSideMissions()
{
	return ms_pReader->GetBoolean("", "coop-side-missions", ms_umDefaultConfig.at("coop-side-missions") != 0);
}

bool CConfigManager::GetConfigMissionLock()
{
	return ms_pReader->GetBoolean("", "mission-lock", ms_umDefaultConfig.at("mission-lock") != 0);
}

bool CConfigManager::GetConfigServerLogFile()
{
	return ms_pReader->GetBoolean("", "server-log-file", ms_umDefaultConfig.at("server-log-file") != 0);
}

bool CConfigManager::GetConfigPacketValidation()
{
	return ms_pReader->GetBoolean("", "packet-validation", ms_umDefaultConfig.at("packet-validation") != 0);
}

bool CConfigManager::GetConfigClearMessages()
{
	return ms_pReader->GetBoolean("", "clear-messages", ms_umDefaultConfig.at("clear-messages") != 0);
}

bool CConfigManager::GetConfigTpRequest() {
    return ms_pReader->GetBoolean("", "tp-request", true);
}
bool CConfigManager::GetVoiceChat()
{
    return ms_pReader->GetBoolean("", "voice-chat", true);
}
uint16_t CConfigManager::GetVoiceRange()
{
    return static_cast<uint16_t>(std::clamp<unsigned long>(ms_pReader->GetUnsigned("", "voice-range", 50), 1, 500));
}
