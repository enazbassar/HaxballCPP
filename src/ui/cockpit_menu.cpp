#include "cockpit_menu.hpp"
#include "../render/font.hpp"
#include "../config/app_config.hpp"

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace haxball {

namespace {

static const char* kQuickEmojiList[14] = {
    "⚽", "🔥", "👑", "🐐", "💎", "⚡", "💀", "🥶", "😎", "🎯", "🏆", "🤡", "😂", "❤️"
};

static bool fileExists(const std::string& p) {
    if (FILE* f = std::fopen(p.c_str(), "rb")) {
        std::fclose(f);
        return true;
    }
    return false;
}

static std::string resolveFont(const std::string& rel) {
    if (fileExists(rel)) return rel;
    std::string up = "../" + rel;
    if (fileExists(up)) return up;
    return "";
}

static inline ImU32 rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return IM_COL32(r, g, b, a);
}

static inline ImU32 withAlpha(ImU32 col, uint8_t a) {
    return (col & 0x00FFFFFFu) | (static_cast<ImU32>(a) << 24);
}

static constexpr ImU32 kAccentPalette[5] = {
    IM_COL32(16, 185, 129, 255),
    IM_COL32(14, 165, 233, 255),
    IM_COL32(139, 92, 246, 255),
    IM_COL32(245, 158, 11, 255),
    IM_COL32(244, 63, 94, 255)
};

struct MenuTheme {
    bool light = false;
    ImU32 accent = kAccentPalette[0];
    ImU32 windowBg = 0;
    ImU32 headerBg = 0;
    ImU32 cardBg = 0;
    ImU32 cardHover = 0;
    ImU32 border = 0;
    ImU32 borderStrong = 0;
    ImU32 text = 0;
    ImU32 textMuted = 0;
    ImU32 textDim = 0;
    ImU32 trackBg = 0;
    ImU32 pillOffBg = 0;
    ImU32 knobCol = 0;
    ImU32 badgeBg = 0;
};

static MenuTheme buildTheme(bool lightMode, int accentIdx, float opacity) {
    MenuTheme th;
    th.light = lightMode;
    th.accent = kAccentPalette[std::clamp(accentIdx, 0, 4)];
    uint8_t winA = static_cast<uint8_t>(std::clamp(opacity, 0.65f, 1.0f) * 255.0f);

    if (!lightMode) {

        th.windowBg     = rgba(26, 33, 37, winA);
        th.headerBg     = rgba(19, 25, 28, winA);
        th.cardBg       = rgba(20, 26, 29, 245);
        th.cardHover    = rgba(33, 43, 48, 255);
        th.border       = rgba(42, 54, 61, 255);
        th.borderStrong = rgba(64, 82, 92, 255);
        th.text         = rgba(235, 241, 245, 255);
        th.textMuted    = rgba(154, 169, 178, 255);
        th.textDim      = rgba(104, 120, 130, 255);
        th.trackBg      = rgba(13, 18, 20, 255);
        th.pillOffBg    = rgba(48, 60, 68, 255);
        th.knobCol      = rgba(255, 255, 255, 255);
        th.badgeBg      = rgba(28, 37, 42, 255);
    } else {
        th.windowBg     = rgba(242, 245, 248, winA);
        th.headerBg     = rgba(255, 255, 255, winA);
        th.cardBg       = rgba(255, 255, 255, 255);
        th.cardHover    = rgba(235, 240, 246, 255);
        th.border       = rgba(212, 220, 230, 255);
        th.borderStrong = rgba(174, 188, 204, 255);
        th.text         = rgba(20, 28, 36, 255);
        th.textMuted    = rgba(82, 96, 112, 255);
        th.textDim      = rgba(124, 138, 154, 255);
        th.trackBg      = rgba(220, 228, 236, 255);
        th.pillOffBg    = rgba(194, 204, 216, 255);
        th.knobCol      = rgba(255, 255, 255, 255);
        th.badgeBg      = rgba(236, 241, 246, 255);
    }
    return th;
}

static MenuTheme gTheme;

static void uiPanelBg(ImVec2 p0, float w, float h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), gTheme.cardBg, 5.0f);
    dl->AddRect(p0, ImVec2(p0.x + w, p0.y + h), gTheme.border, 5.0f, 0, 1.0f);
}

static void uiGroupLabel(const char* title, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 tsz = ImGui::CalcTextSize(title);
    dl->AddText(ImVec2(p.x + 2.0f, p.y), gTheme.textDim, title);
    ImGui::Dummy(ImVec2(width, tsz.y + 3.0f));
}

static bool uiFlatToggle(const char* id, const char* label, const char* hotkey, bool& value, float width, float height = 24.0f) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, ImVec2(width, height));
    bool hovered = ImGui::IsItemHovered();
    if (clicked) value = !value;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pMax(p.x + width, p.y + height);

    if (hovered) {
        dl->AddRectFilled(p, pMax, gTheme.cardHover, 4.0f);
    }

    const float swW = 28.0f;
    const float swH = 14.0f;
    ImVec2 sw0(pMax.x - swW - 6.0f, p.y + (height - swH) * 0.5f);
    ImVec2 sw1(sw0.x + swW, sw0.y + swH);
    dl->AddRectFilled(sw0, sw1, value ? gTheme.accent : gTheme.pillOffBg, swH * 0.5f);

    float knobR = 5.0f;
    float knobX = value ? (sw1.x - knobR - 2.0f) : (sw0.x + knobR + 2.0f);
    dl->AddCircleFilled(ImVec2(knobX, (sw0.y + sw1.y) * 0.5f), knobR, gTheme.knobCol, 14);

    ImVec2 lsz = ImGui::CalcTextSize(label);
    float ly = p.y + (height - lsz.y) * 0.5f;
    dl->AddText(ImVec2(p.x + 7.0f, ly), value ? gTheme.text : gTheme.textMuted, label);

    if (hotkey && hotkey[0]) {
        char kbuf[12];
        std::snprintf(kbuf, sizeof(kbuf), "[%s]", hotkey);
        ImVec2 ksz = ImGui::CalcTextSize(kbuf);
        float kx = p.x + 7.0f + lsz.x + 4.0f;
        if (kx + ksz.x < sw0.x - 4.0f) {
            dl->AddText(ImVec2(kx, ly), gTheme.textDim, kbuf);
        }
    }

    return clicked;
}

static bool uiFlatSlider(const char* id, const char* label, const char* valText, float normVal, float width, float& outNewNorm, float labelW = 104.0f, float valW = 66.0f, ImU32 customAccent = 0) {
    const float h = 23.0f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(width, h));
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();

    outNewNorm = std::clamp(normVal, 0.0f, 1.0f);
    bool changed = false;

    const float trackX0 = p.x + labelW + 6.0f;
    const float trackX1 = p.x + width - valW - 6.0f;
    const float trackW = std::max(20.0f, trackX1 - trackX0);

    if (active) {
        float mx = ImGui::GetIO().MousePos.x;
        float t = std::clamp((mx - trackX0) / trackW, 0.0f, 1.0f);
        if (std::abs(t - outNewNorm) > 1e-4f) {
            outNewNorm = t;
            changed = true;
        }
    }

    ImU32 acc = (customAccent != 0) ? customAccent : gTheme.accent;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pMax(p.x + width, p.y + h);

    if (hovered || active) {
        dl->AddRectFilled(p, pMax, gTheme.cardHover, 4.0f);
    }

    ImVec2 lsz = ImGui::CalcTextSize(label);
    ImVec2 vsz = ImGui::CalcTextSize(valText);
    float ty = p.y + (h - lsz.y) * 0.5f;
    dl->AddText(ImVec2(p.x + 7.0f, ty), hovered || active ? gTheme.text : gTheme.textMuted, label);
    dl->AddText(ImVec2(pMax.x - vsz.x - 7.0f, ty), acc, valText);

    float cy = p.y + h * 0.5f;
    dl->AddRectFilled(ImVec2(trackX0, cy - 2.0f), ImVec2(trackX1, cy + 2.0f), gTheme.trackBg, 2.0f);
    float fillX = trackX0 + outNewNorm * trackW;
    if (fillX > trackX0 + 1.0f) {
        dl->AddRectFilled(ImVec2(trackX0, cy - 2.0f), ImVec2(fillX, cy + 2.0f), acc, 2.0f);
    }
    float knobR = active ? 5.8f : (hovered ? 5.2f : 4.6f);
    dl->AddCircleFilled(ImVec2(fillX, cy), knobR, acc, 16);
    dl->AddCircleFilled(ImVec2(fillX, cy), knobR - 1.6f, gTheme.knobCol, 16);

    return changed;
}

static bool uiSegmented(const char* idPrefix, const char* label, const char* const* opts, int count, int& current, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool hasLabel = (label && label[0]);
    float labelW = hasLabel ? 116.0f : 0.0f;
    float barH = 24.0f;

    if (hasLabel) {
        ImVec2 lsz = ImGui::CalcTextSize(label);
        dl->AddText(ImVec2(p.x + 6.0f, p.y + (barH - lsz.y) * 0.5f), gTheme.textMuted, label);
    }

    ImVec2 b0(p.x + labelW, p.y);
    ImVec2 b1(p.x + width, p.y + barH);
    float barW = b1.x - b0.x;
    dl->AddRectFilled(b0, b1, gTheme.trackBg, 4.0f);

    float btnW = barW / static_cast<float>(count);
    bool changed = false;

    for (int i = 0; i < count; ++i) {
        ImVec2 s0(b0.x + i * btnW, b0.y);
        ImVec2 s1(b0.x + (i + 1) * btnW, b1.y);
        ImGui::SetCursorScreenPos(s0);
        char bid[64];
        std::snprintf(bid, sizeof(bid), "%s_%d", idPrefix, i);
        bool clk = ImGui::InvisibleButton(bid, ImVec2(btnW, barH));
        bool hov = ImGui::IsItemHovered();
        if (clk && current != i) {
            current = i;
            changed = true;
        }

        bool sel = (current == i);
        if (sel) {
            dl->AddRectFilled(ImVec2(s0.x + 1.5f, s0.y + 1.5f), ImVec2(s1.x - 1.5f, s1.y - 1.5f), gTheme.accent, 3.0f);
        } else if (hov) {
            dl->AddRectFilled(ImVec2(s0.x + 1.5f, s0.y + 1.5f), ImVec2(s1.x - 1.5f, s1.y - 1.5f), gTheme.cardHover, 3.0f);
        }

        ImVec2 tsz = ImGui::CalcTextSize(opts[i]);
        ImU32 txtCol = sel ? rgba(255, 255, 255, 255) : (hov ? gTheme.text : gTheme.textMuted);
        dl->AddText(ImVec2(s0.x + (btnW - tsz.x) * 0.5f, s0.y + (barH - tsz.y) * 0.5f), txtCol, opts[i]);
    }

    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + barH + 5.0f));
    return changed;
}

