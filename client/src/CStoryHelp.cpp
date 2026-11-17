#include "stdafx.h"
#include "CStoryHelp.h"
#include "CCoopMission.h"
#include "CMissionMode.h"
#include "CMissionMirror.h"
#include "CMissionHints.h"
#include "CNetworkStaticBlip.h"
#include "COpCodeSync.h"
#include "CConfigLoader.h"
#include "UI/CChat.h"
#include "UI/CCheatMenu.h"
#include "UI/CModeMenu.h"
#include "network/packets/story_help.h"

#include "imgui.h"

#include <algorithm>

namespace
{
// the contact points (map icons) of this game before the authority's story mission started
std::vector<Packets::Blips::_StaticBlipPayload> s_aContactsBefore;

bool IsMenuOrChatOpen()
{
    return FrontEndMenuManager.m_bMenuActive || CCheatMenu::ms_bOpen || CModeMenu::ms_bOpen || CChat::m_bInputActive;
}

bool IsLocalPlayerDeadOrArrested()
{
    return CWorld::Players[0].m_nPlayerState == PLAYERSTATE_HASDIED ||
           CWorld::Players[0].m_nPlayerState == PLAYERSTATE_HASBEENARRESTED;
}

bool SameContact(const Packets::Blips::_StaticBlipPayload& a, const Packets::Blips::_StaticBlipPayload& b)
{
    return a.sprite == b.sprite && fabsf(a.position.x - b.position.x) < 3.0f && fabsf(a.position.y - b.position.y) < 3.0f;
}

CNetworkPlayer* FindHost()
{
    for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
        if (networkPlayer && networkPlayer->m_bIsHost)
            return networkPlayer;
    return nullptr;
}
}  // namespace

bool CStoryHelp::Enabled()
{
    return CConfigLoader::ms_bStoryHelpOptional && CConfigLoader::ms_bVanillaScripts && CNetwork::m_bAuthenticated;
}

void CStoryHelp::SendTo(playerid_t target, uint8_t kind, const char* szMission)
{
    Packets::Scripts::StoryHelp packet{};
    packet.target = target;
    packet.kind = kind;
    if (szMission)
        strncpy_s(packet.mission, sizeof(packet.mission), szMission, _TRUNCATE);
    GetPacketFactory().Send(packet);
}

CStoryHelp::eRole CStoryHelp::GetRole(const CNetworkPlayer* pNetworkPlayer, int chain)
{
    if (!Enabled() || pNetworkPlayer == nullptr || chain < 0 || chain >= CMissionMode::STORY_CHAIN_COUNT)
        return ROLE_PENDING;

    // the authority's own counter against the one the player tells (his own, not the copy of the host's)
    const int mine = CMissionMode::OwnChain(chain);
    const uint8_t theirs = pNetworkPlayer->m_anChainProgress[chain];
    if (mine < 0 || mine >= CMissionMode::CHAIN_UNKNOWN || theirs == CMissionMode::CHAIN_UNKNOWN || theirs == mine)
        return ROLE_PENDING;

    for (const auto& offer : ms_vOffers)
        if (offer.id == pNetworkPlayer->m_playerId)
            return static_cast<eRole>(offer.eState);
    return ROLE_UNOFFERED;
}

void CStoryHelp::GateTouch()
{
    ms_nGateTick = GetTickCount();
}

