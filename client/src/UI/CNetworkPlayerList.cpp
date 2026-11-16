#include "stdafx.h"
#include "CNetworkPlayerList.h"
#include "CHudColours.h"
#include "CoopSaLogo.h"

void CNetworkPlayerList::DrawBox(float fX, float fY)
{
    CRect rect = CRect(CUtil::HUD_X(fX), CUtil::HUD_Y(fY), CUtil::HUD_X(fX + BOX_WIDTH), CUtil::HUD_Y(fY + BOX_HEIGHT));

    CSprite2d::DrawRect(rect, CRGBA(0, 0, 0, 190));

    CFont::SetColor(CRGBA(225, 225, 225, 255));

    CFont::SetEdge(2);
    CFont::SetOrientation(eFontAlignment::ALIGN_LEFT);
    CFont::SetFontStyle(0);

    CFont::SetScaleForCurrentlanguage(CUtil::HUD_X(0.6f), CUtil::HUD_Y(0.9f));

    CFont::PrintString(rect.left + CUtil::HUD_X(8.0f), fmin(rect.bottom, rect.top) - CUtil::HUD_Y(10.0f), "Players");
}

void CNetworkPlayerList::DrawPing(CNetworkPlayer* pNetworkPlayer, float fX, float fY)
{
    uint32_t nRTT = pNetworkPlayer == nullptr ? CNetwork::GetRTT() : pNetworkPlayer->m_nRTT;
    uint32_t nPingStripesNum = PING_STRIPES;

    static const CRGBA colGreen = CRGBA(35, 176, 74, 255);
    static const CRGBA colYellow = CRGBA(253, 197, 0, 255);
    static const CRGBA colRed = CRGBA(255, 45, 45, 255);

    CRGBA pingColor;

    if (nRTT <= 60)
    {
        nPingStripesNum = PING_STRIPES;
        pingColor = colGreen;
    }
    else if (nRTT <= 100)
    {
        nPingStripesNum = PING_STRIPES - 1;
        pingColor = colGreen;
    }
    else if (nRTT <= 150)
    {
        nPingStripesNum = PING_STRIPES - 2;
        pingColor = colYellow;
    }
    else
    {
        nPingStripesNum = PING_STRIPES - 3;
        pingColor = colRed;
    }

    CRect rect = CRect(CUtil::HUD_X(fX + PING_OFFSET_X), CUtil::HUD_Y(fY + PING_OFFSET_Y),
        CUtil::HUD_X(fX + PING_OFFSET_X + PING_SCALE_X), CUtil::HUD_Y(fY + PING_OFFSET_Y + PING_SCALE_Y));

    for (uint8_t i = 0; i < nPingStripesNum; i++)
    {
        CSprite2d::DrawRect(rect, pingColor);

        rect.left += CUtil::HUD_X(PING_SPACE_X);
        rect.right += CUtil::HUD_X(PING_SPACE_X);
        rect.top -= CUtil::HUD_Y(PING_ADD_SCALE_Y);
    }

    for (; nPingStripesNum < PING_STRIPES; nPingStripesNum++)
    {
        pingColor.a = 100;
        CSprite2d::DrawRect(rect, pingColor);

        rect.left += CUtil::HUD_X(PING_SPACE_X);
        rect.right += CUtil::HUD_X(PING_SPACE_X);
        rect.top -= CUtil::HUD_Y(PING_ADD_SCALE_Y);
    }

    CFont::SetOrientation(eFontAlignment::ALIGN_CENTER);
    CFont::SetColor(CRGBA(255, 255, 255, 255));
    CFont::SetFontStyle(1);
    CFont::SetEdge(0);

    char buffer[16];
    _itoa_s(nRTT, buffer, sizeof(buffer), 10);

    CFont::SetScale(CUtil::HUD_X(PING_COUNT_SCALE_X), CUtil::HUD_Y(PING_COUNT_SCALE_Y));
    CFont::PrintString(CUtil::HUD_X(fX + PING_OFFSET_X + PING_COUNT_OFFSET_X),
        CUtil::HUD_Y(fY + PING_OFFSET_Y + PING_COUNT_OFFSET_Y - PING_ADD_SCALE_Y * PING_STRIPES), buffer);
}