static bool uiMiniBtn(const char* id, const char* label, float w, float h, bool active = false) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, ImVec2(w, h));
    bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p1(p.x + w, p.y + h);

    dl->AddRectFilled(p, p1, active ? gTheme.accent : (hovered ? gTheme.cardHover : gTheme.trackBg), 4.0f);
    if (!active && hovered) {
        dl->AddRect(p, p1, gTheme.borderStrong, 4.0f, 0, 1.0f);
    }

    ImVec2 tsz = ImGui::CalcTextSize(label);
    ImU32 tc = active ? rgba(255, 255, 255, 255) : (hovered ? gTheme.text : gTheme.textMuted);
    dl->AddText(ImVec2(p.x + (w - tsz.x) * 0.5f, p.y + (h - tsz.y) * 0.5f), tc, label);
    return clicked;
}

}

CockpitMenu::CockpitMenu() = default;

CockpitMenu::~CockpitMenu() {
    shutdown();
}

bool CockpitMenu::init(SDL_Window* window, SDL_Renderer* renderer) {
    if (initialized) return true;
    if (!window || !renderer) return false;

    sdlWindow = window;
    sdlRenderer = renderer;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(0.0f, 0.0f);
    style.FramePadding = ImVec2(6.0f, 4.0f);
    style.ItemSpacing = ImVec2(6.0f, 6.0f);
    style.WindowRounding = 10.0f;
    style.WindowBorderSize = 1.0f;

    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    cfg.PixelSnapH = true;

    std::string regPath = resolveFont("assets/fonts/Lato-Medium.ttf");
    std::string boldPath = resolveFont("assets/fonts/Lato-Bold.ttf");
    std::string fallbackPath = resolveFont("assets/fonts/font.ttf");

    if (!regPath.empty()) {
        fontRegular = io.Fonts->AddFontFromFileTTF(regPath.c_str(), 14.0f, &cfg);
    } else if (!fallbackPath.empty()) {
        fontRegular = io.Fonts->AddFontFromFileTTF(fallbackPath.c_str(), 14.0f, &cfg);
    }
    if (!boldPath.empty()) {
        fontBold = io.Fonts->AddFontFromFileTTF(boldPath.c_str(), 14.0f, &cfg);
        fontSmall = io.Fonts->AddFontFromFileTTF(boldPath.c_str(), 13.0f, &cfg);
    }
    if (!fontRegular) fontRegular = io.Fonts->AddFontDefault();
    if (!fontBold) fontBold = fontRegular;
    if (!fontSmall) fontSmall = fontBold;

    if (!ImGui_ImplSDL2_InitForSDLRenderer(window, renderer)) {
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplSDLRenderer2_Init(renderer)) {
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    loadEmojiTextures();
    initialized = true;
    return true;
}

void CockpitMenu::shutdown() {
    if (!initialized) return;
    destroyEmojiTextures();
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    sdlWindow = nullptr;
    sdlRenderer = nullptr;
    initialized = false;
}

void CockpitMenu::loadEmojiTextures() {
    if (!sdlRenderer) return;
    for (int i = 0; i < 14; ++i) {
        auto cps = FontRenderer::decodeNormalizedUTF8(kQuickEmojiList[i]);
        if (cps.empty()) continue;
        int w = 0, h = 0;
        std::vector<uint32_t> rgbaPx;
        if (FontRenderer::decodeColorEmojiRGBA(cps[0], w, h, rgbaPx) && w > 0 && h > 0) {
            SDL_Texture* tex = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
            if (tex) {
                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
                SDL_UpdateTexture(tex, nullptr, rgbaPx.data(), w * static_cast<int>(sizeof(uint32_t)));
                emojiTextures[i] = tex;
            }
        }
    }
}

void CockpitMenu::destroyEmojiTextures() {
    for (auto*& tex : emojiTextures) {
        if (tex) {
            SDL_DestroyTexture(tex);
            tex = nullptr;
        }
    }
}

bool CockpitMenu::processEvent(const SDL_Event& rawEvent, bool menuOpen) {
    if (!initialized || !menuOpen) return false;
    ImGui_ImplSDL2_ProcessEvent(&rawEvent);

    if (rawEvent.type == SDL_MOUSEBUTTONDOWN ||
        rawEvent.type == SDL_MOUSEBUTTONUP ||
        rawEvent.type == SDL_MOUSEMOTION ||
        rawEvent.type == SDL_MOUSEWHEEL) {
        return true;
    }
    return false;
}

bool CockpitMenu::handleX88KeyDown(
    SDL_Keycode sym,
    SDL_Keymod mods,
    bool isRepeat,
    GameRenderer& gr,
    HaxballClient& client,
    const World* world,
    bool isMultiplayer,
    OverlayAction& outAction
) {
    outAction = OverlayAction::None;
    if (!showX88Hud) return false;

    auto& a = gr.assistant;
    auto& st = gr.settings;
    const auto& cfg = AppConfig::get();

    if ((mods & (KMOD_CTRL | KMOD_ALT)) != 0) {
        X88Item hit = X88Item::None;
        if (sym == SDLK_0) {
            hit = X88Item::AutoGoal;
        } else if (sym == SDLK_1) {
            hit = X88Item::AnyGoal;
        } else if (sym == SDLK_6) {
            hit = X88Item::AutoSave;
        } else if (sym == SDLK_7) {
            hit = X88Item::SmartGk;
        } else if (sym == SDLK_8) {
            hit = X88Item::PenaltyGk;
        } else if (sym == SDLK_2) {
            hit = X88Item::AntiOpMode;
        } else if (sym == SDLK_3) {
            hit = X88Item::AntiFakeShot;
        } else if (sym == SDLK_4) {
            hit = X88Item::FirstTouchSoft;
        } else if (sym == cfg.keyFakeToggle || sym == SDLK_f) {
            hit = X88Item::FakeShot;
        } else if (sym == SDLK_5) {
            hit = X88Item::ExtendFake;
        } else if (sym == SDLK_p || sym == SDLK_9) {
            hit = X88Item::FastFake;
        } else if (sym == cfg.keyRealShot || sym == SDLK_n) {
            hit = X88Item::AutoClick;
        } else if (sym == cfg.keyAutoShake || sym == SDLK_b) {
            hit = X88Item::AutoShaker;
        } else if (sym == cfg.keyBallOrbit || sym == SDLK_o) {
            hit = X88Item::MoonOrbit;
        } else if (sym == cfg.keyFakeTyping || sym == SDLK_j) {
            hit = X88Item::FakeTyping;
        } else if (sym == cfg.keyTypingSpam || sym == SDLK_u) {
            hit = X88Item::TypeStrobe;
        } else if (sym == SDLK_e || sym == SDLK_LEFTBRACKET || sym == SDLK_RIGHTBRACKET) {
            hit = X88Item::Extrap;
        }

        if (hit != X88Item::None) {
            if (!isRepeat) {
                x88SelectedItem = (x88SelectedItem == hit) ? X88Item::None : hit;
            }
            return true;
        }
    }

    if (sym == SDLK_ESCAPE && x88SelectedItem != X88Item::None) {
        if (!isRepeat) {
            x88SelectedItem = X88Item::None;
        }
        return true;
    }

    auto getVisibleItems = [&]() -> std::vector<X88Item> {
        std::vector<X88Item> items;
        items.reserve(static_cast<size_t>(X88Item::Count));
        auto pushIf = [&](X88Item id, bool active) {
            if (!x88ActiveOnly || active || x88SelectedItem == id) {
                items.push_back(id);
            }
        };
        pushIf(X88Item::AutoGoal,       a.autoGoalEnabled);
        pushIf(X88Item::AnyGoal,        a.autoGoalAnyGoal);
        pushIf(X88Item::AutoSave,       a.autoSaveEnabled);
        pushIf(X88Item::SmartGk,        a.smartGkEnabled);
        pushIf(X88Item::PenaltyGk,      a.penaltyGkEnabled);
        pushIf(X88Item::AntiOpMode,     a.antiOpMode);
        pushIf(X88Item::AntiFakeShot,   a.antiFakeShotFilter);
        pushIf(X88Item::FirstTouchSoft, a.firstTouchCushionEnabled);
        pushIf(X88Item::FakeShot,       a.fakeShotEnabled);
        pushIf(X88Item::ExtendFake,     a.fakeShotExtendEnabled);
        pushIf(X88Item::FastFake,       a.holdingFastFakeKey);
        pushIf(X88Item::AutoClick,      a.holdingRealShotKey);
        pushIf(X88Item::AutoShaker,     a.holdingShakeKey);
        pushIf(X88Item::MoonOrbit,      a.holdingOrbitKey);
        pushIf(X88Item::FakeTyping,     a.fakeTypingActive);
        pushIf(X88Item::TypeStrobe,     a.typingSpamEnabled);
        items.push_back(X88Item::Extrap);
        return items;
    };

    if (sym == SDLK_UP || sym == SDLK_DOWN) {
        auto items = getVisibleItems();
        if (items.empty()) return true;

        if (x88SelectedItem == X88Item::None) {
            if (!isRepeat) {
                x88SelectedItem = (sym == SDLK_DOWN) ? items.front() : items.back();
            }
            return true;
        }

        int idx = -1;
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            if (items[i] == x88SelectedItem) {
                idx = i;
                break;
            }
        }
        int n = static_cast<int>(items.size());
        if (idx < 0) {
            x88SelectedItem = items.front();
        } else {
            int nextIdx = (idx + (sym == SDLK_DOWN ? 1 : -1) + n) % n;
            x88SelectedItem = items[nextIdx];
        }
        return true;
    }

    if ((sym == SDLK_LEFT || sym == SDLK_RIGHT) && x88SelectedItem != X88Item::None) {
        int dir = (sym == SDLK_RIGHT) ? 1 : -1;
        bool fast = (mods & KMOD_SHIFT) != 0;

        switch (x88SelectedItem) {
            case X88Item::AutoGoal:
                a.autoGoalAccuracy = std::clamp(a.autoGoalAccuracy + dir * (fast ? 5 : 1), 30, 100);
                outAction = OverlayAction::AssistanceChanged;
                break;

            case X88Item::AnyGoal:
                if (!isRepeat) {
                    std::vector<uint32_t> colors;
                    if (world && world->stadium) {
                        colors = TacticalAssistant::getAvailableGoalColors(*world->stadium);
                    }
                    if (!colors.empty()) {

                        int cur = 0;
                        if (a.autoGoalExcludeColor >= 0) {
                            uint32_t curC = static_cast<uint32_t>(a.autoGoalExcludeColor) & 0xFFFFFFu;
                            for (size_t i = 0; i < colors.size(); ++i) {
                                if ((colors[i] & 0xFFFFFFu) == curC) {
                                    cur = static_cast<int>(i) + 1;
                                    break;
                                }
                            }
                        }
                        int total = static_cast<int>(colors.size()) + 1;
                        int next = (cur + dir + total) % total;
                        if (next == 0) {
                            a.autoGoalExcludeColor = -1;
                        } else {
                            a.autoGoalExcludeColor = static_cast<int32_t>(colors[next - 1] & 0xFFFFFFu);
                            a.autoGoalAnyGoal = true;
                        }
                    } else {
                        a.autoGoalAnyGoal = !a.autoGoalAnyGoal;
                    }
                    outAction = OverlayAction::AssistanceChanged;
                }
                break;

            case X88Item::AutoSave:
                if (!isRepeat) {
                    a.autoSaveEnabled = !a.autoSaveEnabled;
                    outAction = OverlayAction::AssistanceChanged;
                }
                break;

            case X88Item::SmartGk:
                if (!isRepeat) {
                    a.smartGkEnabled = !a.smartGkEnabled;
                    if (a.smartGkEnabled) a.penaltyGkEnabled = false;
                    outAction = OverlayAction::AssistanceChanged;
                }
                break;

            case X88Item::PenaltyGk:
                if (!isRepeat) {
                    a.penaltyGkEnabled = !a.penaltyGkEnabled;
                    if (a.penaltyGkEnabled) a.smartGkEnabled = false;
                    outAction = OverlayAction::AssistanceChanged;
                }
                break;

            case X88Item::AntiOpMode:
                if (!isRepeat) {
                    a.antiOpMode = !a.antiOpMode;
                    outAction = OverlayAction::AssistanceChanged;
                }
                break;

            case X88Item::AntiFakeShot:
                if (!isRepeat) {
                    a.antiFakeShotFilter = !a.antiFakeShotFilter;
                    outAction = OverlayAction::AssistanceChanged;
                }
                break;

            case X88Item::FirstTouchSoft:
                if (!isRepeat) {
                    a.firstTouchCushionEnabled = !a.firstTouchCushionEnabled;
                    outAction = OverlayAction::AssistanceChanged;
                }
                break;

            case X88Item::FakeShot:
                a.fakeShotIntervalMs = std::clamp(a.fakeShotIntervalMs + dir * (fast ? 50 : 10), 50, 1000);
                outAction = OverlayAction::AssistanceChanged;
                break;

            case X88Item::ExtendFake:
                a.fakeShotExtendTicks = std::clamp(a.fakeShotExtendTicks + dir, 0, 15);
                outAction = OverlayAction::AssistanceChanged;
                break;

            case X88Item::FastFake:
                a.fastFakeSpeedTicks = std::clamp(a.fastFakeSpeedTicks + dir, 1, 10);
                outAction = OverlayAction::AssistanceChanged;
                break;

            case X88Item::AutoClick:
                a.realShotIntervalMs = std::clamp(a.realShotIntervalMs + dir * (fast ? 25 : 5), 0, 1000);
                outAction = OverlayAction::AssistanceChanged;
                break;

            case X88Item::AutoShaker:
                a.autoShakeFrequencyTicks = std::clamp(a.autoShakeFrequencyTicks + dir, 1, 12);
                outAction = OverlayAction::AssistanceChanged;
                break;

            case X88Item::MoonOrbit:
                a.ballOrbitDistance = std::clamp(a.ballOrbitDistance + dir, 0, 15);
                outAction = OverlayAction::AssistanceChanged;
                break;

            case X88Item::FakeTyping:
                if (!isRepeat) {
                    a.fakeTypingActive = !a.fakeTypingActive;
                    if (isMultiplayer) {
                        client.sendChatIndicator(a.fakeTypingActive);
                    }
                    outAction = OverlayAction::AssistanceChanged;
                }
                break;

            case X88Item::TypeStrobe:
                a.typingSpamSpeedMs = std::clamp(a.typingSpamSpeedMs + dir * (fast ? 25 : 5), 5, 500);
                outAction = OverlayAction::AssistanceChanged;
                break;

            case X88Item::Extrap:
                st.extrapolationMs = std::clamp(st.extrapolationMs + dir * (fast ? 25 : 5), -200, 500);
                outAction = OverlayAction::ExtrapolationChanged;
                break;

            default:
                break;
        }
        return true;
    }

    return false;
}