bool CStoryHelp::AskNotPending(int chain, std::string& asking)
{
    const uint32_t now = GetTickCount();
    ms_nGateTick = now;
    const std::string mission = CMissionMode::GetChainStateName(chain, CMissionMode::OwnChain(chain));

    for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (networkPlayer == nullptr || networkPlayer->m_pPed == nullptr || !CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer))
            continue;

        eRole role = GetRole(networkPlayer, chain);
        if (role == ROLE_UNOFFERED)
        {
            SOffer offer;
            offer.id = networkPlayer->m_playerId;
            offer.eState = ROLE_ASKING;
            offer.nTick = now;
            ms_vOffers.push_back(offer);
            SendTo(networkPlayer->m_playerId, Packets::Scripts::StoryHelp::OFFER, mission.c_str());
            CMissionMirror::LogEventf("story-help offer to=%s chain=%s mission=%s", networkPlayer->GetName().c_str(),
                CMissionMode::GetChainName(chain), mission.c_str());
            role = ROLE_ASKING;
        }
        else if (role == ROLE_ASKING)
        {
            for (auto& offer : ms_vOffers)
            {
                if (offer.id == networkPlayer->m_playerId && now - offer.nTick > OFFER_WAIT_MS)
                {
                    // no answer: he plays on his own, as if he had said no
                    offer.eState = ROLE_DECLINED;
                    role = ROLE_DECLINED;
                    CMissionMirror::LogEventf("story-help timeout from=%s", networkPlayer->GetName().c_str());
                }
            }
        }

        if (role == ROLE_ASKING)
        {
            if (!asking.empty())
                asking += ", ";
            asking += networkPlayer->GetName();
        }
    }
    return !asking.empty();
}

void CStoryHelp::CancelOffers()
{
    for (const auto& offer : ms_vOffers)
        SendTo(offer.id, Packets::Scripts::StoryHelp::END, nullptr);
    if (!ms_vOffers.empty())
        CMissionMirror::LogEventf("story-help cancel offers=%d", (int)ms_vOffers.size());
    ms_vOffers.clear();
    ms_bMissionLaunched = false;
}

void CStoryHelp::OnMissionLaunched()
{
    if (!CNetwork::m_bAuthenticated)
        return;

    // the end of the previous mission's contacts is still waiting: send it before the new snapshot
    if (ms_bContactsPending)
        SendContactDelta();

    ms_bMissionLaunched = !ms_vOffers.empty();
    s_aContactsBefore.clear();
    CNetworkStaticBlip::CollectContacts(s_aContactsBefore);
    ms_bContactsBefore = true;
}

void CStoryHelp::OnMissionEnded(bool bPassed)
{
    // the players it offered the mission to: it is over (after the progress packet: same ordered channel)
    if (!ms_vOffers.empty())
    {
        CancelOffers();
    }

    // the launcher makes the next contact a moment after the mission script ends: the second snapshot waits a bit
    if (ms_bContactsBefore)
    {
        ms_bContactsBefore = false;
        if (bPassed)
        {
            ms_bContactsPending = true;
            ms_nContactsDeadline = GetTickCount() + CONTACT_SETTLE_MS;
        }
    }
}

void CStoryHelp::SendContactDelta()
{
    ms_bContactsPending = false;
    if (!CNetwork::m_bAuthenticated)
        return;

    std::vector<Packets::Blips::_StaticBlipPayload> after;
    CNetworkStaticBlip::CollectContacts(after);

    Packets::Scripts::StoryContacts packet{};
    for (const auto& before : s_aContactsBefore)
    {
        const bool kept = std::any_of(after.begin(), after.end(), [&](const auto& blip) { return SameContact(blip, before); });
        if (!kept && packet.countRemoved < Packets::Scripts::StoryContacts::MAX_CONTACTS)
            packet.removed[packet.countRemoved++] = before;
    }
    for (const auto& blip : after)
    {
        const bool had = std::any_of(s_aContactsBefore.begin(), s_aContactsBefore.end(),
            [&](const auto& before) { return SameContact(blip, before); });
        if (!had && packet.countAdded < Packets::Scripts::StoryContacts::MAX_CONTACTS)
            packet.added[packet.countAdded++] = blip;
    }
    s_aContactsBefore.clear();

    if (packet.countRemoved == 0 && packet.countAdded == 0)
        return;
    CMissionMirror::LogEventf("story-contacts send removed=%d added=%d", (int)packet.countRemoved, (int)packet.countAdded);
    GetPacketFactory().Send(packet);
}

