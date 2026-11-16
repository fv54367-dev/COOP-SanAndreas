#include "stdafx.h"
#include "CModCheck.h"

#include <network/packets/mods.h>
#include <CModelInfo.h>
#include <CVehicleModelInfo.h>
#include <CText.h>
#include <TlHelp32.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

// the 1.0 US table has 20000 slots; a limit adjuster that moves it updates CModelInfo::ms_modelInfoPtrs
static constexpr int MODEL_SLOTS = 20000;
// cutscene / script special actors are swapped in at run time (SPECIAL01-10): never the same in two games
static constexpr int SPECIAL_PED_FIRST = 290;
static constexpr int SPECIAL_PED_LAST = 299;
// mod_HandlingManager (cHandlingDataMgr, 0xC624 bytes) and aWeaponInfo (80 x CWeaponInfo, 0x70 bytes)
static constexpr uintptr_t HANDLING_ADDR = 0xC2B9C8;
static constexpr size_t HANDLING_SIZE = 0xC624;
static constexpr uintptr_t WEAPON_INFO_ADDR = 0xC8AAB8;
static constexpr size_t WEAPON_INFO_SIZE = 80 * 0x70;
// test tools of this PC, not mods
static const char* const IGNORED_PLUGINS[] = {"sa_bridge.asi", "_nodep.asi"};

static uint32_t Fnv1a(const uint8_t* data, size_t size)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < size; i++)
        hash = (hash ^ data[i]) * 16777619u;
    return hash;
}

static std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)tolower(c); });
    return text;
}

static bool EndsWith(const std::string& text, const char* end)
{
    const size_t n = strlen(end);
    return text.size() >= n && text.compare(text.size() - n, n, end) == 0;
}

// loaded .asi / .cleo plugins and the scripts of the cleo folder
static std::set<std::string> CollectNames()
{
    std::set<std::string> names;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot != INVALID_HANDLE_VALUE)
    {
        MODULEENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        for (BOOL ok = Module32FirstW(snapshot, &entry); ok; ok = Module32NextW(snapshot, &entry))
        {
            char narrow[MAX_MODULE_NAME32 + 1]{};
            WideCharToMultiByte(CP_ACP, 0, entry.szModule, -1, narrow, sizeof(narrow), nullptr, nullptr);
            const std::string name = Lower(narrow);
            if (EndsWith(name, ".asi") || EndsWith(name, ".cleo"))
                names.insert(name);
        }
        CloseHandle(snapshot);
    }

    char exe[MAX_PATH]{};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string dir(exe);
    dir = dir.substr(0, dir.find_last_of("\\/") + 1);
    for (const char* pattern : {"cleo\\*.cs", "cleo\\*.cs3", "cleo\\*.cs4", "cleo\\*.cs5"})
    {
        WIN32_FIND_DATAA data{};
        HANDLE find = FindFirstFileA((dir + pattern).c_str(), &data);
        if (find == INVALID_HANDLE_VALUE)
            continue;
        do
        {
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                names.insert(Lower(data.cFileName));
        } while (FindNextFileA(find, &data));
        FindClose(find);
    }

    // the scripts Modloader loads from its folders (no cleo\ copy of them)
    std::vector<std::string> pending{dir + "modloader"};
    for (int depth = 0; depth < 6 && !pending.empty(); depth++)
    {
        std::vector<std::string> next;
        for (const std::string& folder : pending)
        {
            WIN32_FIND_DATAA data{};
            HANDLE find = FindFirstFileA((folder + "\\*").c_str(), &data);
            if (find == INVALID_HANDLE_VALUE)
                continue;
            do
            {
                const std::string name = data.cFileName;
                if (name.empty() || name[0] == '.')
                    continue;  // ".", ".." and the disabled mods (".data", ".profiles")
                if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    next.push_back(folder + "\\" + name);
                else
                {
                    const std::string lower = Lower(name);
                    if (EndsWith(lower, ".cs") || EndsWith(lower, ".cs3") || EndsWith(lower, ".cs4") || EndsWith(lower, ".cs5"))
                        names.insert(lower);
                }
            } while (FindNextFileA(find, &data));
            FindClose(find);
        }
        pending = std::move(next);
    }

    for (const char* ignored : IGNORED_PLUGINS)
        names.erase(ignored);
    return names;
}

