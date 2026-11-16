#include "stdafx.h"
#include "CNetworkPed.h"
#include "CConfigLoader.h"

bool CUtil::CompareControllerStates(const CControllerState & state1, const CControllerState & state2) {
    return state1.LeftStickX == state2.LeftStickX &&
        state1.LeftStickY == state2.LeftStickY &&
        //state1.RightStickX == state2.RightStickX &&
        //state1.RightStickY == state2.RightStickY &&
        state1.LeftShoulder1 == state2.LeftShoulder1 &&
        state1.LeftShoulder2 == state2.LeftShoulder2 &&
        state1.RightShoulder1 == state2.RightShoulder1 &&
        state1.RightShoulder2 == state2.RightShoulder2 &&
        state1.DPadUp == state2.DPadUp &&
        state1.DPadDown == state2.DPadDown &&
        state1.DPadLeft == state2.DPadLeft &&
        state1.DPadRight == state2.DPadRight &&
        state1.Start == state2.Start &&
        state1.Select == state2.Select &&
        state1.ButtonSquare == state2.ButtonSquare &&
        state1.ButtonTriangle == state2.ButtonTriangle &&
        state1.ButtonCross == state2.ButtonCross &&
        state1.ButtonCircle == state2.ButtonCircle &&
        state1.ShockButtonL == state2.ShockButtonL &&
        state1.ShockButtonR == state2.ShockButtonR &&
        state1.m_bChatIndicated == state2.m_bChatIndicated &&
        state1.m_bPedWalk == state2.m_bPedWalk &&
        state1.m_bVehicleMouseLook == state2.m_bVehicleMouseLook &&
        state1.m_bRadioTrackSkip == state2.m_bRadioTrackSkip;
}

void CUtil::CopyControllerState(CControllerState& destination, const CControllerState& source) {
    destination.LeftStickX = source.LeftStickX;
    destination.LeftStickY = source.LeftStickY;
    destination.RightStickX = source.RightStickX;
    destination.RightStickY = source.RightStickY;

    destination.LeftShoulder1 = 0;
    destination.LeftShoulder2 = 0;

    destination.RightShoulder1 = source.RightShoulder1;
    destination.RightShoulder2 = source.RightShoulder2;

    destination.DPadUp = source.DPadUp;
    destination.DPadDown = source.DPadDown;
    destination.DPadLeft = source.DPadLeft;
    destination.DPadRight = source.DPadRight;

    destination.Start = source.Start;
    destination.Select = source.Select;

    destination.ButtonSquare = source.ButtonSquare;
    destination.ButtonTriangle = source.ButtonTriangle;
    destination.ButtonCross = source.ButtonCross;
    destination.ButtonCircle = source.ButtonCircle;

    destination.ShockButtonR = source.ShockButtonR;

    destination.m_bChatIndicated = source.m_bChatIndicated;
    destination.m_bPedWalk = source.m_bPedWalk;
    destination.m_bVehicleMouseLook = source.m_bVehicleMouseLook;
    destination.m_bRadioTrackSkip = source.m_bRadioTrackSkip;
}

bool CUtil::IsDucked(CPed* ped)
{
    CTaskSimpleDuck* task = ped->m_pIntelligence->GetTaskDuck(true);

    if (task == nullptr)
        return false;
    if (task->m_bIsFinished || task->m_bIsAborting)
        return false;

    return true;
}

bool CUtil::isDifferenceGreaterThanPercent(float value1, float value2, int percent) {
    float difference = std::abs(value1 - value2);
    float average = (std::abs(value1) + std::abs(value2)) / 2;
    return (difference / average) > (percent / 100);
}

bool CUtil::IsPositionUpdateNeeded(CVector pos, CVector update, int percent)
{
    return
        CUtil::isDifferenceGreaterThanPercent(pos.x, update.x, percent) ||
        CUtil::isDifferenceGreaterThanPercent(pos.y, update.y, percent) ||
        CUtil::isDifferenceGreaterThanPercent(pos.z, update.z, percent);
}

int CUtil::GetWeaponModelById(unsigned char id)
{
    return CWeaponInfo::GetWeaponInfo((eWeaponType)id, 1)->m_nModelId1;
}

bool CUtil::IsMeleeWeapon(unsigned char id)
{
    return CWeaponInfo::GetWeaponInfo((eWeaponType)id, 1)->m_nWeaponFire == 0;
}

// Weapon model request: with weapon-model-release the request is a plain one and ReleaseWeaponModel lets it go once the
// weapon is in the slot (CWeapon::Initialise AddRefs the model, so the streamer cannot evict it while a ped holds the
// weapon; gta-reversed Weapon.cpp / Streaming.cpp RemoveLeastUsedModel checks m_nRefCount). Off: pinned KEEP_IN_MEMORY.
static void RequestWeaponModel(int model, bool mission)
{
    unsigned int flags = eStreamingFlags::GAME_REQUIRED | eStreamingFlags::PRIORITY_REQUEST;
    if (mission) flags |= eStreamingFlags::MISSION_REQUIRED;
    if (!CConfigLoader::ms_bWeaponModelRelease) flags |= eStreamingFlags::KEEP_IN_MEMORY;
    CStreaming::RequestModel(model, flags);
    CStreaming::LoadAllRequestedModels(true);

    int count = 0;
    while (count++ < 10 && CStreaming::ms_aInfoForModel[model].m_nLoadState != LOADSTATE_LOADED)
    {
        CStreaming::LoadAllRequestedModels(count < 3);
    }
}