bool CStoryHelp::GuestNotPending(int chain)
{
    if (!Enabled() || chain < 0 || chain >= CMissionMode::STORY_CHAIN_COUNT)
        return false;

    CNetworkPlayer* host = FindHost();
    if (host == nullptr)
        return false;

    const int mine = CMissionMode::OwnChain(chain);
    const uint8_t theirs = host->m_anChainProgress[chain];
    if (mine < 0 || mine >= CMissionMode::CHAIN_UNKNOWN || theirs == CMissionMode::CHAIN_UNKNOWN || theirs == mine)
        return false;

    // he is being asked, or already answered: the mission goes its way without more notices
    if (ms_bOfferOpen || ms_bNotPending)
        return true;

    static uint32_t nLastHint = 0;
    const uint32_t now = GetTickCount();
    if (now - nLastHint > 5000)
    {
        nLastHint = now;
        const std::string mission = CMissionMode::GetChainStateName(chain, theirs);
        CMissionHints::Help(host->GetName() + " empieza " + mission + ". " +
            (mine > theirs ? "Ya la hiciste" : "Todavia no llegaste") + ": espera su invitacion para ayudar.");
        CMissionMirror::LogEventf("story-help not-pending chain=%s mine=%d host=%d", CMissionMode::GetChainName(chain), mine, (int)theirs);
    }
    return true;
}

void CStoryHelp::OnPacket(const Packets::Scripts::StoryHelp& packet)
{
    using P = Packets::Scripts::StoryHelp;
    switch (packet.kind)
    {
    case P::OFFER:
    {
        if (!Enabled() || CCoopMission::IsScriptAuthority())
            return;

        ms_authorityId = packet.senderPlayerid.value;
        strncpy_s(ms_szMission, sizeof(ms_szMission), packet.mission, _TRUNCATE);
        ms_nOfferTick = GetTickCount();
        ms_nOfferDeadline = ms_nOfferTick + OFFER_TIMEOUT_MS;
        ms_bOfferOpen = true;
        CMissionMirror::LogEventf("story-help offered mission=%s", ms_szMission);

        // cannot answer: no, right away (he already plays alone, runs a mission of his own or cannot press a key)
        if (CMissionMode::IsIndividual() || COpCodeSync::IsLocalMissionRunning() || CCutsceneMgr::ms_running ||
            IsLocalPlayerDeadOrArrested())
            ReplyToOffer(false);
        break;
    }
    case P::ACCEPT:
    case P::DECLINE:
    {
        if (!CCoopMission::IsScriptAuthority())
            return;
        for (auto& offer : ms_vOffers)
        {
            if (offer.id == packet.senderPlayerid.value && offer.eState == ROLE_ASKING)
            {
                offer.eState = packet.kind == P::ACCEPT ? ROLE_HELPER : ROLE_DECLINED;
                CMissionMirror::LogEventf("story-help answer accept=%d", packet.kind == P::ACCEPT ? 1 : 0);
            }
        }
        break;
    }
    case P::END:
        if (packet.senderPlayerid.value == ms_authorityId)
            LeaveHelp();
        break;
    default:
        break;
    }
}

void CStoryHelp::ReplyToOffer(bool bAccept)
{
    ms_bOfferOpen = false;
    ms_bNotPending = true;

    CNetworkPlayer* authority = CNetworkPlayerManager::GetPlayer(ms_authorityId);
    const std::string name = authority ? authority->GetName() : "el anfitrion";
    if (!bAccept)
    {
        // out of this mission: individual mode, the F1 path; the authority sees it before the answer (same ordered channel)
        if (!CMissionMode::IsIndividual())
        {
            CMissionMode::SetIndividual(true, true);
            ms_bAutoIndividual = CMissionMode::IsIndividual();
        }
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Seguis libre en el mundo: " + name + " juega " + ms_szMission + " sin vos.");
    }
    else
    {
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Ayudas a " + name + " en " + ms_szMission + ": no se guarda progreso para vos.");
    }

    SendTo(ms_authorityId, bAccept ? Packets::Scripts::StoryHelp::ACCEPT : Packets::Scripts::StoryHelp::DECLINE, nullptr);
    CMissionMirror::LogEventf("story-help reply accept=%d individual=%d", bAccept ? 1 : 0, CMissionMode::IsIndividual() ? 1 : 0);
}

