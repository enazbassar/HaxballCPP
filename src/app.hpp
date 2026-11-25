#pragma once

#include <SDL2/SDL.h>
#include "net/haxball_client.hpp"
#include "physics/world.hpp"
#include "render/renderer.hpp"
#include "ui/room_browser.hpp"
#include "ui/cockpit_menu.hpp"
#include "audio/sound.hpp"
#include <memory>
#include <string>
#include <deque>
#include <mutex>
#include <vector>

namespace haxball {

enum class AppState {
    Browser,
    Connecting,
    Multiplayer,
    Sandbox
};

class App {
public:
    App();
    ~App();

    bool init(const std::string& autoRoom = "", bool autoSandbox = false, bool testMode = false, bool verbose = false);
    void run();

private:
    void handleEvents();
    void drainClientEvents();
    void processServerTickAssistant(double frameDt);
    void update(double dt);
    void render(double frameDt);
    void handleOverlayAction(OverlayAction action);
    void syncSettingsToConfig();

    void startMultiplayer(const std::string& roomId, const std::string& password = "");
    void startSandbox();
    void startPenaltyPractice();
    void resetPenaltyRound();
    void leaveToBrowser();

    bool testMode = false;
    bool verbose = false;
    double testTimer = 0.0;
    double connectedTestTimer = 0.0;
    uint32_t initialConnectedFrame = 0;

    SDL_Window* window = nullptr;
    SDL_Renderer* sdlRenderer = nullptr;
    bool isRunning = true;

    AppState state = AppState::Browser;

    RoomBrowser browser;
    GameRenderer gameRenderer;
    CockpitMenu cockpitMenu;
    FontRenderer connectingFont;
    SoundManager sound;
    HaxballClient client;

    std::shared_ptr<World> sandboxWorld;
    std::shared_ptr<World> sandboxRenderWorld;
    std::deque<World> sandboxHistory;

    struct ClientUiEvent {
        enum class Type { Connected, Disconnected, Chat, Announcement, GoalReset };
        Type type = Type::Connected;
        std::string sender;
        std::string text;
        uint32_t color = 0xFFFFFF;
        int team = 0;
        bool isAdmin = false;
        uint8_t style = 0;
        uint8_t sound = 0;
    };
    std::mutex clientEventMutex;
    std::vector<ClientUiEvent> pendingClientEvents;
    bool penaltyPracticeMode = false;
    int penaltyGoals = 0;
    int penaltySaves = 0;
    int penaltyMisses = 0;
    int penaltyRoundTicks = 0;
    int penaltyPostKickTicks = 0;
    int penaltyResetDelayTicks = 0;
    bool penaltyKickTaken = false;
    bool penaltyGkTouched = false;
    bool penaltyRoundFinished = false;
    bool penaltyRunupStarted = false;
    int penaltyStopTicks = 0;
    uint32_t penaltyBotLastMask = 0;
    float currentSmoothedExtrapolation = 0.0f;
    uint32_t currentKeyMask = 0;
    uint32_t lastAssistantServerFrame = 0xFFFFFFFFu;
    std::shared_ptr<World> frameRenderWorld;
    std::shared_ptr<World> frameRealWorld;

    uint32_t rawPhysicalMask = 0;
    uint64_t pressTimeUp = 0;
    uint64_t pressTimeDown = 0;
    uint64_t pressTimeLeft = 0;
    uint64_t pressTimeRight = 0;
    int socdHoldTicksRemainingX = 0;
    int socdHoldTicksRemainingY = 0;
    uint32_t socdPendingMaskX = 0;
    uint32_t socdPendingMaskY = 0;

    uint32_t resolveSOCD(uint32_t rawMask);

    int fps = 60;
    int frameCount = 0;
    Uint32 fpsTimer = 0;
};

}