void CockpitMenu::drawX88Overlay(
    const GameRenderer& gr,
    const World* world,
    int pingMs,
    int fps,
    bool isSandbox,
    float dispW,
    float dispH
) {
    if (!showX88Hud) return;

    const auto& a = gr.assistant;
    const auto& st = gr.settings;

    uint8_t A = static_cast<uint8_t>(std::clamp(x88Opacity, 0.30f, 1.0f) * 255.0f);
    ImU32 colShadow = rgba(0, 0, 0, static_cast<uint8_t>(std::min(255, int(A) + 20)));
    ImU32 colHeader = rgba(125, 211, 252, A);
    ImU32 colName   = rgba(236, 242, 250, A);
    ImU32 colKey    = rgba(148, 163, 184, A);
    ImU32 colOn     = rgba(74, 222, 128, A);
    ImU32 colOff    = rgba(248, 113, 113, A);
    ImU32 colWarn   = rgba(250, 204, 21, A);
    ImU32 colDanger = rgba(251, 113, 133, A);
    ImU32 colInfo   = rgba(186, 230, 253, A);
    ImU32 colSel    = rgba(56, 189, 248, A);

    struct X88Row {
        X88Item id;
        std::string label;
        std::string key;
        std::string value;
        ImU32 valCol;
        bool isActive;
        int32_t swatchRgb = -1;
    };

    std::vector<X88Row> rows;
    rows.reserve(18);

    auto addBool = [&](X88Item id, const char* lbl, const char* key, bool on, const std::string& extra = "", bool showExtraWhenOn = true) {
        bool sel = (x88SelectedItem == id);
        if (x88ActiveOnly && !on && !sel) return;
        std::string val;
        if (on) {
            bool showEx = !extra.empty() && (showExtraWhenOn || sel);
            val = showEx ? ("ON " + extra) : "ON";
        } else {
            val = (sel && !extra.empty()) ? ("OFF " + extra) : "OFF";
        }
        rows.push_back({id, lbl, key, val, on ? colOn : colOff, on, -1});
    };

    {
        char agBuf[32];
        std::snprintf(agBuf, sizeof(agBuf), "(%d%% Accuracy)", std::clamp(a.autoGoalAccuracy, 30, 100));
        addBool(X88Item::AutoGoal, "Auto-Goal", "0", a.autoGoalEnabled, agBuf);
    }
    {
        bool sel = (x88SelectedItem == X88Item::AnyGoal);
        if (a.autoGoalAnyGoal || !x88ActiveOnly || sel) {
            if (a.autoGoalExcludeColor >= 0 && (a.autoGoalAnyGoal || sel)) {
                rows.push_back({
                    X88Item::AnyGoal, "10 Men Futsal (OFF IN RS)", "1",
                    a.autoGoalAnyGoal ? "ON (Excl)" : "OFF (Excl)",
                    a.autoGoalAnyGoal ? colOn : colOff,
                    a.autoGoalAnyGoal,
                    a.autoGoalExcludeColor & 0xFFFFFF
                });
            } else {
                addBool(X88Item::AnyGoal, "10 Men Futsal (OFF IN RS)", "1", a.autoGoalAnyGoal, "(All)", false);
            }
        }
    }

    addBool(X88Item::AutoSave,       "Auto-Save",      "6", a.autoSaveEnabled);
    addBool(X88Item::SmartGk,        "Smart GK",       "7", a.smartGkEnabled);
    addBool(X88Item::PenaltyGk,      "Penalty GK",     "8", a.penaltyGkEnabled);
    addBool(X88Item::AntiOpMode,     "Anti-OPMode",    "2", a.antiOpMode);
    addBool(X88Item::AntiFakeShot,   "Anti-FakeShot",  "3", a.antiFakeShotFilter);
    addBool(X88Item::FirstTouchSoft, "1st-Touch Soft", "4", a.firstTouchCushionEnabled);

    {
        char fsBuf[24];
        std::snprintf(fsBuf, sizeof(fsBuf), "(%d ms)", a.fakeShotIntervalMs);
        addBool(X88Item::FakeShot, "Fake Shot", "F", a.fakeShotEnabled, fsBuf, false);
    }

    {
        bool sel = (x88SelectedItem == X88Item::ExtendFake);
        if (a.fakeShotExtendEnabled || !x88ActiveOnly || sel) {
            int extT = std::clamp(a.fakeShotExtendTicks, 0, 15);
            char eb[40];
            ImU32 ec = colOff;
            if (!a.fakeShotExtendEnabled) {
                if (sel) {
                    if (extT == 0) std::snprintf(eb, sizeof(eb), "OFF (Auto)");
                    else if (extT <= 5) std::snprintf(eb, sizeof(eb), "OFF (+%d %s)", extT, (extT == 1 ? "Tick" : "Ticks"));
                    else std::snprintf(eb, sizeof(eb), "OFF (+%d Ticks!)", extT);
                } else {
                    std::snprintf(eb, sizeof(eb), "OFF");
                }
            } else {
                if (extT == 0) {
                    std::snprintf(eb, sizeof(eb), "ON (Auto)");
                    ec = colOn;
                } else if (extT <= 5) {
                    std::snprintf(eb, sizeof(eb), "ON (+%d %s)", extT, (extT == 1 ? "Tick" : "Ticks"));
                    ec = colWarn;
                } else {
                    std::snprintf(eb, sizeof(eb), "ON (+%d Ticks!)", extT);
                    ec = colDanger;
                }
            }
            rows.push_back({X88Item::ExtendFake, "Extend Fake", "5", eb, ec, a.fakeShotExtendEnabled, -1});
        }
    }

    {
        int ffT = std::clamp(a.fastFakeSpeedTicks, 1, 10);
        char ffBuf[28];
        std::snprintf(ffBuf, sizeof(ffBuf), "(%d %s)", ffT, (ffT == 1 ? "Tick" : "Ticks"));
        addBool(X88Item::FastFake, "Fast-Fake", "P", a.holdingFastFakeKey, ffBuf);
    }
    {
        char acBuf[24];
        std::snprintf(acBuf, sizeof(acBuf), "(%d ms)", std::max(0, a.realShotIntervalMs));
        addBool(X88Item::AutoClick, "Auto-Click", "N", a.holdingRealShotKey, acBuf);
    }
    {
        int shT = std::clamp(a.autoShakeFrequencyTicks, 1, 12);
        char shBuf[28];
        std::snprintf(shBuf, sizeof(shBuf), "(%d %s)", shT, (shT == 1 ? "Tick" : "Ticks"));
        addBool(X88Item::AutoShaker, "Auto-Shaker", "B", a.holdingShakeKey, shBuf, false);
    }
    {
        char obBuf[28];
        if (a.ballOrbitDistance <= 0) std::snprintf(obBuf, sizeof(obBuf), "(Surface)");
        else std::snprintf(obBuf, sizeof(obBuf), "(+%d Gap)", a.ballOrbitDistance);
        addBool(X88Item::MoonOrbit, "Moon Orbit", "O", a.holdingOrbitKey, obBuf, false);
    }
    addBool(X88Item::FakeTyping, "Fake Typing", "J", a.fakeTypingActive);
    {
        char tsBuf[24];
        std::snprintf(tsBuf, sizeof(tsBuf), "(%d ms)", a.typingSpamSpeedMs);
        addBool(X88Item::TypeStrobe, "Type Strobe", "U", a.typingSpamEnabled, tsBuf, false);
    }

    {
        char netBuf[32];
        std::snprintf(netBuf, sizeof(netBuf), "%+d ms", st.extrapolationMs);
        rows.push_back({X88Item::Extrap, "Extrap", "", netBuf, colInfo, true, -1});
    }

    ImGui::PushFont(fontSmall);
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    const float sc = std::clamp(x88Scale, 0.70f, 1.60f);
    const float fontSize = 13.0f * sc;
    const float lineH = 16.0f * sc;
    const float labelColW = 205.0f * sc;
    const float totalW = 320.0f * sc;
    const float extraFooterH = (x88SelectedItem != X88Item::None) ? (lineH + 4.0f * sc) : 0.0f;
    const float totalH = (static_cast<float>(rows.size()) + 1.4f) * lineH + extraFooterH;

    float x0 = 16.0f;
    float y0 = 54.0f;
    switch (x88Position % 4) {
        case 0:
            x0 = 16.0f;
            y0 = 54.0f;
            break;
        case 1:
            x0 = std::max(16.0f, dispW - totalW - 14.0f);
            y0 = 20.0f;
            break;
        case 2:
            x0 = 16.0f;
            y0 = std::max(54.0f, dispH - totalH - 165.0f);
            break;
        case 3:
            x0 = std::max(16.0f, dispW - totalW - 14.0f);
            y0 = std::max(54.0f, dispH - totalH - 16.0f);
            break;
    }

    auto measureTxt = [&](const char* txt) -> ImVec2 {
        return fontSmall->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, txt);
    };

    auto drawShadowText = [&](float x, float y, ImU32 col, const char* txt) {
        dl->AddText(fontSmall, fontSize, ImVec2(x + 1.0f, y + 1.0f), colShadow, txt);
        dl->AddText(fontSmall, fontSize, ImVec2(x, y), col, txt);
    };

    std::string roomLabel = (world && !world->roomName.empty())
        ? world->roomName
        : (isSandbox ? "Sandbox" : "Lobby");
    if (roomLabel.size() > 28) roomLabel = roomLabel.substr(0, 27) + "...";

    char hdrLeft[96];
    std::snprintf(hdrLeft, sizeof(hdrLeft), "x88 haxball  |  %s  |  ", roomLabel.c_str());
    drawShadowText(x0, y0, colHeader, hdrLeft);

    ImVec2 hlSz = measureTxt(hdrLeft);
    char fpsStr[24];
    std::snprintf(fpsStr, sizeof(fpsStr), "%d FPS  ", fps);
    drawShadowText(x0 + hlSz.x, y0, colName, fpsStr);

    ImVec2 fSz = measureTxt(fpsStr);
    char pingStr[24];
    std::snprintf(pingStr, sizeof(pingStr), "%d ms", pingMs);
    ImU32 pingCol = (pingMs < 50) ? colOn : ((pingMs < 100) ? colWarn : colOff);
    drawShadowText(x0 + hlSz.x + fSz.x, y0, pingCol, pingStr);

    float y = y0 + lineH + 3.0f * sc;

    for (const auto& r : rows) {
        bool sel = (r.id == x88SelectedItem);
        float lblX = x0;
        if (sel) {
            drawShadowText(x0, y, colSel, ">");
            lblX = x0 + 10.0f * sc;
        }

        drawShadowText(lblX, y, sel ? colSel : colName, r.label.c_str());
        if (x88ShowHotkeys && !r.key.empty()) {
            ImVec2 lsz = measureTxt(r.label.c_str());
            std::string kTag = "[" + r.key + "]";
            drawShadowText(lblX + lsz.x + 4.0f * sc, y, sel ? colHeader : colKey, kTag.c_str());
        }
        drawShadowText(x0 + labelColW - 10.0f * sc, y, sel ? colSel : colKey, ":");
        drawShadowText(x0 + labelColW, y, r.valCol, r.value.c_str());

        float endX = x0 + labelColW + measureTxt(r.value.c_str()).x;
        if (r.swatchRgb >= 0) {
            float swSz = 9.0f * sc;
            float sx = endX + 5.0f * sc;
            float sy = y + 3.0f * sc;
            ImU32 scCol = IM_COL32((r.swatchRgb >> 16) & 0xFF, (r.swatchRgb >> 8) & 0xFF, r.swatchRgb & 0xFF, A);
            dl->AddRectFilled(ImVec2(sx, sy), ImVec2(sx + swSz, sy + swSz), scCol, 2.0f);
            dl->AddRect(ImVec2(sx, sy), ImVec2(sx + swSz, sy + swSz), colShadow, 2.0f);
            endX = sx + swSz;
        }
        if (sel) {
            drawShadowText(endX + 6.0f * sc, y, colSel, "< >");
        }
        y += lineH;
    }

    if (x88SelectedItem != X88Item::None) {
        drawShadowText(x0, y + 3.0f * sc, colKey, "[< / >] Adjust   [Up/Dn] Select   [Esc] Done");
    }

    ImGui::PopFont();
}