// Same pattern as CNetworkVehicle::CreateVehicle: drop only the flags this request added
static void ReleaseWeaponModel(int model, unsigned char oldFlags, bool mission)
{
    if (!CConfigLoader::ms_bWeaponModelRelease)
        return;
    if (!(oldFlags & eStreamingFlags::GAME_REQUIRED))
        CStreaming::SetModelIsDeletable(model);
    if (mission && !(oldFlags & eStreamingFlags::MISSION_REQUIRED))
        CStreaming::SetMissionDoesntRequireModel(model);
}

void CUtil::GiveWeaponByPacket(CNetworkPlayer* player, unsigned char weapon, unsigned short ammo, bool select)
{
    // update weapon, ammo
    auto& activeWeapon = player->m_pPed->m_aWeapons[player->m_pPed->m_nActiveWeaponSlot];
    bool isWeaponTypeDifferent = (activeWeapon.m_eWeaponType != weapon);
    bool isAmmoDifferent = (activeWeapon.m_nAmmoInClip != ammo);

    if (isWeaponTypeDifferent || isAmmoDifferent)
    {
        //CStatsSync::ApplyNetworkPlayerContext(player);

        if (weapon != 0)
        {
            if (weapon == WEAPON_SATCHEL_CHARGE)
                CUtil::GiveWeaponByPacket(player, WEAPON_DETONATOR, 1, false);

            int model = CUtil::GetWeaponModelById(weapon);

            const unsigned char oldFlags = CStreaming::ms_aInfoForModel[model].m_nFlags;
            const bool requested = CStreaming::ms_aInfoForModel[model].m_nLoadState != LOADSTATE_LOADED;
            if (requested)
                RequestWeaponModel(model, false);

            // give weapon
            bool isMeleeWeapon = CUtil::IsMeleeWeapon(weapon);

            if (activeWeapon.m_nTotalAmmo <= 0 || isWeaponTypeDifferent)
            {
                player->m_pPed->GiveWeapon((eWeaponType)weapon, isMeleeWeapon ? 1 : 9999, false);
            }
            // set ammo in clip
            activeWeapon.m_nAmmoInClip = ammo;

            if (requested)
                ReleaseWeaponModel(model, oldFlags, false);
        }

        if (isWeaponTypeDifferent && select)
        {
            player->m_pPed->SetCurrentWeapon((eWeaponType)weapon);
        }

        //CStatsSync::ApplyLocalContext();
    }
}

void CUtil::GiveWeaponByPacket(CNetworkPed* ped, unsigned char weapon, unsigned short ammo, bool select)
{
    // update weapon, ammo
    auto& activeWeapon = ped->m_pPed->m_aWeapons[ped->m_pPed->m_nActiveWeaponSlot];
    bool isWeaponTypeDifferent = (activeWeapon.m_eWeaponType != weapon);
    bool isAmmoDifferent = (activeWeapon.m_nAmmoInClip != ammo);

    if (isWeaponTypeDifferent || isAmmoDifferent)
    {
        if (isWeaponTypeDifferent)
        {
            ped->m_pPed->ClearWeapons();
        }

        if (weapon != 0)
        {
            if (weapon == WEAPON_SATCHEL_CHARGE)
                CUtil::GiveWeaponByPacket(ped, WEAPON_DETONATOR, 1, false);

            int model = CUtil::GetWeaponModelById(weapon);

            const unsigned char oldFlags = CStreaming::ms_aInfoForModel[model].m_nFlags;
            const bool requested = CStreaming::ms_aInfoForModel[model].m_nLoadState != LOADSTATE_LOADED;
            if (requested)
                RequestWeaponModel(model, true);

            // give weapon
            bool isMeleeWeapon = CUtil::IsMeleeWeapon(weapon);

            if (activeWeapon.m_nTotalAmmo <= 0)
            {
                ped->m_pPed->GiveWeapon((eWeaponType)weapon, isMeleeWeapon ? 1 : 99999, false);
            }


            // set ammo in clip
            activeWeapon.m_nAmmoInClip = ammo;

            if (requested)
                ReleaseWeaponModel(model, oldFlags, true);
        }

        if (isWeaponTypeDifferent && select)
        {
            ped->m_pPed->SetCurrentWeapon((eWeaponType)weapon);
        }
    }
}