void CNetworkPlayerList::DrawName(CNetworkPlayer* pNetworkPlayer, float fX, float fY)
{
    char szPlayerName[Config::MAX_NICKNAME_LENGTH + 1];

    if (pNetworkPlayer == nullptr)
    {
        strcpy_s(szPlayerName, CLocalPlayer::m_Name);
    }
    else
    {
        strcpy_s(szPlayerName, pNetworkPlayer->GetName().c_str());
    }

    float fNormalizedValue =
        static_cast<float>(strlen(szPlayerName) - 1) / static_cast<float>(sizeof(szPlayerName) / sizeof(char) - 1 - 1);

    CFont::SetScale(CUtil::HUD_X(MAX_NAME_SCALE_X - (MAX_NAME_SCALE_X - MIN_NAME_SCALE_X) * fNormalizedValue),
        CUtil::HUD_Y(MAX_NAME_SCALE_Y - (MAX_NAME_SCALE_Y - MIN_NAME_SCALE_Y) * fNormalizedValue));

    CFont::SetOrientation(eFontAlignment::ALIGN_LEFT);
    CFont::SetColor(CRGBA(255, 255, 255, 255));
    CFont::SetFontStyle(1);
    CFont::SetEdge(1);

    CFont::PrintString(CUtil::HUD_X(fX + NAME_OFFSET_X),
        CUtil::HUD_Y(fY + NAME_OFFSET_Y) + CUtil::HUD_Y(4.0f + MAX_NAME_OFFSET_Y * fNormalizedValue), szPlayerName);
}

void CNetworkPlayerList::DrawBars(CPlayerPed* pPlayerPed, float fX, float fY)
{
    float fBarOffsetX = CUtil::HUD_X(fX + BAR_OFFSET_X + BOX_WIDTH / 2.0f);
    float fBarOffsetY = CUtil::HUD_Y(fY + BAR_OFFSET_Y);

    uint16_t barWidth = static_cast<uint16_t>(CUtil::HUD_X(BAR_WIDTH));
    uint8_t barHeight = static_cast<uint8_t>(CUtil::HUD_Y(BAR_HEIGHT));

    if (pPlayerPed->m_fArmour != 0.0f)
    {
        CSprite2d::DrawBarChart(fBarOffsetX + barWidth, fBarOffsetY - BAR_OFFSET_Y - BAR_ARMOUR_OFFSET_Y, barWidth,
            barHeight, pPlayerPed->m_fArmour, 0, 0, 1, HudColour.GetRGBA(HUD_COLOUR_WHITE), CRGBA(0, 0, 0, 0));
    }

    CSprite2d::DrawBarChart(fBarOffsetX + barWidth, fBarOffsetY, barWidth, barHeight, pPlayerPed->m_fHealth, 0, 0, 1,
        HudColour.GetRGBA(HUD_COLOUR_RED), CRGBA(0, 0, 0, 0));
}

void CNetworkPlayerList::DrawWeaponIcon(CPlayerPed* pPlayerPed, float fX, float fY)
{
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, RWRSTATE(rwFILTERLINEAR));

    int nModelId = CUtil::GetWeaponModelById(pPlayerPed->GetWeapon().m_eWeaponType);

    float fWidth = CUtil::HUD_X(47.0f / 2.0f);
    float fHeight = CUtil::HUD_Y(58.0f / 2.0f);
    float fHalfWidth = fWidth / 2.0f;
    float fHalfHeight = fHeight / 2.0f;

    float fOffsetX = CUtil::HUD_X(fX + BOX_WIDTH) - fWidth - CUtil::SCREEN_SCALE_X(7.0f);
    float fOffsetY = CUtil::HUD_Y(fY + NAME_OFFSET_Y) - fHalfHeight / 2.0f;

    if (nModelId <= 0)
    {
        CHud::Sprites[0].Draw({fOffsetX, fOffsetY, fWidth + fOffsetX, fHeight + fOffsetY}, CRGBA(255, 255, 255, 255));
        return;
    }

    CBaseModelInfo* mi = CModelInfo::GetModelInfo(nModelId);
    TxdDef* txd = CTxdStore::ms_pTxdPool->GetAt(mi->m_nTxdIndex);
    if (txd == nullptr)
    {
        return;
    }

    RwTexture* texture =
        RwTexDictionaryFindHashNamedTexture(txd->m_pRwDictionary, CKeyGen::AppendStringToKey(mi->m_nKey, "ICON"));
    if (texture == nullptr)
    {
        return;
    }

    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, RWRSTATE(FALSE));
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, RWRSTATE(RwTextureGetRaster(texture)));

    CSprite::RenderOneXLUSprite(fOffsetX + fHalfWidth, fOffsetY + fHalfHeight, 1.0f, fHalfWidth, fHalfHeight, 255, 255,
        255, 255, 1.0f, 255, 0, 0);

    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, RWRSTATE(FALSE));
}