OverlayAction CockpitMenu::render(
    GameRenderer& gr,
    HaxballClient& client,
    const World* world,
    int pingMs,
    int fps,
    bool isSandbox,
    int screenW,
    int screenH
) {
    OverlayAction action = OverlayAction::None;
    if (!initialized) return action;
    if (!gr.showSettingsPanel && !showX88Hud) {
        wasOpenLastFrame = false;
        return action;
    }

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 disp = io.DisplaySize;
    if (disp.x < 100.0f || disp.y < 100.0f) {
        disp = ImVec2(static_cast<float>(screenW), static_cast<float>(screenH));
    }

    drawX88Overlay(gr, world, pingMs, fps, isSandbox, disp.x, disp.y);

    if (gr.showSettingsPanel) {
        gTheme = buildTheme(lightMode, accentIdx, menuOpacity);

        int tab = std::clamp(gr.activeMenuTab, 0, 3);
        const float tabWidths[4]  = {540.0f, 650.0f, 540.0f, 540.0f};
        const float tabHeights[4] = {376.0f, 470.0f, 418.0f, 342.0f};
        float panelW = tabWidths[tab];
        float panelH = std::min(tabHeights[tab], std::max(300.0f, disp.y - 32.0f));

        ImGui::GetBackgroundDrawList()->AddRectFilled(
            ImVec2(0.0f, 0.0f),
            disp,
            lightMode ? rgba(15, 23, 42, 65) : rgba(0, 0, 0, 115)
        );

        ImGuiCond posCond = !wasOpenLastFrame ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
        ImGui::SetNextWindowPos(ImVec2((disp.x - panelW) * 0.5f, (disp.y - panelH) * 0.5f), posCond);
        ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_Always);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(gTheme.windowBg));
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(gTheme.border));
        ImGui::PushFont(fontRegular);

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse;

        if (ImGui::Begin("##HaxballSimpleMenu", nullptr, flags)) {
            ImVec2 winPos = ImGui::GetWindowPos();
            ImVec2 winSize = ImGui::GetWindowSize();

            float clampedX = std::clamp(winPos.x, 8.0f, std::max(8.0f, disp.x - winSize.x - 8.0f));
            float clampedY = std::clamp(winPos.y, 8.0f, std::max(8.0f, disp.y - winSize.y - 8.0f));
            if (clampedX != winPos.x || clampedY != winPos.y) {
                ImGui::SetWindowPos(ImVec2(clampedX, clampedY));
                winPos = ImVec2(clampedX, clampedY);
            }

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                !ImGui::IsMouseHoveringRect(winPos, ImVec2(winPos.x + winSize.x, winPos.y + winSize.y), false)) {
                gr.showSettingsPanel = false;
            }

            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float headerH = 40.0f;

            dl->AddRectFilled(winPos, ImVec2(winPos.x + winSize.x, winPos.y + headerH), gTheme.headerBg, 6.0f, ImDrawFlags_RoundCornersTop);
            dl->AddLine(ImVec2(winPos.x, winPos.y + headerH), ImVec2(winPos.x + winSize.x, winPos.y + headerH), gTheme.border, 1.0f);

            const char* tabNames[4] = {"Network  [F1]", "Assist  [F2]", "Camera  [F3]", "Menu"};
            const float padX = 12.0f;
            const float closeW = 26.0f;
            float tabsW = winSize.x - padX * 2.0f - closeW - 8.0f;
            float tabW = tabsW / 4.0f;

            ImGui::PushFont(fontBold);
            for (int i = 0; i < 4; ++i) {
                ImVec2 tp0(winPos.x + padX + i * tabW, winPos.y);
                ImVec2 tp1(tp0.x + tabW, winPos.y + headerH);
                ImGui::SetCursorScreenPos(tp0);
                char tid[24];
                std::snprintf(tid, sizeof(tid), "##htab_%d", i);
                bool clk = ImGui::InvisibleButton(tid, ImVec2(tabW, headerH));
                bool hov = ImGui::IsItemHovered();
                if (clk) {
                    gr.activeMenuTab = i;
                    tab = i;
                }
                bool sel = (tab == i);
                if (hov && !sel) {
                    dl->AddRectFilled(ImVec2(tp0.x + 2.0f, tp0.y + 4.0f), ImVec2(tp1.x - 2.0f, tp1.y - 4.0f), gTheme.cardHover, 4.0f);
                }
                if (sel) {
                    dl->AddRectFilled(ImVec2(tp0.x + 6.0f, tp1.y - 2.5f), ImVec2(tp1.x - 6.0f, tp1.y), gTheme.accent, 1.0f);
                }
                ImVec2 tsz = ImGui::CalcTextSize(tabNames[i]);
                ImU32 tc = sel ? gTheme.text : (hov ? gTheme.text : gTheme.textMuted);
                dl->AddText(ImVec2(tp0.x + (tabW - tsz.x) * 0.5f, tp0.y + (headerH - tsz.y) * 0.5f), tc, tabNames[i]);
            }
            ImGui::PopFont();

            ImGui::SetCursorScreenPos(ImVec2(winPos.x + winSize.x - padX - closeW, winPos.y + 7.0f));
            if (uiMiniBtn("##btn_close_menu", "X", closeW, 26.0f, false)) {
                gr.showSettingsPanel = false;
            }

            const float bodyW = winSize.x - padX * 2.0f;
            ImGui::SetCursorScreenPos(ImVec2(winPos.x + padX, winPos.y + headerH + 10.0f));
            ImGui::BeginGroup();

            switch (tab) {
                case 0: renderNetworkTab(gr, client, action, bodyW); break;
                case 1: renderAssistTab(gr, world, action, bodyW); break;
                case 2: renderCameraTab(gr, action, bodyW); break;
                case 3: renderMenuTab(action, bodyW); break;
            }

            ImGui::EndGroup();
        }
        ImGui::End();
        ImGui::PopFont();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
    }

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), sdlRenderer);

    SDL_SetRenderDrawBlendMode(sdlRenderer, SDL_BLENDMODE_BLEND);
    SDL_RenderSetClipRect(sdlRenderer, nullptr);

    wasOpenLastFrame = gr.showSettingsPanel;
    return action;
}