eVehicleType CUtil::GetVehicleType(CVehicle* vehicle)
{
    return (eVehicleType)reinterpret_cast<CVehicleModelInfo*>(CModelInfo::ms_modelInfoPtrs[vehicle->m_nModelIndex])->m_nVehicleType;
}

CNetworkPed* CUtil::GetNetworkPedByTask(CTask* targetTask)
{
    for (auto networkPed : CNetworkPedManager::m_pPeds)
    {
        CPed* ped = networkPed->m_pPed;
        if (!ped) continue;

        auto findTaskInArray = [&](CTask* tasks[], int size) -> CNetworkPed* {
            for (int i = 0; i < size; i++)
            {
                CTask* task = tasks[i];
                while (task)
                {
                    if (task == targetTask)
                        return networkPed;
                    task = task->GetSubTask();
                }
            }
            return nullptr;
            };

        if (auto foundPed = findTaskInArray(ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks, 5))
            return foundPed;

        if (auto foundPed = findTaskInArray(ped->m_pIntelligence->m_TaskMgr.m_aSecondaryTasks, 5))
            return foundPed;
    }

    return nullptr;
}

bool CUtil::IsPedHasJetpack(CPed* ped)
{
    return plugin::CallMethodAndReturn<CTaskSimpleJetPack*, 0x601110, CPedIntelligence*>(ped->m_pIntelligence);
}

void CUtil::SetPlayerJetpack(CNetworkPlayer* player, bool set)
{
    if (set)
    {
        CPlayerPed* ped = player->m_pPed;

        if (ped == nullptr || player->GetInternalId() == -1)
            return;

        // the on-foot update comes every 50-100 ms: a new task each time would reset the thrust/leg animation state
        // and the exhaust fx of the remote jetpack, so only give it once
        if (IsPedHasJetpack(ped) || ped->m_nPedFlags.bInVehicle)
            return;

        // CTaskSimpleJetPack renders the jetpack model (CPed::Render) and plays the thrust anims/fx from the
        // remote ped's synced pad (CPlayerPed::ProcessControl hook); it needs the jetpack model loaded.
        // the task is given directly instead of CCheat::JetpackCheat so the cheat code paths/stats are not touched
        if (CStreaming::ms_aInfoForModel[MODEL_JETPACK].m_nLoadState != LOADSTATE_LOADED)
        {
            CStreaming::RequestModel(MODEL_JETPACK, eStreamingFlags::GAME_REQUIRED);
            CStreaming::LoadAllRequestedModels(false);
            if (CStreaming::ms_aInfoForModel[MODEL_JETPACK].m_nLoadState != LOADSTATE_LOADED)
                return;
        }

        // the real ctor (0x67B4E0) takes a 4th param, the target entity, that the plugin-sdk declaration lacks:
        // without it a stack garbage pointer gets RegisterReference'd (crash 2026-10-18)
        CTaskSimpleJetPack* task = (CTaskSimpleJetPack*)CTask::operator new(sizeof(CTaskSimpleJetPack));
        plugin::CallMethod<0x67B4E0, CTaskSimpleJetPack*, const CVector*, float, int, CEntity*>(task, nullptr, 10.0f, 0, nullptr);
        ped->m_pIntelligence->m_TaskMgr.SetTask(task, 3, false); // TASK_PRIMARY_PRIMARY
    }
    else
    {
        // CPedIntelligence::GetTaskJetPack
        CTaskSimpleJetPack* task = plugin::CallMethodAndReturn<CTaskSimpleJetPack*, 0x601110, CPedIntelligence*>(player->m_pPed->m_pIntelligence);

        if (task)
        {
            task->m_bIsFinished = true; // dont create a jetpack pickup, TODO when syncing pickups
            plugin::CallMethod<0x67B660, CTaskSimpleJetPack*>(task, player->m_pPed); // CTaskSimpleJetPack::DropJetPack
        }
    }
}

std::string CUtil::GetWeaponName(eWeaponType type)
{
    static const std::string names[] =
    {
        "Fist",
        "Brass Knuckles",
        "Golf Club",
        "Nightstick",
        "Knife",
        "Baseball Bat",
        "Shovel",
        "Pool Cue",
        "Katana",
        "Chainsaw",
        "Purple Dildo",
        "Dildo",
        "Vibrator",
        "Silver Vibrator",
        "Flowers 1",
        "Cane",
        "Grenade",
        "Tear Gas",
        "Molotov Cocktail",
        "",
        "",
        "",
        "9mm",
        "Silenced 9mm",
        "Desert Eagle",
        "Shotgun",
        "Sawnoff Shotgun",
        "Combat Shotgun",
        "Micro SMG",
        "MP5",
        "AK-47",
        "M4",
        "Tec-9",
        "Country Rifle",
        "Sniper Rifle",
        "RPG",
        "HS Rocket",
        "Flamethrower",
        "Minigun",
        "Satchel Charge",
        "Detonator",
        "Spraycan",
        "Fire Extinguisher",
        "Camera",
        "Night Vision Goggles",
        "Thermal Goggles",
        "Parachute"
    };

    return names[type];
}