void CModCheck::SendManifest()
{
    Packets::Mods::ModManifest packet{};
    auto flush = [&packet](bool last) {
        packet.last = last;
        GetPacketFactory().Send(packet);
        packet.part++;
        packet.count = 0;
    };

    for (int id = 0; id < MODEL_SLOTS; id++)
    {
        CBaseModelInfo* info = CModelInfo::ms_modelInfoPtrs[id];
        if (info == nullptr || (id >= SPECIAL_PED_FIRST && id <= SPECIAL_PED_LAST))
            continue;
        const ModelInfoType type = info->GetModelType();
        uint8_t kind = type == MODEL_INFO_VEHICLE ? 1 : type == MODEL_INFO_PED ? 2 : type == MODEL_INFO_WEAPON ? 3 : 0;
        if (kind == 0)
            continue;

        Packets::Mods::ModelEntry& entry = packet.models[packet.count++];
        entry = {};
        entry.id = (uint16_t)id;
        entry.kind = kind;
        entry.key = info->m_nKey;
        if (kind == 1)
            strncpy_s(entry.gameName, static_cast<CVehicleModelInfo*>(info)->m_szGameName, _TRUNCATE);
        if (packet.count == Packets::Mods::ModManifest::MAX_MODELS)
            flush(false);
    }

    packet.handlingHash = Fnv1a(reinterpret_cast<const uint8_t*>(HANDLING_ADDR), HANDLING_SIZE);
    packet.weaponHash = Fnv1a(reinterpret_cast<const uint8_t*>(WEAPON_INFO_ADDR), WEAPON_INFO_SIZE);
    for (const std::string& name : CollectNames())
    {
        if (packet.nameCount == Packets::Mods::ModManifest::MAX_NAMES)
            break;
        strncpy_s(packet.names[packet.nameCount++], name.c_str(), _TRUNCATE);
    }
    flush(true);
}

// a model as the player reads it: the car's name, or its kind and number
static std::string DescribeModel(const Packets::Mods::ModelEntry& entry)
{
    if (entry.kind == 1 && entry.gameName[0] != '\0')
    {
        char key[8]{};
        strncpy_s(key, entry.gameName, _TRUNCATE);
        const char* text = TheText.Get(key);
        if (text != nullptr && text[0] != '\0' && strcmp(text, key) != 0)
            return std::string(text) + " (" + std::to_string(entry.id) + ")";
        return std::string("auto ") + key + " (" + std::to_string(entry.id) + ")";
    }
    return std::string(entry.kind == 1 ? "auto" : entry.kind == 2 ? "personaje" : "arma") + " " + std::to_string(entry.id);
}

static std::string JoinNames(const char (*names)[32], int count)
{
    std::string text;
    for (int i = 0; i < count; i++)
        text += (i ? ", " : "") + std::string(names[i]);
    return text;
}

PACKET_HANDLER(ePacketType::MOD_CHECK_RESULT, Packets::Mods::ModCheckResult* pResult)
{
    pResult->hostName[sizeof(pResult->hostName) - 1] = '\0';
    const std::string host = pResult->hostName;
    if (pResult->blocked)
        CChat::AddMessage("{63c7e8}[Mods] {e8b86d}No podes jugar con %s: tus autos/personajes/armas no son los mismos que los suyos.", host.c_str());
    else
        CChat::AddMessage("{63c7e8}[Mods] {e8b86d}Tus mods no son los mismos que los de %s (podes jugar, pero puede haber cosas raras):", host.c_str());

    std::string hostOnly, playerOnly, different;
    for (int i = 0; i < pResult->count; i++)
    {
        pResult->models[i].gameName[sizeof(pResult->models[i].gameName) - 1] = '\0';
        std::string& list = pResult->status[i] == Packets::Mods::ModCheckResult::MODEL_HOST_ONLY ? hostOnly
            : pResult->status[i] == Packets::Mods::ModCheckResult::MODEL_PLAYER_ONLY ? playerOnly : different;
        list += (list.empty() ? "" : ", ") + DescribeModel(pResult->models[i]);
    }
    if (!hostOnly.empty())
        CChat::AddMessage("{d6dce8}  Te faltan (los tiene %s): %s", host.c_str(), hostOnly.c_str());
    if (!playerOnly.empty())
        CChat::AddMessage("{d6dce8}  Tenes de mas: %s", playerOnly.c_str());
    if (!different.empty())
        CChat::AddMessage("{d6dce8}  Reemplazados por otro modelo: %s", different.c_str());
    if (pResult->totalModels > pResult->count)
        CChat::AddMessage("{d6dce8}  ... y %d mas.", pResult->totalModels - pResult->count);
    if (pResult->handlingDiffers)
        CChat::AddMessage("{d6dce8}  El handling de los autos (handling.cfg) es distinto.");
    if (pResult->weaponsDiffer)
        CChat::AddMessage("{d6dce8}  Los datos de las armas (weapon.dat) son distintos.");
    if (pResult->hostOnlyCount > 0)
        CChat::AddMessage("{d6dce8}  Plugins/CLEO que tiene %s y vos no: %s", host.c_str(),
            JoinNames(pResult->hostOnly, pResult->hostOnlyCount).c_str());
    if (pResult->playerOnlyCount > 0)
        CChat::AddMessage("{d6dce8}  Plugins/CLEO que tenes vos y %s no: %s", host.c_str(),
            JoinNames(pResult->playerOnly, pResult->playerOnlyCount).c_str());
}