void CockpitMenu::renderNetworkTab(GameRenderer& gr, HaxballClient& client, OverlayAction& action, float W) {
    auto& st = gr.settings;
    float t = 0.0f;
    char buf[40];

    ImVec2 p1 = ImGui::GetCursorScreenPos();
    uiPanelBg(p1, W, 174.0f);
    float innerW = W - 16.0f;
    float ix = p1.x + 8.0f;
    float iy = p1.y + 8.0f;

    ImGui::SetCursorScreenPos(ImVec2(ix, iy));
    std::snprintf(buf, sizeof(buf), "%d ms", st.extrapolationMs);
    if (uiFlatSlider("##sl_extrap", "Extrapolation", buf, (st.extrapolationMs + 200.0f) / 700.0f, innerW, t, 118.0f, 64.0f)) {
        st.extrapolationMs = std::clamp((static_cast<int>(std::round(-200.0f + t * 700.0f)) / 5) * 5, -200, 500);
        action = OverlayAction::ExtrapolationChanged;
    }

    iy += 28.0f;
    const char* pLbl[7] = {"-25", "-5", "0 ms", "+5", "+25", "+60", "+120"};
    const int pVal[7] = {-25, -5, 0, 5, 25, 60, 120};
    const bool pDelta[7] = {true, true, false, true, true, false, false};
    float pbW = (innerW - 6.0f * 4.0f) / 7.0f;
    for (int i = 0; i < 7; ++i) {
        ImGui::SetCursorScreenPos(ImVec2(ix + i * (pbW + 4.0f), iy));
        char id[24];
        std::snprintf(id, sizeof(id), "##exp_%d", i);
        bool act = (!pDelta[i] && st.extrapolationMs == pVal[i]);
        if (uiMiniBtn(id, pLbl[i], pbW, 22.0f, act)) {
            st.extrapolationMs = std::clamp(pDelta[i] ? (st.extrapolationMs + pVal[i]) : pVal[i], -200, 500);
            action = OverlayAction::ExtrapolationChanged;
        }
    }

    iy += 29.0f;
    ImGui::SetCursorScreenPos(ImVec2(ix, iy));
    std::snprintf(buf, sizeof(buf), "%d ms", st.handicapMs);
    if (uiFlatSlider("##sl_hcap", "Ping Delay", buf, st.handicapMs / 250.0f, innerW, t, 118.0f, 64.0f)) {
        st.handicapMs = std::clamp((static_cast<int>(std::round(t * 250.0f)) / 5) * 5, 0, 250);
        action = OverlayAction::ExtrapolationChanged;
    }

    iy += 27.0f;
    ImGui::SetCursorScreenPos(ImVec2(ix, iy));
    float fpsNorm = (st.fpsCap <= 0 || st.fpsCap >= 500) ? 1.0f : ((st.fpsCap - 60.0f) / 440.0f);
    std::string fpsTxt = (st.fpsCap <= 0 || st.fpsCap >= 500) ? "Uncapped" : (std::to_string(st.fpsCap) + " FPS");
    if (uiFlatSlider("##sl_fps", "FPS Limit", fpsTxt.c_str(), fpsNorm, innerW, t, 118.0f, 64.0f)) {
        int raw = static_cast<int>(std::round(60.0f + t * 440.0f));
        st.fpsCap = (raw >= 495) ? 0 : (raw / 10) * 10;
        action = OverlayAction::AssistanceChanged;
    }

    iy += 31.0f;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(ix, iy - 4.0f), ImVec2(ix + innerW, iy - 4.0f), gTheme.border, 1.0f);
    float col3 = (innerW - 8.0f) / 3.0f;
    ImGui::SetCursorScreenPos(ImVec2(ix, iy));
    if (uiFlatToggle("##tg_vs", "VSync", "", st.vsync, col3)) action = OverlayAction::ToggleVSync;
    ImGui::SetCursorScreenPos(ImVec2(ix + col3 + 4.0f, iy));
    if (uiFlatToggle("##tg_ng", "NetGraph", "F8", st.showNetGraph, col3)) action = OverlayAction::AssistanceChanged;
    ImGui::SetCursorScreenPos(ImVec2(ix + (col3 + 4.0f) * 2.0f, iy));
    if (uiFlatToggle("##tg_gh", "Desync Ghost", "", st.showDesyncGhost, col3)) action = OverlayAction::AssistanceChanged;

    ImVec2 p2(p1.x, p1.y + 184.0f);
    uiPanelBg(p2, W, 128.0f);
    float iy2 = p2.y + 8.0f;

    ImGui::SetCursorScreenPos(ImVec2(ix, iy2));
    uiGroupLabel("Fake Ping (/fakeping)", innerW);
    iy2 += 20.0f;

    ImGui::SetCursorScreenPos(ImVec2(ix, iy2));
    const char* fpOpts[6] = {"Off", "Static", "Flash", "Random", "Wave", "Count"};
    auto applyFakePing = [&]() {
        switch (fakePingModeIdx) {
            case 0: client.setFakePingMs(std::nullopt); break;
            case 1: client.setFakePingMs(static_cast<uint32_t>(fakePingStaticMs)); break;
            case 2: client.setFakePingFlash({9999, 666, 420, 69}); break;
            case 3: client.setFakePingRange(HaxballClient::FakePingMode::Random, 10, 999); break;
            case 4: client.setFakePingRange(HaxballClient::FakePingMode::Wave, 10, 500); break;
            case 5: client.setFakePingRange(HaxballClient::FakePingMode::Count, 10, 999); break;
        }
        client.setFakePingIntervalMs(fakePingIntervalMs);
    };
    if (uiSegmented("##seg_fp", nullptr, fpOpts, 6, fakePingModeIdx, innerW)) {
        applyFakePing();
    }

    iy2 += 32.0f;
    ImGui::SetCursorScreenPos(ImVec2(ix, iy2));
    std::snprintf(buf, sizeof(buf), "%d ms", fakePingStaticMs);
    if (uiFlatSlider("##sl_fpms", "Static Ping", buf, fakePingStaticMs / 999.0f, innerW, t, 118.0f, 64.0f)) {
        fakePingStaticMs = std::clamp(static_cast<int>(std::round(t * 999.0f)), 0, 999);
        if (fakePingModeIdx != 1) fakePingModeIdx = 1;
        applyFakePing();
    }

    iy2 += 27.0f;
    ImGui::SetCursorScreenPos(ImVec2(ix, iy2));
    std::snprintf(buf, sizeof(buf), "%d ms", fakePingIntervalMs);
    if (uiFlatSlider("##sl_fpint", "Cycle Speed", buf, (fakePingIntervalMs - 150.0f) / 1850.0f, innerW, t, 118.0f, 64.0f)) {
        fakePingIntervalMs = std::clamp((static_cast<int>(std::round(150.0f + t * 1850.0f)) / 50) * 50, 150, 2000);
        client.setFakePingIntervalMs(fakePingIntervalMs);
    }
}

