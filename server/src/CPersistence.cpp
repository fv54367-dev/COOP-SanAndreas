#include "stdafx.h"
#include "CPersistence.h"

#include <type_traits>

static_assert(std::is_trivially_copyable_v<Packets::Players::SPersistData>, "SPersistData is written to disk as is");

static constexpr char PERSIST_MAGIC[4] = {'C', 'A', 'P', 'S'};
static const std::filesystem::path SAVES_DIR = "saves";

struct SPersistFileHeader
{
    char magic[4];
    uint32_t version;
    uint32_t size;
};

static std::filesystem::path GetPlayerFile(const char* name)
{
    // nicknames are already limited to [a-zA-Z0-9_[]] by PlayerConnected
    return SAVES_DIR / (std::string(name) + ".bin");
}

// <file>.1 ... <file>.5, the previous versions of a save (E: save-backups)
static std::filesystem::path GetBackupFile(const std::filesystem::path& path, uint32_t index)
{
    return std::filesystem::path(path.string() + "." + std::to_string(index));
}

// shift <file>.1 -> .2 ... -> .N before the current file is overwritten (the oldest one is dropped)
static void RotateBackups(const std::filesystem::path& path)
{
    uint32_t count = CConfigManager::GetConfigSaveBackups();
    if (count == 0)
        return;

    std::error_code ec;
    // the state is written every few seconds: only keep a new backup when the newest one is 10+ minutes old, so
    // the copies span a useful amount of time instead of the last minute
    auto newest = std::filesystem::last_write_time(GetBackupFile(path, 1), ec);
    if (!ec && std::filesystem::file_time_type::clock::now() - newest < std::chrono::minutes(10))
        return;
    ec.clear();
    std::filesystem::remove(GetBackupFile(path, count), ec);
    for (uint32_t i = count; i > 1; i--)
    {
        std::filesystem::rename(GetBackupFile(path, i - 1), GetBackupFile(path, i), ec);
    }
    std::filesystem::rename(path, GetBackupFile(path, 1), ec);
}

bool CPersistence::IsEnabled()
{
    return CConfigManager::GetConfigPersistence();
}

bool CPersistence::Load(const char* name, Packets::Players::SPersistData& data)
{
    if (!name[0])
        return false;

    std::ifstream file(GetPlayerFile(name), std::ios::binary);
    if (!file)
        return false;

    SPersistFileHeader header{};
    file.read((char*)&header, sizeof(header));
    if (!file || memcmp(header.magic, PERSIST_MAGIC, sizeof(PERSIST_MAGIC)) != 0 ||
        header.version != Packets::Players::SPersistData::PERSIST_VERSION ||
        header.size != sizeof(Packets::Players::SPersistData))
    {
        CServerLog::Printf("[Persistence] : ignoring incompatible save of %s\n", name);
        return false;
    }

    Packets::Players::SPersistData loaded{};
    file.read((char*)&loaded, sizeof(loaded));
    if (!file)
        return false;

    data = loaded;
    return true;
}

void CPersistence::Save(const char* name, const Packets::Players::SPersistData& data)
{
    if (!name[0])
        return;

    std::error_code ec;
    std::filesystem::create_directories(SAVES_DIR, ec);

    std::filesystem::path path = GetPlayerFile(name);
    std::filesystem::path tmpPath = path;
    tmpPath += ".tmp";

    {
        std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            CServerLog::Printf("[Persistence] : couldn't write %s\n", tmpPath.string().c_str());
            return;
        }

        SPersistFileHeader header{};
        memcpy(header.magic, PERSIST_MAGIC, sizeof(PERSIST_MAGIC));
        header.version = Packets::Players::SPersistData::PERSIST_VERSION;
        header.size = sizeof(Packets::Players::SPersistData);
        file.write((const char*)&header, sizeof(header));
        file.write((const char*)&data, sizeof(data));
        if (!file)
            return;
    }

    // replace the old save only once the new one is complete; the previous version is kept as a backup
    RotateBackups(path);
    std::filesystem::rename(tmpPath, path, ec);
    if (ec)
    {
        CServerLog::Printf("[Persistence] : couldn't save %s (%s)\n", name, ec.message().c_str());
    }
}
