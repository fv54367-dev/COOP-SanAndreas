#include "stdafx.h"
#include "CCoopPoolHud.h"
#include <CFont.h>
#include <CMessages.h>
#include <CSprite2d.h>
#include <CText.h>
#include <CTheScripts.h>
#include <CTimer.h>
#include <CTxdStore.h>
#include <algorithm>
#include <cmath>

namespace CoopPoolHud {
namespace {
constexpr const char* Keys[] = {
    "", "PL_01", "PL_02", "PL_03", "PL_04", "PL_05", "PL_06", "PL_12",
    "PL_13", "PL_14", "PL_15", "PL_16", "PL_17", "FOUL1", "FOUL2", "FOUL3",
    "FOUL4", "FOUL5", "FOUL6", "NUM", "COMMA"
};
static_assert(std::size(Keys) <= 32);

constexpr char TxdName[] = "coop_pool_hud";
Hud captured;
uint32_t captureFrame = 0;
unsigned textBefore = 0, rectBefore = 0;
uint8_t displayedHelp = 0;
char previousTable[9]{};
bool textTried = false, ownsTextTable = false;
bool spritesTried = false;
int txd = -1;
CSprite2d sprites[2];

float FloatBits(int bits) {
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

bool Range(float value, float min, float max) {
    return std::isfinite(value) && value >= min && value <= max;
}

void BeginDrawRecord() {
    if (captureFrame == CTimer::m_FrameCounter) return;
    captureFrame = CTimer::m_FrameCounter;
    captured.textCount = captured.rectCount = 0;
}

bool LoadPoolText() {
    if (TheText.missionTableLoaded && !_strnicmp(TheText.missionTableName, "POOL", 8)) {
        textTried = true;
        return true;
    }
    // If another script took over after our first load, leave its table alone.
    if (textTried) return false;
    textTried = true;
    if (TheText.missionTableLoaded) memcpy(previousTable, TheText.missionTableName, 8);
    TheText.LoadMissionTable("POOL");
    ownsTextTable = TheText.missionTableLoaded && !_strnicmp(TheText.missionTableName, "POOL", 8);
    return ownsTextTable;
}

bool LoadSprites() {
    if (spritesTried) return txd >= 0;
    spritesTried = true;
    txd = CTxdStore::AddTxdSlot(TxdName);
    if (txd < 0) return false;
    // Use a separate slot. The native SCRIPT dictionary belongs to local scripts.
    if (!CTxdStore::LoadTxd(txd, "models\\txd\\LD_POOL.txd")) {
        CTxdStore::RemoveTxdSlot(txd);
        txd = -1;
        return false;
    }
    CTxdStore::AddRef(txd);
    CTxdStore::PushCurrentTxd();
    CTxdStore::SetCurrentTxd(txd);
    sprites[0].SetTexture(const_cast<char*>("ball"));
    sprites[1].SetTexture(const_cast<char*>("nib"));
    CTxdStore::PopCurrentTxd();
    return true;
}

void UpdateHelp(uint8_t help) {
    if (help == displayedHelp) return;
    if (!help) {
        CHud::SetHelpMessage(nullptr, true, false, false);
    } else {
        char label[] = "PL_H0";
        label[4] = char('0' + help);
        CHud::SetHelpMessage(TheText.Get(label), false, true, false);
    }
    displayedHelp = help;
}
} // namespace

void BeforeCommand(const CRunningScript& script, uint16_t opcode) {
    switch (opcode) {
    case 0x033E: case 0x045A:
        textBefore = CTheScripts::NumberOfIntroTextLinesThisFrame;
        break;
    case 0x038D: case 0x0937:
        rectBefore = CTheScripts::NumberOfIntroRectanglesThisFrame;
        break;
    case 0x03E5: case 0x0512: {
        const auto* ip = script.m_pCurrentIP;
        if (ip[0] == 9 && !memcmp(ip + 1, "PL_H", 4) && ip[5] >= '1' && ip[5] <= '9' && !ip[6])
            captured.help = uint8_t(ip[5] - '0');
        break;
    }
    case 0x03E6:
        captured.help = 0;
        break;
    }
}

void AfterCommand(uint16_t opcode) {
    if (opcode == 0x033E || opcode == 0x045A) {
        if (textBefore >= 96 || CTheScripts::NumberOfIntroTextLinesThisFrame != textBefore + 1) return;
        BeginDrawRecord();
        if (captured.textCount >= Hud::MaxText) return;
        const auto& entry = CTheScripts::IntroTextLines[textBefore];
        const auto key = std::find_if(std::begin(Keys) + 1, std::end(Keys), [&](const char* allowed) {
            return strncmp(entry.gxtEntry, allowed, sizeof(entry.gxtEntry)) == 0;
        });
        if (key == std::end(Keys)) return;

        auto& text = captured.texts[captured.textCount++];
        text.key = static_cast<uint8_t>(key - std::begin(Keys));
        text.x = FloatBits(entry.xPosition);
        text.y = FloatBits(entry.yPosition);
        text.scaleX = entry.letterWidth;
        text.scaleY = FloatBits(entry.letterHeight);
        memcpy(&text.color, &entry.color, sizeof(text.color));
        text.number = std::clamp(entry.param1, -1, 16);
        text.font = static_cast<uint8_t>(std::clamp(entry.font, 0, 3));
        text.align = entry.m_bRightJustify ? ALIGN_RIGHT : (entry.centered ? ALIGN_CENTER : ALIGN_LEFT);
    } else if (opcode == 0x038D || opcode == 0x0937) {
        if (rectBefore >= 128 || CTheScripts::NumberOfIntroRectanglesThisFrame != rectBefore + 1) return;
        BeginDrawRecord();
        if (captured.rectCount >= Hud::MaxRects || RsGlobal.maximumWidth <= 0 || RsGlobal.maximumHeight <= 0) return;
        const auto& entry = CTheScripts::IntroRectangles[rectBefore];
        if (opcode == 0x0937) {
            if (entry.type != 2 || strncmp(entry.gxt, "SCORE", sizeof(entry.gxt))) return;
        } else if (entry.type != 4 || entry.textureID < 0 || entry.textureID > 1) return;
        auto& rect = captured.rects[captured.rectCount++];
        rect.kind = opcode == 0x0937 ? Hud::PANEL : uint8_t(Hud::BALL + entry.textureID);
        rect.left = FloatBits(entry.cornerA_X) / RsGlobal.maximumWidth;
        rect.top = FloatBits(entry.cornerA_Y) / RsGlobal.maximumHeight;
        rect.right = FloatBits(entry.cornerB_X) / RsGlobal.maximumWidth;
        rect.bottom = FloatBits(entry.cornerB_Y) / RsGlobal.maximumHeight;
    }
}

Hud Snapshot() {
    Hud hud = captured;
    if (CTimer::m_FrameCounter - captureFrame > 1) hud.textCount = hud.rectCount = 0;
    return hud;
}

uint32_t Hash(const Hud& hud) {
    uint32_t hash = 2166136261u;
    const auto mix = [&](uint32_t value) { hash = (hash ^ value) * 16777619u; };
    const auto mixFloat = [&](float value) {
        uint32_t bits;
        memcpy(&bits, &value, sizeof(bits));
        mix(bits);
    };
    mix(hud.help); mix(hud.textCount); mix(hud.rectCount);
    for (unsigned i = 0; i < hud.textCount; ++i) {
        const auto& t = hud.texts[i];
        mix(t.key); mix(t.color); mix(t.font); mix(t.align); mix(static_cast<uint32_t>(t.number));
        mixFloat(t.x); mixFloat(t.y); mixFloat(t.scaleX); mixFloat(t.scaleY);
    }
    for (unsigned i = 0; i < hud.rectCount; ++i) {
        const auto& r = hud.rects[i];
        mix(r.kind); mixFloat(r.left); mixFloat(r.top); mixFloat(r.right); mixFloat(r.bottom);
    }
    return hash;
}

void Draw(const Hud& hud) {
    if ((!hud.help && !hud.textCount && !hud.rectCount && !displayedHelp) || !LoadPoolText()) return;
    UpdateHelp(hud.help);

    const float width = float(RsGlobal.maximumWidth);
    const float height = float(RsGlobal.maximumHeight);
    const float sx = width / 640.0f, sy = height / 448.0f;
    float wrap = width;
    for (unsigned i = 0; i < hud.rectCount; ++i) {
        const auto& r = hud.rects[i];
        if (!Range(r.left, -0.25f, 1.25f) || !Range(r.top, -0.25f, 1.25f) ||
            !Range(r.right, r.left, 1.25f) || !Range(r.bottom, r.top, 1.25f)) continue;
        const CRect bounds(r.left * width, r.top * height, r.right * width, r.bottom * height);
        if (r.kind == Hud::PANEL) {
            // The SDK wrapper's CallMethod deduces a by-value CRect, but 0x573EE0
            // reads a pointer to the bounds and then the GXT key (gta-reversed DrawWindow).
            plugin::CallMethod<0x573EE0, CMenuManager*, const CRect&, const char*, uint8_t, CRGBA, bool, bool>(
                &FrontEndMenuManager, bounds, "SCORE", 0, CRGBA(0, 0, 0, 190), true, true);
            wrap = std::max(bounds.right - 20.0f * sx, 64.0f * sx);
        } else if ((r.kind == Hud::BALL || r.kind == Hud::NIB) && LoadSprites()) {
            sprites[r.kind - Hud::BALL].Draw(bounds, CRGBA(128, 128, 128, 255));
        }
    }

    CFont::SetBackground(false, false);
    CFont::SetProportional(true);
    CFont::SetJustify(false);
    CFont::SetWrapx(wrap);
    CFont::SetCentreSize(width);
    CFont::SetDropColor(CRGBA(0, 0, 0, 255));
    CFont::SetDropShadowPosition(2);
    for (unsigned i = 0; i < hud.textCount; ++i) {
        const auto& t = hud.texts[i];
        if (!t.key || t.key >= std::size(Keys) || !Range(t.x, 0.0f, 640.0f) || !Range(t.y, 0.0f, 448.0f) ||
            !Range(t.scaleX, 0.01f, 4.0f) || !Range(t.scaleY, 0.01f, 8.0f)) continue;

        char text[400]{};
        CMessages::InsertNumberInString(TheText.Get(Keys[t.key]), t.number, -1, -1, -1, -1, -1, text);
        CMessages::InsertPlayerControlKeysInString(text);
        CFont::SetOrientation(static_cast<eFontAlignment>(t.align));
        CFont::SetFontStyle(static_cast<eFontStyle>(t.font));
        CFont::SetScale(t.scaleX * sx, t.scaleY * 0.5f * sy);
        CFont::SetColor(CRGBA(t.color & 255, (t.color >> 8) & 255, (t.color >> 16) & 255, t.color >> 24));
        CFont::PrintString(t.x * sx, t.y * sy, text);
    }
    CFont::SetEdge(0);
}

void Reset(bool worldGone) {
    if (!worldGone) {
        if (displayedHelp) UpdateHelp(0);
        if (ownsTextTable && previousTable[0] && !_strnicmp(TheText.missionTableName, "POOL", 8))
            TheText.LoadMissionTable(previousTable);
    }
    if (txd >= 0 && CTxdStore::ms_pTxdPool && CTxdStore::FindTxdSlot(TxdName) == txd) {
        for (auto& sprite : sprites) sprite.Delete();
        CTxdStore::RemoveRefWithoutDelete(txd);
        CTxdStore::RemoveTxdSlot(txd);
    }
    txd = -1;
    spritesTried = textTried = ownsTextTable = false;
    previousTable[0] = '\0';
    displayedHelp = 0;
    captured = {};
    captureFrame = 0;
}
} // namespace CoopPoolHud