void CNetworkPlayerList::DrawSeparator(float fCenterBoxX, float fCenterBoxY, float fColumnY)
{
    float fSeparatorX = fCenterBoxX + SEPARATOR_PADDING_X;
    float fSeparatorY = fColumnY + fCenterBoxY / 2.0f + SEPARATOR_OFFSET_Y;

    CSprite2d::DrawRect(
        CRect(CUtil::HUD_X(fSeparatorX), CUtil::HUD_Y(fSeparatorY),
            CUtil::HUD_X(fSeparatorX + BOX_WIDTH + SEPARATOR_WIDTH), CUtil::HUD_Y(fSeparatorY + SEPARATOR_HEIGHT)),
        CRGBA(169, 169, 169, 130));
}

void CNetworkPlayerList::Draw()
{
    if (CPad::NewKeyState.tab == 0 || FrontEndMenuManager.m_bMenuActive)
    {
        return;
    }

    if (CConfigLoader::ms_bPlayerListCoopSa)
    {
        DrawCoopSa();
    }
    else
    {
        DrawClassic();
    }
}

// ---- player-list-coopsa ----

static RwTexture* ms_pLogoTexture = nullptr;
static bool ms_bLogoFailed = false;

// the logo as a texture: black disc with the white logo, transparent outside the circle
static RwTexture* GetLogoTexture()
{
    if (ms_pLogoTexture != nullptr || ms_bLogoFailed)
    {
        return ms_pLogoTexture;
    }

    const int size = COOPSA_LOGO_SIZE;
    RwRaster* raster = RwRasterCreate(size, size, 32, rwRASTERTYPETEXTURE | rwRASTERFORMAT8888);
    if (raster == nullptr)
    {
        ms_bLogoFailed = true;
        return nullptr;
    }

    RwUInt8* pixels = RwRasterLock(raster, 0, rwRASTERLOCKWRITE);
    if (pixels == nullptr)
    {
        RwRasterDestroy(raster);
        ms_bLogoFailed = true;
        return nullptr;
    }

    const float center = (size - 1) / 2.0f;
    const float radius = size / 2.0f - 0.5f;
    for (int y = 0; y < size; y++)
    {
        RwUInt8* row = pixels + y * raster->stride;
        for (int x = 0; x < size; x++)
        {
            const float dx = x - center;
            const float dy = y - center;
            const float edge = std::clamp(radius - sqrtf(dx * dx + dy * dy), 0.0f, 1.0f);
            const RwUInt8 luma = COOPSA_LOGO_LUMA[y * size + x];
            row[x * 4 + 0] = luma;  // B
            row[x * 4 + 1] = luma;  // G
            row[x * 4 + 2] = luma;  // R
            row[x * 4 + 3] = static_cast<RwUInt8>(edge * 235.0f);
        }
    }
    RwRasterUnlock(raster);

    ms_pLogoTexture = RwTextureCreate(raster);
    if (ms_pLogoTexture == nullptr)
    {
        RwRasterDestroy(raster);
        ms_bLogoFailed = true;
        return nullptr;
    }
    RwTextureSetFilterMode(ms_pLogoTexture, rwFILTERLINEAR);
    return ms_pLogoTexture;
}