void CockpitMenu::renderAssistTab(GameRenderer& gr, const World* world, OverlayAction& action, float W) {
    auto& a = gr.assistant;
    float t = 0.0f;
    char buf[40];

    ImVec2 p1 = ImGui::GetCursorScreenPos();
    const float p1H = 200.0f;
    uiPanelBg(p1, W, p1H);

    const float pad = 8.0f;
    const float innerW = W - pad * 2.0f;
    const float colGap = 10.0f;
    const float colW = (innerW - colGap * 2.0f) / 3.0f;
    const float rowStep = 24.5f;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddLine(ImVec2(p1.x + pad + colW + colGap * 0.5f, p1.y + 8.0f),
                ImVec2(p1.x + pad + colW + colGap * 0.5f, p1.y + p1H - 8.0f), gTheme.border, 1.0f);
    dl->AddLine(ImVec2(p1.x + pad + colW * 2.0f + colGap * 1.5f, p1.y + 8.0f),
                ImVec2(p1.x + pad + colW * 2.0f + colGap * 1.5f, p1.y + p1H - 8.0f), gTheme.border, 1.0f);

    const float c0x = p1.x + pad;
    const float c1x = c0x + colW + colGap;
    const float c2x = c1x + colW + colGap;
    const float hdrY = p1.y + 6.0f;
    const float r0y = p1.y + 24.0f;

    dl->AddText(ImVec2(c0x + 7.0f, hdrY), gTheme.textDim, "Goal & GK");
    dl->AddText(ImVec2(c1x + 7.0f, hdrY), gTheme.textDim, "Feints & Filters");
    dl->AddText(ImVec2(c2x + 7.0f, hdrY), gTheme.textDim, "Lines & Tech");

    ImGui::SetCursorScreenPos(ImVec2(c0x, r0y + rowStep * 0));
    if (uiFlatToggle("##tg_ag",  "Auto-Goal",       "0", a.autoGoalEnabled,    colW)) action = OverlayAction::AssistanceChanged;
    ImGui::SetCursorScreenPos(ImVec2(c0x, r0y + rowStep * 1));
    if (uiFlatToggle("##tg_any", "10 Men Futsal (OFF IN RS)", "1", a.autoGoalAnyGoal, colW)) action = OverlayAction::AssistanceChanged;
    ImGui::SetCursorScreenPos(ImVec2(c0x, r0y + rowStep * 2));
    if (uiFlatToggle("##tg_as",  "Auto-Save",       "6", a.autoSaveEnabled,    colW)) action = OverlayAction::AssistanceChanged;
    ImGui::SetCursorScreenPos(ImVec2(c0x, r0y + rowStep * 3));
    bool prevSmart = a.smartGkEnabled;
    if (uiFlatToggle("##tg_sgk", "Smart GK",        "7", a.smartGkEnabled,     colW)) {
        if (a.smartGkEnabled && !prevSmart) a.penaltyGkEnabled = false;
        action = OverlayAction::AssistanceChanged;
    }
    ImGui::SetCursorScreenPos(ImVec2(c0x, r0y + rowStep * 4));
    bool prevPen = a.penaltyGkEnabled;
    if (uiFlatToggle("##tg_pgk", "Penalty GK",      "8", a.penaltyGkEnabled,   colW)) {
        if (a.penaltyGkEnabled && !prevPen) a.smartGkEnabled = false;
        action = OverlayAction::AssistanceChanged;
    }
    ImGui::SetCursorScreenPos(ImVec2(c0x, r0y + rowStep * 5));
    if (uiFlatToggle("##tg_gkc", "GK Danger Cone",  "",  a.showGkDangerCone,   colW)) action = OverlayAction::AssistanceChanged;
    ImGui::SetCursorScreenPos(ImVec2(c0x, r0y + rowStep * 6));
    if (uiFlatToggle("##tg_ogc", "Other Cones",     "",  a.showOtherGoalsCone, colW)) action = OverlayAction::AssistanceChanged;

    ImGui::SetCursorScreenPos(ImVec2(c1x, r0y + rowStep * 0));
    if (uiFlatToggle("##tg_fk",  "Fake Shot",       "F", a.fakeShotEnabled,          colW)) action = OverlayAction::AssistanceChanged;
    ImGui::SetCursorScreenPos(ImVec2(c1x, r0y + rowStep * 1));
    if (uiFlatToggle("##tg_fke", "Extend Fake",     "5", a.fakeShotExtendEnabled,    colW)) action = OverlayAction::AssistanceChanged;
    ImGui::SetCursorScreenPos(ImVec2(c1x, r0y + rowStep * 2));
    if (uiFlatToggle("##tg_aop", "Anti-OPMode",     "2", a.antiOpMode,               colW)) action = OverlayAction::AssistanceChanged;
    ImGui::SetCursorScreenPos(ImVec2(c1x, r0y + rowStep * 3));
    if (uiFlatToggle("##tg_af",  "Anti-FakeShot",   "3", a.antiFakeShotFilter,       colW)) action = OverlayAction::AssistanceChanged;
    ImGui::SetCursorScreenPos(ImVec2(c1x, r0y + rowStep * 4));
    if (uiFlatToggle("##tg_ftc", "1st-Touch Soft",  "4", a.firstTouchCushionEnabled, colW)) action = OverlayAction::AssistanceChanged;

    ImGui::SetCursorScreenPos(ImVec2(c2x, r0y + rowStep * 0));
    if (uiFlatToggle("##tg_aim", "Live Aim Line",   "",  a.showLiveAimTrajectory,  colW)) action = OverlayAction::AssistanceChanged;
    ImGui::SetCursorScreenPos(ImVec2(c2x, r0y + rowStep * 1));
    if (uiFlatToggle("##tg_rol", "Ball Roll Line",  "",  a.showBallRollTrajectory, colW)) action = OverlayAction::AssistanceChanged;
    ImGui::SetCursorScreenPos(ImVec2(c2x, r0y + rowStep * 2));
    if (uiFlatToggle("##tg_rs",  "Real Soccer",     "",  a.rsHelperEnabled,        colW)) action = OverlayAction::AssistanceChanged;
    ImGui::SetCursorScreenPos(ImVec2(c2x, r0y + rowStep * 3));
    if (uiFlatToggle("##tg_ftt", "Fake Typing",     "J", a.fakeTypingActive,       colW)) action = OverlayAction::AssistanceChanged;
    ImGui::SetCursorScreenPos(ImVec2(c2x, r0y + rowStep * 4));
    if (uiFlatToggle("##tg_ts",  "Type Strobe",     "U", a.typingSpamEnabled,      colW)) action = OverlayAction::AssistanceChanged;

    ImVec2 pEx(p1.x, p1.y + p1H + 8.0f);
    const float exH = 32.0f;
    uiPanelBg(pEx, W, exH);
    {
        const char* lbl = "My Goal (Exclude):";
        ImVec2 lsz = ImGui::CalcTextSize(lbl);
        dl->AddText(ImVec2(pEx.x + 12.0f, pEx.y + (exH - lsz.y) * 0.5f), gTheme.textMuted, lbl);

        float cx = pEx.x + 132.0f;
        float cy = pEx.y + 6.0f;
        float ch = 20.0f;

        ImGui::SetCursorScreenPos(ImVec2(cx, cy));
        if (uiMiniBtn("##ex_none", "All", 36.0f, ch, a.autoGoalExcludeColor < 0)) {
            a.autoGoalExcludeColor = -1;
            action = OverlayAction::AssistanceChanged;
        }
        cx += 42.0f;

        std::vector<uint32_t> goalColors;
        if (world && world->stadium) {
            goalColors = TacticalAssistant::getAvailableGoalColors(*world->stadium);
        }

        if (goalColors.empty()) {
            dl->AddText(ImVec2(cx, pEx.y + (exH - lsz.y) * 0.5f), gTheme.textDim, "Join match to detect goals");
        } else {
            for (size_t i = 0; i < goalColors.size(); ++i) {
                uint32_t c = goalColors[i] & 0xFFFFFFu;
                bool sel = (a.autoGoalExcludeColor == static_cast<int32_t>(c));
                float swW = 28.0f;
                if (cx + swW > pEx.x + W - 70.0f) break;

                ImGui::SetCursorScreenPos(ImVec2(cx, cy));
                char sid[32];
                std::snprintf(sid, sizeof(sid), "##gsw_%u", static_cast<unsigned>(i));
                bool clk = ImGui::InvisibleButton(sid, ImVec2(swW, ch));
                bool hov = ImGui::IsItemHovered();
                if (clk) {
                    if (sel) {
                        a.autoGoalExcludeColor = -1;
                    } else {
                        a.autoGoalExcludeColor = static_cast<int32_t>(c);
                        a.autoGoalAnyGoal = true;
                    }
                    action = OverlayAction::AssistanceChanged;
                }

                ImU32 fill = IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255);
                ImVec2 s0(cx, cy), s1(cx + swW, cy + ch);
                dl->AddRectFilled(s0, s1, fill, 3.0f);
                dl->AddRect(s0, s1, sel ? gTheme.text : (hov ? gTheme.borderStrong : rgba(0, 0, 0, 120)), 3.0f, 0, sel ? 2.0f : 1.0f);
                if (sel) {
                    dl->AddLine(ImVec2(s0.x + 6.0f, s0.y + 4.0f), ImVec2(s1.x - 6.0f, s1.y - 4.0f), rgba(239, 68, 68, 255), 2.2f);
                    dl->AddLine(ImVec2(s1.x - 6.0f, s0.y + 4.0f), ImVec2(s0.x + 6.0f, s1.y - 4.0f), rgba(239, 68, 68, 255), 2.2f);
                }
                cx += swW + 6.0f;
            }
        }

        char hexBuf[16];
        ImU32 hexCol = gTheme.textDim;
        if (a.autoGoalExcludeColor >= 0) {
            uint32_t c = static_cast<uint32_t>(a.autoGoalExcludeColor) & 0xFFFFFFu;
            std::snprintf(hexBuf, sizeof(hexBuf), "#%06X", c);
            hexCol = IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255);
        } else {
            std::snprintf(hexBuf, sizeof(hexBuf), "None");
        }
        ImVec2 hsz = ImGui::CalcTextSize(hexBuf);
        dl->AddText(ImVec2(pEx.x + W - hsz.x - 10.0f, pEx.y + (exH - hsz.y) * 0.5f), hexCol, hexBuf);
    }

    ImVec2 p2(p1.x, pEx.y + exH + 8.0f);
    const float p2H = 160.0f;
    uiPanelBg(p2, W, p2H);

    const float sGap = 12.0f;
    const float sColW = (innerW - sGap) * 0.5f;
    const float sx0 = p2.x + pad;
    const float sx1 = sx0 + sColW + sGap;
    const float sy0 = p2.y + 7.0f;
    const float sStep = 24.5f;

    dl->AddLine(ImVec2(sx0 + sColW + sGap * 0.5f, p2.y + 8.0f),
                ImVec2(sx0 + sColW + sGap * 0.5f, p2.y + p2H - 8.0f), gTheme.border, 1.0f);

    ImGui::SetCursorScreenPos(ImVec2(sx0, sy0 + sStep * 0));
    int agAcc = std::clamp(a.autoGoalAccuracy, 30, 100);
    std::snprintf(buf, sizeof(buf), "%d%%", agAcc);
    if (uiFlatSlider("##sl_agacc", "Auto-Goal Acc", buf, (agAcc - 30.0f) / 70.0f, sColW, t)) {
        a.autoGoalAccuracy = 30 + static_cast<int>(std::round(t * 70.0f));
        action = OverlayAction::AssistanceChanged;
    }

    ImGui::SetCursorScreenPos(ImVec2(sx0, sy0 + sStep * 1));
    std::snprintf(buf, sizeof(buf), "%d ms", a.fakeShotIntervalMs);
    if (uiFlatSlider("##sl_fkcd", "Fake Cooldown", buf, (a.fakeShotIntervalMs - 50.0f) / 950.0f, sColW, t)) {
        a.fakeShotIntervalMs = std::clamp((static_cast<int>(std::round(50.0f + t * 950.0f)) / 10) * 10, 50, 1000);
        action = OverlayAction::AssistanceChanged;
    }

    ImGui::SetCursorScreenPos(ImVec2(sx0, sy0 + sStep * 2));
    int extT = std::clamp(a.fakeShotExtendTicks, 0, 15);
    ImU32 extCol = (extT == 0) ? kAccentPalette[0] : ((extT <= 5) ? kAccentPalette[3] : kAccentPalette[4]);
    if (extT == 0) std::snprintf(buf, sizeof(buf), "Auto Safe");
    else if (extT <= 5) std::snprintf(buf, sizeof(buf), "+%dt Safe", extT);
    else std::snprintf(buf, sizeof(buf), "+%dt Unsafe!", extT);
    if (uiFlatSlider("##sl_fke", "Extend Hold", buf, extT / 15.0f, sColW, t, 104.0f, 74.0f, extCol)) {
        a.fakeShotExtendTicks = std::clamp(static_cast<int>(std::round(t * 15.0f)), 0, 15);
        action = OverlayAction::AssistanceChanged;
    }

    ImGui::SetCursorScreenPos(ImVec2(sx0, sy0 + sStep * 3));
    if (a.fastFakeSpeedTicks <= 1) std::snprintf(buf, sizeof(buf), "1t Hyper");
    else std::snprintf(buf, sizeof(buf), "%d ticks", a.fastFakeSpeedTicks);
    if (uiFlatSlider("##sl_ff", "Fast-Fake [P]", buf, (a.fastFakeSpeedTicks - 1.0f) / 9.0f, sColW, t)) {
        a.fastFakeSpeedTicks = std::clamp(1 + static_cast<int>(std::round(t * 9.0f)), 1, 10);
        action = OverlayAction::AssistanceChanged;
    }

    ImGui::SetCursorScreenPos(ImVec2(sx0, sy0 + sStep * 4));
    if (a.realShotIntervalMs <= 0) std::snprintf(buf, sizeof(buf), "0ms Max");
    else std::snprintf(buf, sizeof(buf), "%d ms", a.realShotIntervalMs);
    if (uiFlatSlider("##sl_ac", "Auto-Click [N]", buf, a.realShotIntervalMs / 1000.0f, sColW, t)) {
        int raw = static_cast<int>(std::round(t * 1000.0f));
        a.realShotIntervalMs = (raw <= 5) ? 0 : (raw / 5) * 5;
        action = OverlayAction::AssistanceChanged;
    }

    ImGui::SetCursorScreenPos(ImVec2(sx0, sy0 + sStep * 5));
    std::snprintf(buf, sizeof(buf), "%d%%", a.slowSpeedPercent);
    if (uiFlatSlider("##sl_slw", "Slow Speed [M]", buf, (a.slowSpeedPercent - 10.0f) / 80.0f, sColW, t)) {
        a.slowSpeedPercent = std::clamp(10 + static_cast<int>(std::round(t * 80.0f)), 10, 90);
        action = OverlayAction::AssistanceChanged;
    }

    ImGui::SetCursorScreenPos(ImVec2(sx1, sy0 + sStep * 0));
    int aimD = std::clamp(a.aimLineDistance, 20, 300);
    std::snprintf(buf, sizeof(buf), "%d u", aimD);
    if (uiFlatSlider("##sl_aimd", "Aim Distance", buf, (aimD - 20.0f) / 280.0f, sColW, t)) {
        a.aimLineDistance = std::clamp((static_cast<int>(std::round(20.0f + t * 280.0f)) / 5) * 5, 20, 300);
        action = OverlayAction::AssistanceChanged;
    }

    ImGui::SetCursorScreenPos(ImVec2(sx1, sy0 + sStep * 1));
    std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::round(a.lineOpacity * 100.0f)));
    if (uiFlatSlider("##sl_lop", "Line Opacity", buf, (a.lineOpacity - 0.10f) / 0.90f, sColW, t)) {
        a.lineOpacity = std::clamp(0.10f + t * 0.90f, 0.10f, 1.0f);
        action = OverlayAction::AssistanceChanged;
    }

    ImGui::SetCursorScreenPos(ImVec2(sx1, sy0 + sStep * 2));
    std::snprintf(buf, sizeof(buf), "%d ticks", a.trajectoryPreviewTicks);
    if (uiFlatSlider("##sl_lk", "Lookahead", buf, (a.trajectoryPreviewTicks - 30.0f) / 150.0f, sColW, t)) {
        a.trajectoryPreviewTicks = std::clamp(static_cast<int>(std::round(30.0f + t * 150.0f)), 30, 180);
        action = OverlayAction::AssistanceChanged;
    }

    ImGui::SetCursorScreenPos(ImVec2(sx1, sy0 + sStep * 3));
    std::snprintf(buf, sizeof(buf), "%d ticks", a.autoShakeFrequencyTicks);
    if (uiFlatSlider("##sl_shk", "Shake Cadence", buf, (a.autoShakeFrequencyTicks - 1.0f) / 11.0f, sColW, t)) {
        a.autoShakeFrequencyTicks = std::clamp(1 + static_cast<int>(std::round(t * 11.0f)), 1, 12);
        action = OverlayAction::AssistanceChanged;
    }

    ImGui::SetCursorScreenPos(ImVec2(sx1, sy0 + sStep * 4));
    if (a.ballOrbitDistance <= 0) std::snprintf(buf, sizeof(buf), "Surface");
    else std::snprintf(buf, sizeof(buf), "+%d gap", a.ballOrbitDistance);
    if (uiFlatSlider("##sl_orb", "Orbit Gap [O]", buf, a.ballOrbitDistance / 15.0f, sColW, t)) {
        a.ballOrbitDistance = std::clamp(static_cast<int>(std::round(t * 15.0f)), 0, 15);
        action = OverlayAction::AssistanceChanged;
    }

    ImGui::SetCursorScreenPos(ImVec2(sx1, sy0 + sStep * 5));
    std::snprintf(buf, sizeof(buf), "%d ms", a.typingSpamSpeedMs);
    if (uiFlatSlider("##sl_ts", "Strobe [U]", buf, (a.typingSpamSpeedMs - 5.0f) / 495.0f, sColW, t)) {
        a.typingSpamSpeedMs = std::clamp(5 + static_cast<int>(std::round(t * 495.0f)), 5, 500);
        action = OverlayAction::AssistanceChanged;
    }
}

