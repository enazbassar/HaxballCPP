#pragma once

#include <SDL2/SDL.h>
#include <array>
#include <cstdint>
#include <string>
#include "../render/renderer.hpp"
#include "../net/haxball_client.hpp"
#include "../physics/world.hpp"

struct ImFont;

namespace haxball {

class CockpitMenu {
public:
    enum class X88Item : int {
        None = -1,
        AutoGoal = 0,
        AnyGoal,
        AutoSave,
        SmartGk,
        PenaltyGk,
        AntiOpMode,
        AntiFakeShot,
        FirstTouchSoft,
        FakeShot,
        ExtendFake,
        FastFake,
        AutoClick,
        AutoShaker,
        MoonOrbit,
        FakeTyping,
        TypeStrobe,
        Extrap,
        Count
    };

    CockpitMenu();
    ~CockpitMenu();

    bool init(SDL_Window* window, SDL_Renderer* renderer);
    void shutdown();

    bool isInitialized() const { return initialized; }
    bool shouldRenderOverlay() const { return initialized && showX88Hud; }

    bool processEvent(const SDL_Event& rawEvent, bool menuOpen);

    bool isX88EditActive() const { return x88SelectedItem != X88Item::None; }
    void clearX88Selection() { x88SelectedItem = X88Item::None; }
    bool handleX88KeyDown(
        SDL_Keycode sym,
        SDL_Keymod mods,
        bool isRepeat,
        GameRenderer& gr,
        HaxballClient& client,
        const World* world,
        bool isMultiplayer,
        OverlayAction& outAction
    );

    OverlayAction render(
        GameRenderer& gr,
        HaxballClient& client,
        const World* world,
        int pingMs,
        int fps,
        bool isSandbox,
        int screenW,
        int screenH
    );

    bool lightMode = false;
    int accentIdx = 0;
    float menuOpacity = 0.96f;
    bool showX88Hud = true;
    int x88Position = 0;
    bool x88ActiveOnly = false;
    bool x88ShowHotkeys = true;
    float x88Opacity = 0.92f;
    float x88Scale = 1.00f;

private:
    void loadEmojiTextures();
    void destroyEmojiTextures();

    void drawX88Overlay(const GameRenderer& gr, const World* world, int pingMs, int fps, bool isSandbox, float dispW, float dispH);

    void renderNetworkTab(GameRenderer& gr, HaxballClient& client, OverlayAction& action, float W);
    void renderAssistTab(GameRenderer& gr, const World* world, OverlayAction& action, float W);
    void renderCameraTab(GameRenderer& gr, OverlayAction& action, float W);
    void renderMenuTab(OverlayAction& action, float W);

    SDL_Window* sdlWindow = nullptr;
    SDL_Renderer* sdlRenderer = nullptr;
    bool initialized = false;
    bool wasOpenLastFrame = false;

    ImFont* fontRegular = nullptr;
    ImFont* fontBold = nullptr;
    ImFont* fontSmall = nullptr;
    std::array<SDL_Texture*, 14> emojiTextures{};

    int fakePingModeIdx = 0;
    int fakePingStaticMs = 69;
    int fakePingIntervalMs = 500;

    X88Item x88SelectedItem = X88Item::None;
};

}