void CNetworkPlayerList::Shutdown()
{
    if (ms_pLogoTexture != nullptr)
    {
        RwTextureDestroy(ms_pLogoTexture);
        ms_pLogoTexture = nullptr;
    }
    ms_bLogoFailed = false;
}

// "$12.450" (dots for thousands, as the mock-up)
static void FormatMoney(int32_t money, char* out, size_t outSize)
{
    const int64_t value = money;
    char digits[24];
    sprintf_s(digits, "%lld", value < 0 ? -value : value);
    const size_t length = strlen(digits);

    char grouped[32];
    size_t n = 0;
    for (size_t i = 0; i < length; i++)
    {
        if (i > 0 && (length - i) % 3 == 0)
        {
            grouped[n++] = '.';
        }
        grouped[n++] = digits[i];
    }
    grouped[n] = '\0';
    sprintf_s(out, outSize, "%s$%s", value < 0 ? "-" : "", grouped);
}

static void SetTextStyle(eFontStyle style, float scaleX, float scaleY, const CRGBA& color, eFontAlignment alignment)
{
    CFont::SetBackground(false, false);
    CFont::SetProportional(true);
    CFont::SetJustify(false);
    CFont::SetWrapx(static_cast<float>(RsGlobal.maximumWidth));
    CFont::SetRightJustifyWrap(0.0f);
    CFont::SetOrientation(alignment);
    CFont::SetEdge(1);
    CFont::SetDropColor(CRGBA(0, 0, 0, 255));
    CFont::SetFontStyle(style);
    CFont::SetScale(CUtil::HUD_X(scaleX), CUtil::HUD_Y(scaleY));
    CFont::SetColor(color);
}

// the weapon's icon (its model's "ICON" texture, as DrawWeaponIcon), or the fist
static void DrawWeaponIconAt(CPlayerPed* pPlayerPed, float fCenterX, float fCenterY, float fHalfSize)
{
    const float halfSize = CUtil::HUD_Y(fHalfSize);
    const float centerX = CUtil::HUD_X(fCenterX);
    const float centerY = CUtil::HUD_Y(fCenterY);

    const int nModelId = CUtil::GetWeaponModelById(pPlayerPed->GetWeapon().m_eWeaponType);
    if (nModelId <= 0)
    {
        CHud::Sprites[0].Draw(CRect(centerX - halfSize, centerY - halfSize, centerX + halfSize, centerY + halfSize),
            CRGBA(255, 255, 255, 255));
        return;
    }

    CBaseModelInfo* mi = CModelInfo::GetModelInfo(nModelId);
    TxdDef* txd = mi ? CTxdStore::ms_pTxdPool->GetAt(mi->m_nTxdIndex) : nullptr;
    if (txd == nullptr || txd->m_pRwDictionary == nullptr)
    {
        return;
    }

    RwTexture* texture =
        RwTexDictionaryFindHashNamedTexture(txd->m_pRwDictionary, CKeyGen::AppendStringToKey(mi->m_nKey, "ICON"));
    if (texture == nullptr)
    {
        return;
    }

    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, RWRSTATE(rwFILTERLINEAR));
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, RWRSTATE(FALSE));
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, RWRSTATE(RwTextureGetRaster(texture)));

    CSprite::RenderOneXLUSprite(centerX, centerY, 1.0f, halfSize, halfSize, 255, 255, 255, 255, 1.0f, 255, 0, 0);

    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, RWRSTATE(FALSE));
}

static CRGBA PingColour(uint32_t rtt)
{
    if (rtt <= 80)
    {
        return CRGBA(90, 220, 70, 255);
    }
    if (rtt <= 150)
    {
        return CRGBA(245, 200, 40, 255);
    }
    return CRGBA(240, 60, 60, 255);
}

