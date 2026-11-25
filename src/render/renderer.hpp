#pragma once

#include <SDL2/SDL.h>
#include "../math/vec2.hpp"
#include "../physics/world.hpp"
#include "../assist/tactical_assistant.hpp"
#include "font.hpp"
#include <algorithm>
#include <string>
#include <vector>
#include <deque>
#include <cstdint>
#include <mutex>

namespace haxball {

struct ChatMessage {
    std::string sender;
    std::string text;
    uint32_t color = 0xFFFFFF;
    int senderTeam = 0;
    bool isAdmin = false;
    bool isAnnouncement = false;
    uint8_t style = 0;
    float age = 0.0f;
};

struct KickRippleEffect {
    Vec2 worldPos;
    float age = 0.0f;
    float maxAge = 0.45f;
    SDL_Color color{255, 255, 255, 220};
};

struct EsportsSettings {
    int extrapolationMs = 0;
    int handicapMs = 0;
    bool vsync = false;
    int fpsCap = 240;
    int cameraMode = 0;
    float cameraSmoothness = 0.22f;

    int fieldRotation = 0;

    int pitchTheme = 0;
    int ballTrailStyle = 1;
    float ballTrailOpacity = 0.65f;
    float discOutlineWidth = 2.2f;
    float localHaloOpacity = 0.45f;

    bool showTeamColors = true;
    bool showAvatars = true;
    bool showPlayerIds = false;
    bool showLocalHalo = true;
    bool showNetGraph = true;
    bool showChatIndicators = true;
    bool showInvisibleSegments = false;
    bool showDesyncGhost = false;

    std::string pendingAvatarChange;
};

enum class OverlayAction {
    None,
    JoinRed,
    JoinSpectators,
    JoinBlue,
    StartStopGame,
    PauseResumeGame,
    AutoTeams,
    LeaveRoom,
    ToggleVSync,
    ExtrapolationChanged,
    AssistanceChanged,
    AvatarChanged
};

class GameRenderer {
public:
    GameRenderer();
    ~GameRenderer();

    bool init(SDL_Renderer* renderer, const std::string& fontPath);
    void update(double dt, int currentPingMs);
    void renderWorld(SDL_Renderer* renderer, const World& world, uint32_t myPlayerId, int pingMs, int maxPingMs, int fps, double dt, bool isSandbox = false, const World* realWorld = nullptr);

    void addChatMessage(const std::string& sender, const std::string& text, uint32_t color = 0xFFFFFF, int team = 0, bool isAdmin = false);
    void addAnnouncement(const std::string& text, uint32_t color, uint8_t style);
    void spawnKickRipple(const Vec2& pos, SDL_Color col = {255, 255, 255, 220});
    void spawnGoalConfetti(const Vec2& pos, int scoringTeam);

    void setZoom(float z) { zoom = std::clamp(z, 0.45f, 2.60f); }
    float getZoom() const { return zoom; }
    void applyZoomPreset(int presetIdx);

    int getEffectiveFieldRotation(int localPlayerTeam) const;
    float getRotationRadians() const;
    uint32_t remapInputForRotation(uint32_t screenKeyMask, int localPlayerTeam) const;

    OverlayAction handleOverlayClick(int mx, int my, int screenW, int screenH, const World& world, uint32_t myPlayerId, bool isSandbox = false);
    OverlayAction handleOverlayDrag(int mx, int my, int screenW, int screenH);

    bool isChatOpen = false;
    bool showRoomMenu = false;
    bool showSettingsPanel = false;
    bool useImGuiDashboard = false;
    int activeMenuTab = 1;
    float settingsScrollY = 0.0f;
    void scrollSettingsPanel(float deltaPx, int screenH);
    int chatScrollOffset = 0;
    std::string chatInputBuffer;
    void scrollChat(int deltaLines);

    EsportsSettings settings;
    TacticalAssistant assistant;

    Vec2 worldToScreen(const Vec2& worldPos, int screenW, int screenH) const;
    Vec2 screenToWorld(const Vec2& screenPos, int screenW, int screenH) const;

    static void drawAACircleFilled(SDL_Renderer* renderer, float cx, float cy, float radius, SDL_Color color, int segments = 40);
    static void drawAARing(SDL_Renderer* renderer, float cx, float cy, float radius, float thickness, SDL_Color color, int segments = 40);
    static void drawAALine(SDL_Renderer* renderer, float x0, float y0, float x1, float y1, float thickness, SDL_Color color);
    static void drawAAArc(SDL_Renderer* renderer, float cx, float cy, float radius, float startAngle, float endAngle, bool increasingAngle, float thickness, SDL_Color color);
    static void drawRoundedRect(SDL_Renderer* renderer, float x, float y, float w, float h, float r, SDL_Color fillCol, SDL_Color borderCol = {0, 0, 0, 0}, float borderWidth = 0.0f);

private:
    void createPitchTextures(SDL_Renderer* renderer);
    void updateCamera(const World& world, uint32_t myPlayerId, int screenW, int screenH, double dt);
    void drawPitch(SDL_Renderer* renderer, const Stadium& stadium, int screenW, int screenH);
    void drawFunEffectsUnder(SDL_Renderer* renderer, const World& world, int screenW, int screenH);
    void drawJoints(SDL_Renderer* renderer, const World& world, int screenW, int screenH);
    void drawTacticalOverlay(SDL_Renderer* renderer, const World& world, uint32_t myPlayerId, int screenW, int screenH);
    void drawDiscs(SDL_Renderer* renderer, const World& world, uint32_t myPlayerId, int screenW, int screenH);
    void drawStripedPlayerDisc(SDL_Renderer* renderer, float cx, float cy, float radius, const TeamColors& tc, uint32_t fallbackColor);
    void drawLocationIndicators(SDL_Renderer* renderer, const World& world, uint32_t myPlayerId, int screenW, int screenH);
    void drawHUD(SDL_Renderer* renderer, const World& world, uint32_t myPlayerId, int pingMs, int maxPingMs, int fps, int screenW, int screenH);
    void drawChat(SDL_Renderer* renderer, int screenW, int screenH);
    void drawRoomLobbyModal(SDL_Renderer* renderer, const World& world, uint32_t myPlayerId, int screenW, int screenH, bool isSandbox);
    void drawMenuTabsHeader(SDL_Renderer* renderer, float x, float y, float w, int activeTab);
    void drawSliderRow(SDL_Renderer* renderer, float x, float y, float w, const std::string& label, const std::string& valStr, float normVal, SDL_Color barColor);
    void drawUnifiedDashboardModal(SDL_Renderer* renderer, const World& world, int screenW, int screenH);

    Vec2 cameraPos{0.0, 0.0};
    float zoom = 1.0f;
    int activeRotation = 0;

    FontRenderer fontSmall;
    FontRenderer font;
    FontRenderer fontLarge;
    FontRenderer fontBanner;

    SDL_Texture* grassTileTex = nullptr;
    SDL_Texture* hockeyTileTex = nullptr;

    mutable std::mutex chatMutex;
    std::deque<ChatMessage> chatMessages;
    std::deque<int> pingHistory;
    double pingSampleTimer = 0.0;

    std::deque<Vec2> ballTrailPositions;
    double ballTrailTimer = 0.0;
    int lastBallTouchTeam = 0;
    std::vector<KickRippleEffect> kickRipples;

    int prevMatchState = 0;
    double bannerAnimTimer = 0.0;
    std::string bannerLine1;
    std::string bannerLine2;
    SDL_Color bannerColor{255, 255, 255, 255};
};

}