void CockpitMenu::renderCameraTab(GameRenderer& gr, OverlayAction& action, float W) {
    auto& st = gr.settings;
    float t = 0.0f;
    char buf[40];

    ImVec2 p1 = ImGui::GetCursorScreenPos();
    uiPanelBg(p1, W, 124.0f);
    float innerW = W - 16.0f;
    float ix = p1.x + 8.0f;
    float iy = p1.y + 8.0f;

    ImGui::SetCursorScreenPos(ImVec2(ix, iy));
    const char* camOpts[4] = {"Adaptive", "Player", "Ball", "Full Pitch"};
    int cam = st.cameraMode % 4;
    if (uiSegmented("##seg_cam", "Camera [V]", camOpts, 4, cam, innerW)) {
        st.cameraMode = cam;
        action = OverlayAction::AssistanceChanged;
    }

    iy += 30.0f;
    ImGui::SetCursorScreenPos(ImVec2(ix, iy));
    const char* rotOpts[5] = {"0°", "90° Up", "180°", "270° Dn", "Auto Up"};
    int rot = st.fieldRotation % 5;
    if (uiSegmented("##seg_rot", "Rotation [R]", rotOpts, 5, rot, innerW)) {
        st.fieldRotation = rot;
        action = OverlayAction::AssistanceChanged;
    }

    iy += 31.0f;
    ImGui::SetCursorScreenPos(ImVec2(ix, iy));
    std::snprintf(buf, sizeof(buf), "%.2fx", gr.getZoom());
    if (uiFlatSlider("##sl_zoom", "Zoom", buf, (gr.getZoom() - 0.45f) / 2.15f, innerW, t, 114.0f, 56.0f)) {
        gr.setZoom(0.45f + t * 2.15f);
        action = OverlayAction::AssistanceChanged;
    }

    iy += 26.0f;
    ImGui::SetCursorScreenPos(ImVec2(ix, iy));
    std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::round(st.cameraSmoothness * 100.0f)));
    if (uiFlatSlider("##sl_sm", "Smoothness", buf, (st.cameraSmoothness - 0.05f) / 0.95f, innerW, t, 114.0f, 56.0f)) {
        st.cameraSmoothness = std::clamp(0.05f + t * 0.95f, 0.05f, 1.0f);
        action = OverlayAction::AssistanceChanged;
    }

    ImVec2 p2(p1.x, p1.y + 132.0f);
    uiPanelBg(p2, W, 224.0f);
    float iy2 = p2.y + 8.0f;

    ImGui::SetCursorScreenPos(ImVec2(ix, iy2));
    const char* themes[6] = {"Auto", "Grass", "Ice", "Neon", "Asphalt", "Gold"};
    int th = st.pitchTheme % 6;
    if (uiSegmented("##seg_th", "Pitch Theme", themes, 6, th, innerW)) {
        st.pitchTheme = th;
        action = OverlayAction::AssistanceChanged;
    }

    iy2 += 30.0f;
    ImGui::SetCursorScreenPos(ImVec2(ix, iy2));
    const char* trails[6] = {"Off", "Team", "Comet", "Fire", "White", "Rainbow"};
    int tr = st.ballTrailStyle % 6;
    if (uiSegmented("##seg_tr", "Ball Trail", trails, 6, tr, innerW)) {
        st.ballTrailStyle = tr;
        action = OverlayAction::AssistanceChanged;
    }

    iy2 += 30.0f;
    float col3 = (innerW - 8.0f) / 3.0f;
    ImGui::SetCursorScreenPos(ImVec2(ix, iy2));
    if (uiFlatToggle("##tg_tc", "Jerseys", "F4", st.showTeamColors, col3)) action = OverlayAction::AssistanceChanged;
    ImGui::SetCursorScreenPos(ImVec2(ix + col3 + 4.0f, iy2));
    if (uiFlatToggle("##tg_id", "IDs", "F5", st.showPlayerIds, col3)) action = OverlayAction::AssistanceChanged;
    ImGui::SetCursorScreenPos(ImVec2(ix + (col3 + 4.0f) * 2.0f, iy2));
    if (uiFlatToggle("##tg_av", "Avatars", "F6", st.showAvatars, col3)) action = OverlayAction::AssistanceChanged;

    iy2 += 26.0f;
    float col2 = (innerW - 6.0f) * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(ix, iy2));
    if (uiFlatToggle("##tg_hl", "Local Halo", "F7", st.showLocalHalo, col2)) action = OverlayAction::AssistanceChanged;
    ImGui::SetCursorScreenPos(ImVec2(ix + col2 + 6.0f, iy2));
    if (uiFlatToggle("##tg_iw", "Invisible Walls", "F9", st.showInvisibleSegments, col2)) action = OverlayAction::AssistanceChanged;

    iy2 += 28.0f;
    ImGui::SetCursorScreenPos(ImVec2(ix, iy2));
    std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::round(st.ballTrailOpacity * 100.0f)));
    if (uiFlatSlider("##sl_trop", "Trail Opacity", buf, (st.ballTrailOpacity - 0.15f) / 0.85f, col2, t, 88.0f, 44.0f)) {
        st.ballTrailOpacity = std::clamp(0.15f + t * 0.85f, 0.15f, 1.0f);
        action = OverlayAction::AssistanceChanged;
    }
    ImGui::SetCursorScreenPos(ImVec2(ix + col2 + 6.0f, iy2));
    std::snprintf(buf, sizeof(buf), "%.1fpx", st.discOutlineWidth);
    if (uiFlatSlider("##sl_ow", "Outline", buf, (st.discOutlineWidth - 1.0f) / 3.5f, col2, t, 64.0f, 46.0f)) {
        st.discOutlineWidth = std::clamp(1.0f + t * 3.5f, 1.0f, 4.5f);
        action = OverlayAction::AssistanceChanged;
    }

    iy2 += 25.0f;
    ImGui::SetCursorScreenPos(ImVec2(ix, iy2));
    std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::round(st.localHaloOpacity * 100.0f)));
    if (uiFlatSlider("##sl_hlo", "Halo Glow", buf, (st.localHaloOpacity - 0.10f) / 0.90f, innerW, t, 114.0f, 56.0f)) {
        st.localHaloOpacity = std::clamp(0.10f + t * 0.90f, 0.10f, 1.0f);
        action = OverlayAction::AssistanceChanged;
    }

    iy2 += 30.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float eGap = 4.0f;
    float eW = (innerW - eGap * 13.0f) / 14.0f;
    float eH = 30.0f;
    for (int i = 0; i < 14; ++i) {
        ImVec2 e0(ix + i * (eW + eGap), iy2);
        ImVec2 e1(e0.x + eW, e0.y + eH);
        ImGui::SetCursorScreenPos(e0);
        char eid[24];
        std::snprintf(eid, sizeof(eid), "##em_%d", i);
        bool clk = ImGui::InvisibleButton(eid, ImVec2(eW, eH));
        bool hov = ImGui::IsItemHovered();
        bool sel = (st.pendingAvatarChange == kQuickEmojiList[i]);
        if (clk) {
            st.pendingAvatarChange = kQuickEmojiList[i];
            action = OverlayAction::AvatarChanged;
        }
        dl->AddRectFilled(e0, e1, sel ? withAlpha(gTheme.accent, 55) : (hov ? gTheme.cardHover : gTheme.trackBg), 4.0f);
        if (sel) dl->AddRect(e0, e1, gTheme.accent, 4.0f, 0, 1.5f);
        if (emojiTextures[i]) {
            float ico = 20.0f;
            ImVec2 i0(e0.x + (eW - ico) * 0.5f, e0.y + (eH - ico) * 0.5f);
            dl->AddImage((ImTextureID)emojiTextures[i], i0, ImVec2(i0.x + ico, i0.y + ico));
        }
    }
}