void CNetworkPlayerList::DrawCoopSa()
{
    // the local player first, then the others by id
    std::vector<CNetworkPlayer*> vRemote;
    for (CNetworkPlayer* pPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (pPlayer != nullptr)
        {
            vRemote.push_back(pPlayer);
        }
    }
    std::sort(vRemote.begin(), vRemote.end(),
        [](CNetworkPlayer* a, CNetworkPlayer* b) { return a->m_playerId < b->m_playerId; });

    const int nRows = static_cast<int>(std::min<size_t>(vRemote.size() + 1, Config::MAX_SERVER_PLAYERS));
    const float fX = CS_X;
    const float fY = CS_Y;
    const float fRight = fX + CS_WIDTH;
    const float fBottom = fY + CS_HEADER + nRows * CS_ROW + 4.0f;

    // the game's own panel look (like SA's stats box): black translucent, gothic title, subtitle font, the HUD's colours
    // and bars (asked by Lyrax, 2026-11-13: nothing overlaps, armour above health, smaller money)
    CSprite2d::DrawRect(
        CRect(CUtil::HUD_X(fX), CUtil::HUD_Y(fY), CUtil::HUD_X(fRight), CUtil::HUD_Y(fBottom)), CRGBA(0, 0, 0, 180));

    // header: logo, title, count and the line
    if (RwTexture* logo = GetLogoTexture())
    {
        CSprite2d sprite;
        sprite.m_pTexture = logo;
        const float centerX = CUtil::HUD_X(fX + 20.0f);
        const float centerY = CUtil::HUD_Y(fY + 16.0f);
        const float radius = CUtil::HUD_Y(CS_LOGO_RADIUS);
        sprite.Draw(CRect(centerX - radius, centerY - radius, centerX + radius, centerY + radius),
            CRGBA(255, 255, 255, 255));
        sprite.m_pTexture = nullptr;  // ours, kept until Shutdown: ~CSprite2d must not destroy it
    }

    SetTextStyle(FONT_GOTHIC, 0.75f, 1.5f, HudColour.GetRGBA(HUD_COLOUR_WHITE), ALIGN_LEFT);
    CFont::SetEdge(2);
    CFont::PrintString(CUtil::HUD_X(fX + 44.0f), CUtil::HUD_Y(fY + 4.0f), "Jugadores");

    char szCount[16];
    sprintf_s(szCount, "%d/%d", nRows, std::max(nRows, CS_SHOWN_MAX_PLAYERS));
    SetTextStyle(FONT_SUBTITLES, 0.4f, 0.9f, HudColour.GetRGBA(HUD_COLOUR_WHITE), ALIGN_RIGHT);
    CFont::PrintString(CUtil::HUD_X(fRight - 8.0f), CUtil::HUD_Y(fY + 12.0f), szCount);

    CSprite2d::DrawRect(CRect(CUtil::HUD_X(fX + 8.0f), CUtil::HUD_Y(fY + CS_HEADER - 3.0f), CUtil::HUD_X(fRight - 8.0f),
                            CUtil::HUD_Y(fY + CS_HEADER - 2.0f)),
        HudColour.GetRGBA(HUD_COLOUR_GREY));

    // columns (base units): name/money | armour over health | weapon | ping
    const float fTextX = fX + 12.0f;
    const float fTextRoom = 92.0f;
    const float fBarsX = fX + 110.0f;
    const float fBarWidth = 46.0f;
    const float fIconX = fX + 176.0f;
    const float fPingRight = fRight - 8.0f;

    for (int i = 0; i < nRows; i++)
    {
        const bool bLocal = i == 0;
        CNetworkPlayer* pNetworkPlayer = bLocal ? nullptr : vRemote[i - 1];
        CPlayerPed* pPed = bLocal ? FindPlayerPed(0) : pNetworkPlayer->m_pPed;
        const float fRow = fY + CS_HEADER + i * CS_ROW;

        if (i > 0)
        {
            CSprite2d::DrawRect(CRect(CUtil::HUD_X(fX + 8.0f), CUtil::HUD_Y(fRow - 0.5f), CUtil::HUD_X(fRight - 8.0f),
                                    CUtil::HUD_Y(fRow)),
                CRGBA(120, 120, 120, 110));
        }

        if (bLocal)
        {
            CSprite2d::DrawRect(CRect(CUtil::HUD_X(fX + 4.0f), CUtil::HUD_Y(fRow + 4.0f), CUtil::HUD_X(fX + 6.0f),
                                    CUtil::HUD_Y(fRow + CS_ROW - 4.0f)),
                HudColour.GetRGBA(HUD_COLOUR_ORANGELIGHT));
        }

        // name, narrowed to its room ("Tu" behind the local one; 0xAA is the font's u with an acute accent)
        char szName[Config::MAX_NICKNAME_LENGTH + 1];
        strcpy_s(szName, bLocal ? CLocalPlayer::m_Name : pNetworkPlayer->GetName().c_str());
        SetTextStyle(FONT_SUBTITLES, 0.38f, 0.85f, HudColour.GetRGBA(HUD_COLOUR_WHITE), ALIGN_LEFT);
        const float fNameRoom = CUtil::HUD_X(fTextRoom - (bLocal ? 16.0f : 0.0f));
        float fNameWidth = CFont::GetStringWidth(szName, true);
        if (fNameWidth > fNameRoom)
        {
            CFont::SetScale(CUtil::HUD_X(0.38f * fNameRoom / fNameWidth), CUtil::HUD_Y(0.85f));
            fNameWidth = fNameRoom;
        }
        CFont::PrintString(CUtil::HUD_X(fTextX), CUtil::HUD_Y(fRow + 2.0f), szName);
        if (bLocal)
        {
            SetTextStyle(FONT_SUBTITLES, 0.32f, 0.75f, HudColour.GetRGBA(HUD_COLOUR_ORANGELIGHT), ALIGN_LEFT);
            CFont::PrintString(CUtil::HUD_X(fTextX + 3.0f) + fNameWidth, CUtil::HUD_Y(fRow + 3.0f), "T\xAA");
        }

        // money, smaller, under the name and never wider than its room (a remote player's comes with PLAYER_MONEY)
        if (bLocal || pNetworkPlayer->m_bHasMoney)
        {
            char szMoney[40];
            FormatMoney(bLocal ? CWorld::Players[0].m_nMoney : pNetworkPlayer->m_nMoney, szMoney, sizeof(szMoney));
            SetTextStyle(FONT_PRICEDOWN, 0.3f, 0.6f, HudColour.GetRGBA(HUD_COLOUR_GREEN), ALIGN_LEFT);
            const float fMoneyRoom = CUtil::HUD_X(fTextRoom);
            const float fMoneyWidth = CFont::GetStringWidth(szMoney, true);
            if (fMoneyWidth > fMoneyRoom)
                CFont::SetScale(CUtil::HUD_X(0.3f * fMoneyRoom / fMoneyWidth), CUtil::HUD_Y(0.6f));
            CFont::PrintString(CUtil::HUD_X(fTextX), CUtil::HUD_Y(fRow + 16.0f), szMoney);
        }

        // armour above health with the HUD's own bars; a remote player with no ped here (F1, joining) shows them empty
        float fHealth = 0.0f;
        float fArmour = 0.0f;
        if (pPed != nullptr)
        {
            const float fMaxHealth = bLocal ? static_cast<float>(CWorld::Players[0].m_nMaxHealth) : pPed->m_fMaxHealth;
            const float fMaxArmour = bLocal ? static_cast<float>(CWorld::Players[0].m_nMaxArmour) : 100.0f;
            fHealth = fMaxHealth > 0.0f ? pPed->m_fHealth / fMaxHealth : 0.0f;
            fArmour = fMaxArmour > 0.0f ? pPed->m_fArmour / fMaxArmour : 0.0f;
        }
        const auto barWidth = static_cast<unsigned short>(CUtil::HUD_X(fBarWidth));
        const auto barHeight = static_cast<unsigned char>(std::max(2.0f, CUtil::HUD_Y(5.0f)));
        CSprite2d::DrawBarChart(CUtil::HUD_X(fBarsX), CUtil::HUD_Y(fRow + 7.0f), barWidth, barHeight,
            std::clamp(fArmour, 0.0f, 1.0f) * 100.0f, 0, false, true, HudColour.GetRGBA(HUD_COLOUR_WHITE), CRGBA(0, 0, 0, 0));
        CSprite2d::DrawBarChart(CUtil::HUD_X(fBarsX), CUtil::HUD_Y(fRow + 17.0f), barWidth, barHeight,
            std::clamp(fHealth, 0.0f, 1.0f) * 100.0f, 0, false, true, HudColour.GetRGBA(HUD_COLOUR_RED), CRGBA(0, 0, 0, 0));

        if (pPed != nullptr)
        {
            DrawWeaponIconAt(pPed, fIconX, fRow + CS_ROW / 2.0f, 9.0f);
        }

        // ping right-aligned in its own column after the icon
        const uint32_t nRTT = bLocal ? CNetwork::GetRTT() : pNetworkPlayer->m_nRTT;
        char szPing[16];
        sprintf_s(szPing, "%u ms", nRTT);
        SetTextStyle(FONT_SUBTITLES, 0.32f, 0.75f, PingColour(nRTT), ALIGN_RIGHT);
        const float fPingRoom = CUtil::HUD_X(fPingRight - fIconX - 11.0f);
        const float fPingWidth = CFont::GetStringWidth(szPing, true);
        if (fPingWidth > fPingRoom)
            CFont::SetScale(CUtil::HUD_X(0.32f * fPingRoom / fPingWidth), CUtil::HUD_Y(0.75f));
        CFont::PrintString(CUtil::HUD_X(fPingRight), CUtil::HUD_Y(fRow + 9.5f), szPing);
    }

    CFont::RenderFontBuffer();
}