void CStoryHelp::LeaveHelp()
{
    const bool bWas = ms_bOfferOpen || ms_bNotPending;
    if (bWas)
        ms_nIgnoreContactsUntil = GetTickCount() + CONTACT_SETTLE_MS + 6000;
    ms_bOfferOpen = false;
    ms_bNotPending = false;
    if (ms_bAutoIndividual)
    {
        ms_bAutoIndividual = false;
        if (CMissionMode::IsIndividual())
        {
            CMissionMode::SetIndividual(false, true);
            CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Termino la mision: volves a jugar en modo coop con los demas.");
        }
    }
    if (bWas)
        CMissionMirror::LogEventf("story-help leave");
}

void CStoryHelp::OnContacts(const Packets::Scripts::StoryContacts& packet)
{
    // only the players that had the mission pending change their contacts: a helper or one that declined keeps his own
    if (!CConfigLoader::ms_bVanillaScripts || CCoopMission::IsScriptAuthority() || ms_bNotPending || ms_bOfferOpen ||
        CMissionMode::IsIndividual() || COpCodeSync::IsLocalMissionRunning() ||
        (int32_t)(GetTickCount() - ms_nIgnoreContactsUntil) < 0)
        return;

    CNetworkStaticBlip::ApplyContactDelta(packet.removed, packet.countRemoved, packet.added, packet.countAdded);
}

void CStoryHelp::Process()
{
    const uint32_t now = GetTickCount();

    // authority: he left the marker without starting: the players he offered it to are free again
    if (!ms_vOffers.empty() && !ms_bMissionLaunched && now - ms_nGateTick > 10000)
        CancelOffers();

    if (ms_bContactsPending && (int32_t)(now - ms_nContactsDeadline) >= 0)
        SendContactDelta();

    if (ms_bOfferOpen)
    {
        if (!Enabled() || (int32_t)(now - ms_nOfferDeadline) >= 0)
            ReplyToOffer(false);
        else if (!IsMenuOrChatOpen())
        {
            if (CPad::GetPad(0)->ConversationYesJustDown())
                ReplyToOffer(true);
            else if (CPad::GetPad(0)->ConversationNoJustDown())
                ReplyToOffer(false);
        }
    }

    // the authority left (or his mission never ends): he plays with the others again
    if ((ms_bOfferOpen || ms_bNotPending || ms_bAutoIndividual) &&
        (CNetworkPlayerManager::GetPlayer(ms_authorityId) == nullptr || now - ms_nOfferTick > 15 * 60 * 1000))
        LeaveHelp();
}

void CStoryHelp::Draw()
{
    if (!ms_bOfferOpen)
        return;

    const float scale = (std::max)(1.0f, ImGui::GetIO().DisplaySize.y / 720.0f) * 1.1f;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoInputs;
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, 80.0f * scale), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::Begin("Ayudar en la mision", nullptr, flags);
    ImGui::SetWindowFontScale(scale);

    CNetworkPlayer* authority = CNetworkPlayerManager::GetPlayer(ms_authorityId);
    ImGui::TextColored(ImVec4(0.79f, 0.65f, 1.0f, 1.0f), "Ayudar a %s en %s?", authority ? authority->GetName().c_str() : "?",
        ms_szMission);
    ImGui::TextUnformatted("Y (Si) / N (No). Si no contestas seguis libre.");

    ImGui::End();
}