void CockpitMenu::renderMenuTab(OverlayAction& action, float W) {
    float t = 0.0f;
    char buf[32];

    ImVec2 p1 = ImGui::GetCursorScreenPos();
    uiPanelBg(p1, W, 102.0f);
    float innerW = W - 16.0f;
    float ix = p1.x + 8.0f;
    float iy = p1.y + 8.0f;

    ImGui::SetCursorScreenPos(ImVec2(ix, iy));
    const char* modeOpts[2] = {"Dark", "Light"};
    int modeIdx = lightMode ? 1 : 0;
    if (uiSegmented("##seg_theme", "Theme", modeOpts, 2, modeIdx, innerW)) {
        lightMode = (modeIdx == 1);
        action = OverlayAction::AssistanceChanged;
    }

    iy += 30.0f;
    ImGui::SetCursorScreenPos(ImVec2(ix, iy));
    const char* accNames[5] = {"Emerald", "Sky", "Violet", "Amber", "Rose"};
    if (uiSegmented("##seg_acc", "Accent", accNames, 5, accentIdx, innerW)) {
        action = OverlayAction::AssistanceChanged;
    }

    iy += 31.0f;
    ImGui::SetCursorScreenPos(ImVec2(ix, iy));
    std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::round(menuOpacity * 100.0f)));
    if (uiFlatSlider("##sl_mop", "Menu Opacity", buf, (menuOpacity - 0.65f) / 0.35f, innerW, t, 114.0f, 56.0f)) {
        menuOpacity = std::clamp(0.65f + t * 0.35f, 0.65f, 1.0f);
        action = OverlayAction::AssistanceChanged;
    }

    ImVec2 p2(p1.x, p1.y + 112.0f);
    uiPanelBg(p2, W, 168.0f);
    float iy2 = p2.y + 8.0f;

    ImGui::SetCursorScreenPos(ImVec2(ix, iy2));
    uiGroupLabel("x88 Overlay", innerW);
    iy2 += 20.0f;

    float col3 = (innerW - 8.0f) / 3.0f;
    ImGui::SetCursorScreenPos(ImVec2(ix, iy2));
    if (uiFlatToggle("##tg_x88", "Enable x88", "", showX88Hud, col3)) action = OverlayAction::AssistanceChanged;
    ImGui::SetCursorScreenPos(ImVec2(ix + col3 + 4.0f, iy2));
    if (uiFlatToggle("##tg_x88k", "Shortcuts", "", x88ShowHotkeys, col3)) action = OverlayAction::AssistanceChanged;
    ImGui::SetCursorScreenPos(ImVec2(ix + (col3 + 4.0f) * 2.0f, iy2));
    if (uiFlatToggle("##tg_x88a", "Active Only", "", x88ActiveOnly, col3)) action = OverlayAction::AssistanceChanged;

    iy2 += 32.0f;
    ImGui::SetCursorScreenPos(ImVec2(ix, iy2));
    const char* posOpts[4] = {"Top-Left", "Top-Right", "Bot-Left", "Bot-Right"};
    int pos = x88Position % 4;
    if (uiSegmented("##seg_x88pos", "Corner", posOpts, 4, pos, innerW)) {
        x88Position = pos;
        action = OverlayAction::AssistanceChanged;
    }

    iy2 += 32.0f;
    ImGui::SetCursorScreenPos(ImVec2(ix, iy2));
    std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::round(x88Opacity * 100.0f)));
    if (uiFlatSlider("##sl_x88op", "HUD Opacity", buf, (x88Opacity - 0.30f) / 0.70f, innerW, t, 114.0f, 56.0f)) {
        x88Opacity = std::clamp(0.30f + t * 0.70f, 0.30f, 1.0f);
        action = OverlayAction::AssistanceChanged;
    }

    iy2 += 28.0f;
    ImGui::SetCursorScreenPos(ImVec2(ix, iy2));
    std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::round(x88Scale * 100.0f)));
    if (uiFlatSlider("##sl_x88sc", "HUD Size", buf, (x88Scale - 0.70f) / 0.90f, innerW, t, 114.0f, 56.0f)) {
        x88Scale = std::clamp(0.70f + t * 0.90f, 0.70f, 1.60f);
        action = OverlayAction::AssistanceChanged;
    }
}

}