// ---- the old centered list (player-list-coopsa = false) ----

void CNetworkPlayerList::DrawClassic()
{

    float fBoxX = CUtil::SCREEN_BASE_WIDTH / 2.0f - BOX_WIDTH / 2.0f;
    float fBoxY = CUtil::SCREEN_BASE_HEIGHT / 2.0f - BOX_HEIGHT / 2.0f;

    uint8_t nPlayerCount = CNetworkPlayerManager::m_pPlayers.size();
    std::vector<playerid_t> vPlayerId;

    vPlayerId.reserve(nPlayerCount + 1);

    for (uint8_t i = 0; i < nPlayerCount; i++)
    {
        vPlayerId.push_back(CNetworkPlayerManager::m_pPlayers[i]->m_playerId);
    }

    vPlayerId.push_back(CNetworkPlayerManager::m_localPlayerId);
    std::sort(vPlayerId.begin(), vPlayerId.end());

    DrawBox(fBoxX, fBoxY);

    for (uint8_t i = 0; i < vPlayerId.size() && i < Config::MAX_SERVER_PLAYERS; i++)
    {
        CNetworkPlayer* pNetworkPlayer = nullptr;
        CPlayerPed* pPlayerPed = nullptr;

        if (vPlayerId[i] == CNetworkPlayerManager::m_localPlayerId)
        {
            pPlayerPed = FindPlayerPed(0);
        }
        else
        {
            for (CNetworkPlayer* pPlayer : CNetworkPlayerManager::m_pPlayers)
            {
                if (pPlayer->m_playerId == vPlayerId[i])
                {
                    pNetworkPlayer = pPlayer;
                    pPlayerPed = pPlayer->m_pPed;
                    break;
                }
            }
        }

        float fColumnY = fBoxY + i * COLUMN_HEIGHT;

        DrawPing(pNetworkPlayer, fBoxX, fColumnY);
        DrawName(pNetworkPlayer, fBoxX, fColumnY);
        // remote ped may not exist yet (joining, separated by script mode)
        if (pPlayerPed != nullptr)
        {
            DrawBars(pPlayerPed, fBoxX, fColumnY);
            DrawWeaponIcon(pPlayerPed, fBoxX, fColumnY);
        }

        if (i == nPlayerCount)
        {
            continue;
        }

        DrawSeparator(fBoxX, fBoxY, fColumnY);
    }
}
