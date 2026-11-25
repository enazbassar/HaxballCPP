#include "app.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <chrono>
#include <thread>

namespace haxball {

uint32_t App::resolveSOCD(uint32_t rawMask) {
    const auto& cfg = AppConfig::get();
    if (!cfg.socdLastInputPriority) {
        return rawMask;
    }

    uint32_t resolved = rawMask;

    bool upHeld = (rawMask & 1u) != 0;
    bool downHeld = (rawMask & 2u) != 0;
    if (upHeld && downHeld) {

        if (pressTimeUp > pressTimeDown) {
            resolved &= ~2u;
        } else if (pressTimeDown > pressTimeUp) {
            resolved &= ~1u;
        }
    }

    bool leftHeld = (rawMask & 4u) != 0;
    bool rightHeld = (rawMask & 8u) != 0;
    if (leftHeld && rightHeld) {

        if (pressTimeLeft > pressTimeRight) {
            resolved &= ~8u;
        } else if (pressTimeRight > pressTimeLeft) {
            resolved &= ~4u;
        }
    }

    if (socdHoldTicksRemainingX > 0) {
        resolved &= ~12u;
    }
    if (socdHoldTicksRemainingY > 0) {
        resolved &= ~3u;
    }

    return resolved;
}

App::App() {}

App::~App() {
    client.disconnect();
    cockpitMenu.shutdown();
    if (sdlRenderer) SDL_DestroyRenderer(sdlRenderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
}

bool App::init(const std::string& autoRoom, bool autoSandbox, bool isTest, bool isVerbose) {
    this->testMode = isTest;
    this->verbose = isVerbose;
    this->testTimer = 0.0;
    this->connectedTestTimer = 0.0;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        std::cerr << "[App] Failed to init SDL: " << SDL_GetError() << std::endl;
        return false;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

    window = SDL_CreateWindow(
        "Haxball C++ Client",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!window) {
        std::cerr << "[App] Failed to create window: " << SDL_GetError() << std::endl;
        return false;
    }

    Uint32 renderFlags = SDL_RENDERER_ACCELERATED;
    if (gameRenderer.settings.vsync) {
        renderFlags |= SDL_RENDERER_PRESENTVSYNC;
    }
    sdlRenderer = SDL_CreateRenderer(window, -1, renderFlags);
    if (!sdlRenderer) {
        sdlRenderer = SDL_CreateRenderer(window, -1, 0);
    }
    if (!sdlRenderer) {
        std::cerr << "[App] Failed to create renderer: " << SDL_GetError() << std::endl;
        return false;
    }

    std::string fontPath = "assets/fonts/font.ttf";
    auto fileExists = [](const char* p) {
        if (FILE* f = std::fopen(p, "rb")) { std::fclose(f); return true; }
        return false;
    };
    if (!fileExists(fontPath.c_str())) {
        if (fileExists("../assets/fonts/font.ttf")) {
            fontPath = "../assets/fonts/font.ttf";
        } else {
            fontPath = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf";
        }
    }
    if (!browser.init(sdlRenderer, fontPath)) {
        std::cerr << "[App] Failed to init RoomBrowser UI" << std::endl;
        return false;
    }

    if (!gameRenderer.init(sdlRenderer, fontPath)) {
        std::cerr << "[App] Failed to init GameRenderer" << std::endl;
        return false;
    }

    if (cockpitMenu.init(window, sdlRenderer)) {
        gameRenderer.useImGuiDashboard = true;
    }

    connectingFont.init(sdlRenderer, fontPath, 22.0f);

    sound.init();

    const auto& initCfg = AppConfig::get();
    gameRenderer.settings.extrapolationMs = initCfg.extrapolationMs;
    gameRenderer.settings.handicapMs = initCfg.handicapMs;
    gameRenderer.settings.vsync = initCfg.vsync;
    gameRenderer.settings.fpsCap = initCfg.fpsCap;
    gameRenderer.settings.ballTrailStyle = initCfg.ballTrailStyle;
    gameRenderer.settings.pitchTheme = initCfg.pitchTheme;
    gameRenderer.assistant.antiFakeShotFilter = initCfg.antiFakeShot;
    gameRenderer.assistant.antiOpMode = initCfg.antiOpMode;
    gameRenderer.assistant.fakeShotEnabled = initCfg.autoFakeShot;
    gameRenderer.assistant.fakeShotExtendEnabled = initCfg.fakeShotExtendEnabled;
    gameRenderer.assistant.fakeShotExtendTicks = std::clamp(initCfg.fakeShotExtendTicks, 0, 15);
    gameRenderer.assistant.fakeShotIntervalMs = initCfg.fakeShotIntervalMs;
    gameRenderer.assistant.realShotIntervalMs = initCfg.realShotIntervalMs;
    gameRenderer.assistant.slowSpeedPercent = initCfg.slowSpeedPercent;
    gameRenderer.assistant.autoShakeEnabled = true;
    gameRenderer.assistant.autoShakeFrequencyTicks = initCfg.autoShakeFrequencyTicks;
    gameRenderer.assistant.ballOrbitEnabled = true;
    gameRenderer.assistant.ballOrbitDistance = initCfg.ballOrbitDistance;
    gameRenderer.assistant.fakeTypingActive = initCfg.fakeTypingIndicator;
    gameRenderer.assistant.typingSpamEnabled = initCfg.typingSpammerEnabled;
    gameRenderer.assistant.typingSpamSpeedMs = initCfg.typingSpamSpeedMs;
    gameRenderer.assistant.firstTouchCushionEnabled = initCfg.firstTouchCushionEnabled;
    gameRenderer.assistant.fastFakeEnabled = true;
    gameRenderer.assistant.fastFakeSpeedTicks = initCfg.fastFakeSpeedTicks;
    gameRenderer.assistant.realShotEnabled = true;
    gameRenderer.assistant.autoGoalEnabled = initCfg.autoGoalEnabled;
    gameRenderer.assistant.autoGoalAnyGoal = initCfg.autoGoalAnyGoal;
    gameRenderer.assistant.autoGoalAccuracy = std::clamp(initCfg.autoGoalAccuracy, 30, 100);
    gameRenderer.assistant.autoSaveEnabled = initCfg.autoSaveEnabled;
    gameRenderer.assistant.aimLineDistance = std::clamp(initCfg.aimLineDistance, 20, 300);
    gameRenderer.assistant.showLiveAimTrajectory = initCfg.showLiveAimTrajectory;
    gameRenderer.assistant.showBallRollTrajectory = initCfg.showBallRollTrajectory;
    gameRenderer.assistant.showGkDangerCone = initCfg.showGkDangerCone;
    gameRenderer.assistant.showOtherGoalsCone = initCfg.showOtherGoalsCone;
    gameRenderer.settings.showDesyncGhost = initCfg.showDesyncGhost;
    cockpitMenu.lightMode = initCfg.menuLightMode;
    cockpitMenu.accentIdx = initCfg.menuAccentIdx;
    cockpitMenu.menuOpacity = initCfg.menuOpacity;
    cockpitMenu.showX88Hud = initCfg.showX88Hud;
    cockpitMenu.x88Position = initCfg.x88Position;
    cockpitMenu.x88ActiveOnly = initCfg.x88ActiveOnly;
    cockpitMenu.x88ShowHotkeys = initCfg.x88ShowHotkeys;
    cockpitMenu.x88Opacity = initCfg.x88Opacity;
    cockpitMenu.x88Scale = initCfg.x88Scale;
    sound.setMasterVolume(initCfg.masterVolume);

    client.setExtrapolationMs(initCfg.extrapolationMs);
    client.setHandicapMs(initCfg.handicapMs);
    client.setAntiFakeShot(initCfg.antiFakeShot);
    client.setAntiOpMode(initCfg.antiOpMode);
    client.setAuthKey(initCfg.getActiveAuthKey());

    client.onConnected = [this]() {
        std::lock_guard<std::mutex> lock(clientEventMutex);
        ClientUiEvent ev;
        ev.type = ClientUiEvent::Type::Connected;
        pendingClientEvents.push_back(std::move(ev));
    };

    client.onDisconnected = [this](const std::string& reason) {
        std::lock_guard<std::mutex> lock(clientEventMutex);
        ClientUiEvent ev;
        ev.type = ClientUiEvent::Type::Disconnected;
        ev.text = reason;
        pendingClientEvents.push_back(std::move(ev));
    };

    client.onChatReceived = [this](const std::string& sender, int team, bool isAdmin, const std::string& msg) {
        std::lock_guard<std::mutex> lock(clientEventMutex);
        if (pendingClientEvents.size() < 512) {
            ClientUiEvent ev;
            ev.type = ClientUiEvent::Type::Chat;
            ev.sender = sender;
            ev.text = msg;
            ev.team = team;
            ev.isAdmin = isAdmin;
            pendingClientEvents.push_back(std::move(ev));
        }
    };

    client.onAnnouncement = [this](const AnnouncementMsg& ann) {
        std::lock_guard<std::mutex> lock(clientEventMutex);
        if (pendingClientEvents.size() < 512) {
            ClientUiEvent ev;
            ev.type = ClientUiEvent::Type::Announcement;
            ev.text = ann.text;
            ev.color = ann.color;
            ev.style = ann.style;
            ev.sound = ann.sound;
            pendingClientEvents.push_back(std::move(ev));
        }
    };

    client.onKickSound = [this]() {
        sound.play(SoundEffect::Kick);
    };
    client.onGoalSound = [this](int) {
        sound.play(SoundEffect::Goal);
        std::lock_guard<std::mutex> lock(clientEventMutex);
        ClientUiEvent ev;
        ev.type = ClientUiEvent::Type::GoalReset;
        pendingClientEvents.push_back(std::move(ev));
    };
    client.onPlayerBallKick = [this](const BallKickEvent& ev) {
        gameRenderer.assistant.onAuthoritativePlayerKick(ev);
    };
    client.onServerBallSpeedSet = [this](uint32_t tick, const Vec2& vel) {
        gameRenderer.assistant.onAuthoritativeBallSpeedSet(tick, vel);
    };

    fpsTimer = SDL_GetTicks();

    if (autoSandbox) {
        startSandbox();
    } else if (!autoRoom.empty()) {
        startMultiplayer(autoRoom);
    }

    return true;
}

void App::startMultiplayer(const std::string& roomId, const std::string& password) {
    if (roomId.empty()) return;
    state = AppState::Connecting;
    gameRenderer.showRoomMenu = false;
    gameRenderer.isChatOpen = false;
    currentKeyMask = 0;
    lastAssistantServerFrame = 0xFFFFFFFFu;
    frameRenderWorld.reset();
    frameRealWorld.reset();
    gameRenderer.assistant.resetTickSync();

    std::string cleanId = roomId;
    size_t qPos = cleanId.find("?c=");
    if (qPos != std::string::npos) cleanId = cleanId.substr(qPos + 3);
    size_t ampPos = cleanId.find('&');
    if (ampPos != std::string::npos) cleanId = cleanId.substr(0, ampPos);

    client.setVerbose(verbose);
    client.connect(cleanId, browser.getPlayerName(), browser.getPlayerFlag(), browser.getPlayerAvatar(), password, browser.getActiveAuthKey());
}

void App::startSandbox() {
    sandboxWorld = std::make_shared<World>(Stadium::createClassic());
    sandboxWorld->roomName = "Practice Sandbox";
    sandboxWorld->addPlayer(1, browser.getPlayerName(), browser.getPlayerFlag(), browser.getPlayerAvatar());
    if (auto* p1 = sandboxWorld->getPlayer(1)) p1->admin = true;
    sandboxWorld->setPlayerTeam(1, 1);

    sandboxWorld->addPlayer(2, "Winger #1 ⚡", "es", "⚡");
    sandboxWorld->setPlayerTeam(2, 1);
    sandboxWorld->addPlayer(3, "Pivot #2 🔥", "br", "🔥");
    sandboxWorld->setPlayerTeam(3, 1);
    sandboxWorld->addPlayer(4, "Keeper Bot 🥶", "de", "🥶");
    sandboxWorld->setPlayerTeam(4, 2);

    if (auto* p1 = sandboxWorld->getPlayer(1)) {
        if (p1->discIndex >= 0 && p1->discIndex < static_cast<int>(sandboxWorld->discs.size())) {
            sandboxWorld->discs[p1->discIndex].pos = Vec2(28.0, 14.0);
        }
    }
    if (auto* p2 = sandboxWorld->getPlayer(2)) {
        if (p2->discIndex >= 0 && p2->discIndex < static_cast<int>(sandboxWorld->discs.size())) {
            sandboxWorld->discs[p2->discIndex].pos = Vec2(120.0, -95.0);
            sandboxWorld->discs[p2->discIndex].speed = Vec2(1.2, 0.2);
        }
    }
    if (auto* p3 = sandboxWorld->getPlayer(3)) {
        if (p3->discIndex >= 0 && p3->discIndex < static_cast<int>(sandboxWorld->discs.size())) {
            sandboxWorld->discs[p3->discIndex].pos = Vec2(95.0, 95.0);
        }
    }
    if (auto* p4 = sandboxWorld->getPlayer(4)) {
        if (p4->discIndex >= 0 && p4->discIndex < static_cast<int>(sandboxWorld->discs.size())) {
            sandboxWorld->discs[p4->discIndex].pos = Vec2(335.0, 20.0);
        }
    }

    sandboxWorld->onKick = [this]() {
        sound.play(SoundEffect::Kick);
    };
    sandboxWorld->onGoal = [this](int) {
        sound.play(SoundEffect::Goal);
        gameRenderer.assistant.onMatchReset();
    };
    sandboxWorld->onPlayerBallKick = [this](const BallKickEvent& ev) {
        gameRenderer.assistant.onAuthoritativePlayerKick(ev);
    };
    gameRenderer.assistant.resetMatchPowerTracking("Practice Sandbox");

    sandboxRenderWorld = std::make_shared<World>(sandboxWorld->cloneState());
    sandboxHistory.clear();

    state = AppState::Sandbox;
    penaltyPracticeMode = false;
    gameRenderer.assistant.penaltyGkEnabled = false;
    gameRenderer.assistant.smartGkEnabled = false;
    gameRenderer.showRoomMenu = false;
    gameRenderer.isChatOpen = false;
    currentKeyMask = 0;
    lastAssistantServerFrame = 0xFFFFFFFFu;
    frameRenderWorld.reset();
    frameRealWorld.reset();
    gameRenderer.assistant.resetTickSync();
    gameRenderer.addAnnouncement("Sandbox Mode! [F1] Esports | [F2] Tactical Sliders | [F3] Visuals & Emoji | [F] Fake Shot | [R] Rotate", 0x88FF99, 1);
}

void App::resetPenaltyRound() {
    if (!sandboxWorld || !penaltyPracticeMode) return;
    sandboxWorld->matchState = 1;
    sandboxWorld->goalTickCounter = 0;
    sandboxWorld->pauseTickCounter = 0;

    if (!sandboxWorld->discs.empty()) {
        sandboxWorld->discs[0].pos = Vec2(935.0, 0.0);
        sandboxWorld->discs[0].speed = Vec2(0.0, 0.0);
        sandboxWorld->discs[0].cMask = 63;
        sandboxWorld->discs[0].invMass = 0.0;
    }

    if (auto* p1 = sandboxWorld->getPlayer(1)) {
        p1->input = 0;
        p1->isKicking = false;
        p1->kickCooldown = 0;
        if (p1->discIndex >= 0 && p1->discIndex < (int)sandboxWorld->discs.size()) {
            sandboxWorld->discs[p1->discIndex].pos = Vec2(820.0, 0.0);
            sandboxWorld->discs[p1->discIndex].speed = Vec2(0.0, 0.0);
        }
    }

    if (auto* p2 = sandboxWorld->getPlayer(2)) {
        p2->input = 0;
        p2->isKicking = false;
        p2->kickCooldown = 0;
        if (p2->discIndex >= 0 && p2->discIndex < (int)sandboxWorld->discs.size()) {
            sandboxWorld->discs[p2->discIndex].pos = Vec2(1150.0, 0.0);
            sandboxWorld->discs[p2->discIndex].speed = Vec2(0.0, 0.0);
        }
    }

    penaltyRoundTicks = 0;
    penaltyPostKickTicks = 0;
    penaltyResetDelayTicks = 0;
    penaltyKickTaken = false;
    penaltyGkTouched = false;
    penaltyRoundFinished = false;
    penaltyRunupStarted = false;
    penaltyStopTicks = 0;
    penaltyBotLastMask = 0;
    sandboxHistory.clear();
    if (sandboxRenderWorld) {
        *sandboxRenderWorld = sandboxWorld->cloneState();
    }
}

void App::startPenaltyPractice() {

    std::shared_ptr<Stadium> st;
    const std::string hbsAscii = "assets/ldc_real_soccer.hbs";
    const std::string hbsFile = "LDC-Real-Soccer-\xCA\x9C\xE1\xB4\x80x\xE1\xB4\x8D\xE1\xB4\x8F\xE1\xB4\x85s.\xE1\xB4\x84\xE1\xB4\x8F\xE1\xB4\x8D_6a6399b7c92aa.hbs";
    std::vector<std::string> candidates = {
        hbsAscii,
        "../" + hbsAscii,
        hbsFile,
        "../" + hbsFile
    };
    if (char* basePath = SDL_GetBasePath()) {
        std::string bp(basePath);
        SDL_free(basePath);
        candidates.push_back(bp + hbsAscii);
        candidates.push_back(bp + "../" + hbsAscii);
        candidates.push_back(bp + hbsFile);
        candidates.push_back(bp + "../" + hbsFile);
    }
    for (const auto& p : candidates) {
        st = Stadium::loadFromHbsFile(p);
        if (st) break;
    }
    if (!st) {
        st = Stadium::createBig();
    }

    sandboxWorld = std::make_shared<World>(st);
    sandboxWorld->roomName = "LDC Penalty Practice";
    sandboxWorld->scoreLimit = 0;
    sandboxWorld->timeLimit = 0;
    sandboxWorld->addPlayer(1, browser.getPlayerName(), browser.getPlayerFlag(), browser.getPlayerAvatar());
    if (auto* p1 = sandboxWorld->getPlayer(1)) p1->admin = true;
    sandboxWorld->setPlayerTeam(1, 1);

    sandboxWorld->addPlayer(2, "GK Bot", "nl", "🧤");
    sandboxWorld->setPlayerTeam(2, 2);

    penaltyPracticeMode = true;
    penaltyGoals = 0;
    penaltySaves = 0;
    penaltyMisses = 0;

    sandboxWorld->onKick = [this]() {
        sound.play(SoundEffect::Kick);
    };
    sandboxWorld->onGoal = [this](int scoringTeam) {
        sound.play(SoundEffect::Goal);
        gameRenderer.assistant.onMatchReset();
        if (penaltyPracticeMode && !penaltyRoundFinished) {
            penaltyRoundFinished = true;
            penaltyResetDelayTicks = 75;
            if (scoringTeam == 1) {
                penaltyGoals++;
                gameRenderer.addAnnouncement(
                    "⚽ GOAL!  (Goals: " + std::to_string(penaltyGoals) +
                    " | Saved: " + std::to_string(penaltySaves) +
                    " | Missed: " + std::to_string(penaltyMisses) + ")",
                    0x34D399, 1);
            }
        }
    };
    sandboxWorld->onPlayerBallKick = [this](const BallKickEvent& ev) {
        gameRenderer.assistant.onAuthoritativePlayerKick(ev);
        if (penaltyPracticeMode) {
            if (ev.playerId == 1) {
                penaltyKickTaken = true;
                penaltyPostKickTicks = 0;
                if (sandboxWorld && !sandboxWorld->discs.empty()) {
                    sandboxWorld->discs[0].cMask = 63;
                }
            } else if (ev.playerId == 2) {
                penaltyGkTouched = true;
            }
        }
    };

    gameRenderer.assistant.resetMatchPowerTracking("LDC Penalty Practice");
    gameRenderer.assistant.resetTickSync();
    gameRenderer.assistant.penaltyGkEnabled = false;
    gameRenderer.assistant.smartGkEnabled = false;

    sandboxRenderWorld = std::make_shared<World>(sandboxWorld->cloneState());
    resetPenaltyRound();

    state = AppState::Sandbox;
    gameRenderer.showRoomMenu = false;
    gameRenderer.isChatOpen = false;
    currentKeyMask = 0;
    lastAssistantServerFrame = 0xFFFFFFFFu;
    frameRenderWorld.reset();
    frameRealWorld.reset();
    gameRenderer.addAnnouncement("🥅 LDC Penalty Practice! Beat the GK Bot (10s/shot) | [Backspace] Reset | [8] Toggle GK Auto-Saver", 0x38BDF8, 1);
}

void App::leaveToBrowser() {
    client.disconnect();
    sandboxWorld.reset();
    sandboxRenderWorld.reset();
    sandboxHistory.clear();
    penaltyPracticeMode = false;
    gameRenderer.assistant.penaltyGkEnabled = false;
    gameRenderer.assistant.smartGkEnabled = false;
    frameRenderWorld.reset();
    frameRealWorld.reset();
    lastAssistantServerFrame = 0xFFFFFFFFu;
    gameRenderer.assistant.resetTickSync();
    state = AppState::Browser;
    gameRenderer.showRoomMenu = false;
    gameRenderer.isChatOpen = false;
    browser.refresh();
}

void App::syncSettingsToConfig() {
    auto& cfg = AppConfig::get();
    cfg.extrapolationMs = gameRenderer.settings.extrapolationMs;
    cfg.handicapMs = gameRenderer.settings.handicapMs;
    cfg.vsync = gameRenderer.settings.vsync;
    cfg.fpsCap = gameRenderer.settings.fpsCap;
    cfg.ballTrailStyle = gameRenderer.settings.ballTrailStyle;
    cfg.pitchTheme = gameRenderer.settings.pitchTheme;
    cfg.antiFakeShot = gameRenderer.assistant.antiFakeShotFilter;
    cfg.antiOpMode = gameRenderer.assistant.antiOpMode;
    cfg.autoFakeShot = gameRenderer.assistant.fakeShotEnabled;
    cfg.fakeShotExtendEnabled = gameRenderer.assistant.fakeShotExtendEnabled;
    cfg.fakeShotExtendTicks = std::clamp(gameRenderer.assistant.fakeShotExtendTicks, 0, 15);
    cfg.fakeShotIntervalMs = gameRenderer.assistant.fakeShotIntervalMs;
    cfg.realShotIntervalMs = gameRenderer.assistant.realShotIntervalMs;
    cfg.slowSpeedPercent = gameRenderer.assistant.slowSpeedPercent;
    cfg.autoShakeEnabled = gameRenderer.assistant.autoShakeEnabled;
    cfg.autoShakeFrequencyTicks = gameRenderer.assistant.autoShakeFrequencyTicks;
    cfg.ballOrbitEnabled = gameRenderer.assistant.ballOrbitEnabled;
    cfg.ballOrbitDistance = gameRenderer.assistant.ballOrbitDistance;
    cfg.fakeTypingIndicator = gameRenderer.assistant.fakeTypingActive;
    cfg.typingSpammerEnabled = gameRenderer.assistant.typingSpamEnabled;
    cfg.typingSpamSpeedMs = gameRenderer.assistant.typingSpamSpeedMs;
    cfg.firstTouchCushionEnabled = gameRenderer.assistant.firstTouchCushionEnabled;
    cfg.fastFakeEnabled = gameRenderer.assistant.fastFakeEnabled;
    cfg.fastFakeSpeedTicks = gameRenderer.assistant.fastFakeSpeedTicks;
    cfg.autoGoalEnabled = gameRenderer.assistant.autoGoalEnabled;
    cfg.autoGoalAnyGoal = gameRenderer.assistant.autoGoalAnyGoal;
    cfg.autoGoalAccuracy = std::clamp(gameRenderer.assistant.autoGoalAccuracy, 30, 100);
    cfg.autoSaveEnabled = gameRenderer.assistant.autoSaveEnabled;
    cfg.aimLineDistance = std::clamp(gameRenderer.assistant.aimLineDistance, 20, 300);
    cfg.showLiveAimTrajectory = gameRenderer.assistant.showLiveAimTrajectory;
    cfg.showBallRollTrajectory = gameRenderer.assistant.showBallRollTrajectory;
    cfg.showGkDangerCone = gameRenderer.assistant.showGkDangerCone;
    cfg.showOtherGoalsCone = gameRenderer.assistant.showOtherGoalsCone;
    cfg.showDesyncGhost = gameRenderer.settings.showDesyncGhost;
    cfg.menuLightMode = cockpitMenu.lightMode;
    cfg.menuAccentIdx = cockpitMenu.accentIdx;
    cfg.menuOpacity = cockpitMenu.menuOpacity;
    cfg.showX88Hud = cockpitMenu.showX88Hud;
    cfg.x88Position = cockpitMenu.x88Position;
    cfg.x88ActiveOnly = cockpitMenu.x88ActiveOnly;
    cfg.x88ShowHotkeys = cockpitMenu.x88ShowHotkeys;
    cfg.x88Opacity = cockpitMenu.x88Opacity;
    cfg.x88Scale = cockpitMenu.x88Scale;
    cfg.save();
}

void App::handleOverlayAction(OverlayAction action) {
    if (action == OverlayAction::None) return;

    if (action == OverlayAction::LeaveRoom) {
        leaveToBrowser();
        return;
    }
    if (action == OverlayAction::ToggleVSync) {
        SDL_RenderSetVSync(sdlRenderer, gameRenderer.settings.vsync ? 1 : 0);
        syncSettingsToConfig();
        return;
    }
    if (action == OverlayAction::ExtrapolationChanged) {
        client.setExtrapolationMs(gameRenderer.settings.extrapolationMs);
        client.setHandicapMs(gameRenderer.settings.handicapMs);
        syncSettingsToConfig();
        return;
    }
    if (action == OverlayAction::AssistanceChanged) {
        client.setAntiFakeShot(gameRenderer.assistant.antiFakeShotFilter);
        client.setAntiOpMode(gameRenderer.assistant.antiOpMode);
        syncSettingsToConfig();
        return;
    }
    if (action == OverlayAction::AvatarChanged) {
        if (!gameRenderer.settings.pendingAvatarChange.empty()) {
            std::string av = gameRenderer.settings.pendingAvatarChange;
            gameRenderer.settings.pendingAvatarChange.clear();
            browser.setPlayerAvatar(av);
            if (state == AppState::Multiplayer) {
                client.setAvatar(av);
            } else if (state == AppState::Sandbox && sandboxWorld) {
                if (auto* p = sandboxWorld->getPlayer(1)) p->avatar = av;
            }
            gameRenderer.addAnnouncement("Player Avatar updated to: " + av, 0xFFE270, 1);
            syncSettingsToConfig();
        }
        return;
    }

    if (state == AppState::Multiplayer) {
        switch (action) {
            case OverlayAction::JoinRed:        client.setTeam(1); break;
            case OverlayAction::JoinSpectators: client.setTeam(0); break;
            case OverlayAction::JoinBlue:       client.setTeam(2); break;
            case OverlayAction::StartStopGame: {
                auto w = client.getAuthoritativeWorld();
                if (w && w->hasMatch) client.stopGame();
                else client.startGame();
                break;
            }
            case OverlayAction::PauseResumeGame: client.pauseGame(); break;
            case OverlayAction::AutoTeams:       client.autoTeams(); break;
            default: break;
        }
    } else if (state == AppState::Sandbox && sandboxWorld) {
        switch (action) {
            case OverlayAction::JoinRed:        sandboxWorld->setPlayerTeam(1, 1); break;
            case OverlayAction::JoinSpectators: sandboxWorld->setPlayerTeam(1, 0); break;
            case OverlayAction::JoinBlue:       sandboxWorld->setPlayerTeam(1, 2); break;
            case OverlayAction::StartStopGame:
                if (sandboxWorld->hasMatch) sandboxWorld->stopGame();
                else sandboxWorld->startGame();
                break;
            case OverlayAction::PauseResumeGame:
                sandboxWorld->pauseResumeGame(sandboxWorld->pauseTickCounter != 120);
                break;
            case OverlayAction::AutoTeams:
                sandboxWorld->autoTeams();
                break;
            default: break;
        }
    }
}

static void deletePreviousWordUTF8(std::string& s) {
    if (s.empty()) return;

    auto popOneUTF8Char = [](std::string& str) {
        if (str.empty()) return;
        while (!str.empty() && (static_cast<unsigned char>(str.back()) & 0xC0) == 0x80) {
            str.pop_back();
        }
        if (!str.empty()) str.pop_back();
    };

    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }

    while (!s.empty() && s.back() != ' ' && s.back() != '\t') {
        popOneUTF8Char(s);
    }
}

void App::handleEvents() {
    int screenW = 1280, screenH = 720;
    SDL_GetRendererOutputSize(sdlRenderer, &screenW, &screenH);
    int winW = 1280, winH = 720;
    SDL_GetWindowSize(window, &winW, &winH);

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            isRunning = false;
            return;
        }

        SDL_Event rawEvent = event;

        if (winW > 0 && winH > 0 && (winW != screenW || winH != screenH)) {
            float scaleX = (float)screenW / (float)winW;
            float scaleY = (float)screenH / (float)winH;
            if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
                event.button.x = (Sint32)(event.button.x * scaleX);
                event.button.y = (Sint32)(event.button.y * scaleY);
            } else if (event.type == SDL_MOUSEMOTION) {
                event.motion.x = (Sint32)(event.motion.x * scaleX);
                event.motion.y = (Sint32)(event.motion.y * scaleY);
            }
        }

        if (state == AppState::Browser) {
            auto action = browser.handleEvent(event, screenW, screenH);
            if (action == BrowserAction::JoinSelectedRoom) {
                startMultiplayer(browser.getSelectedRoomId(), browser.getRoomPassword());
            } else if (action == BrowserAction::JoinDirectId) {
                startMultiplayer(browser.getDirectRoomId(), browser.getRoomPassword());
            } else if (action == BrowserAction::StartSandbox) {
                startSandbox();
            } else if (action == BrowserAction::StartPenaltyPractice) {
                startPenaltyPractice();
            } else if (action == BrowserAction::SettingsChanged) {
                const auto& cfg = AppConfig::get();
                gameRenderer.settings.extrapolationMs = cfg.extrapolationMs;
                gameRenderer.settings.handicapMs = cfg.handicapMs;
                gameRenderer.settings.vsync = cfg.vsync;
                gameRenderer.settings.fpsCap = cfg.fpsCap;
                gameRenderer.settings.ballTrailStyle = cfg.ballTrailStyle;
                gameRenderer.settings.pitchTheme = cfg.pitchTheme;
                gameRenderer.assistant.antiFakeShotFilter = cfg.antiFakeShot;
                gameRenderer.assistant.antiOpMode = cfg.antiOpMode;
                gameRenderer.assistant.fakeShotEnabled = cfg.autoFakeShot;
                gameRenderer.assistant.fakeShotIntervalMs = cfg.fakeShotIntervalMs;
                gameRenderer.assistant.realShotIntervalMs = cfg.realShotIntervalMs;
                gameRenderer.assistant.slowSpeedPercent = cfg.slowSpeedPercent;
                gameRenderer.assistant.autoShakeEnabled = cfg.autoShakeEnabled;
                gameRenderer.assistant.autoShakeFrequencyTicks = cfg.autoShakeFrequencyTicks;
                gameRenderer.assistant.ballOrbitEnabled = cfg.ballOrbitEnabled;
                gameRenderer.assistant.ballOrbitDistance = cfg.ballOrbitDistance;
                gameRenderer.assistant.fakeTypingActive = cfg.fakeTypingIndicator;
                gameRenderer.assistant.typingSpamEnabled = cfg.typingSpammerEnabled;
                gameRenderer.assistant.typingSpamSpeedMs = cfg.typingSpamSpeedMs;
                gameRenderer.assistant.firstTouchCushionEnabled = cfg.firstTouchCushionEnabled;
                gameRenderer.assistant.fastFakeEnabled = cfg.fastFakeEnabled;
                gameRenderer.assistant.fastFakeSpeedTicks = cfg.fastFakeSpeedTicks;
                gameRenderer.settings.showDesyncGhost = cfg.showDesyncGhost;
                sound.setMasterVolume(cfg.masterVolume);
                client.setExtrapolationMs(cfg.extrapolationMs);
                client.setHandicapMs(cfg.handicapMs);
                client.setAntiFakeShot(cfg.antiFakeShot);
                client.setAntiOpMode(cfg.antiOpMode);
                client.setAuthKey(cfg.getActiveAuthKey());
                SDL_RenderSetVSync(sdlRenderer, cfg.vsync ? 1 : 0);
            }
        } else if (state == AppState::Connecting) {
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                leaveToBrowser();
            }
        } else if (state == AppState::Multiplayer || state == AppState::Sandbox) {
            if (gameRenderer.showSettingsPanel && cockpitMenu.isInitialized()) {
                if (cockpitMenu.processEvent(rawEvent, true)) {
                    continue;
                }
            }

            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                auto w = (state == AppState::Multiplayer) ? client.getAuthoritativeWorld() : sandboxWorld;
                if (w) {
                    uint32_t myId = (state == AppState::Multiplayer) ? client.getPlayerId() : 1;
                    bool isSandbox = (state == AppState::Sandbox);
                    auto act = gameRenderer.handleOverlayClick(event.button.x, event.button.y, screenW, screenH, *w, myId, isSandbox);
                    if (act != OverlayAction::None) {
                        handleOverlayAction(act);
                        continue;
                    }
                }
            } else if (event.type == SDL_MOUSEMOTION && (event.motion.state & SDL_BUTTON_LMASK) != 0) {
                auto act = gameRenderer.handleOverlayDrag(event.motion.x, event.motion.y, screenW, screenH);
                if (act != OverlayAction::None) {
                    handleOverlayAction(act);
                    continue;
                }
            }

            if (gameRenderer.isChatOpen) {
                if (event.type == SDL_TEXTINPUT) {
                    if (gameRenderer.chatInputBuffer.size() < 140) {
                        gameRenderer.chatInputBuffer += event.text.text;
                    }
                } else if (event.type == SDL_KEYDOWN) {
                    if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER) {
                        if (!gameRenderer.chatInputBuffer.empty()) {
                            const std::string& msg = gameRenderer.chatInputBuffer;
                            bool handledLocalCmd = false;
                            if (msg.rfind("/extrapolation", 0) == 0 || msg.rfind("/extrap", 0) == 0) {
                                size_t sp = msg.find(' ');
                                if (sp != std::string::npos) {
                                    try {
                                        int val = std::clamp(std::stoi(msg.substr(sp + 1)), -200, 500);
                                        gameRenderer.settings.extrapolationMs = val;
                                        client.setExtrapolationMs(val);
                                        gameRenderer.addAnnouncement("Extrapolation set to " + std::to_string(val) + " ms", 0x70E6FF, 1);
                                        handledLocalCmd = true;
                                    } catch (...) {}
                                }
                            } else if (msg.rfind("/handicap", 0) == 0) {
                                size_t sp = msg.find(' ');
                                if (sp != std::string::npos) {
                                    try {
                                        int val = std::clamp(std::stoi(msg.substr(sp + 1)), 0, 300);
                                        gameRenderer.settings.handicapMs = val;
                                        client.setHandicapMs(val);
                                        gameRenderer.addAnnouncement("Ping Handicap set to " + std::to_string(val) + " ms", 0x70E6FF, 1);
                                        handledLocalCmd = true;
                                    } catch (...) {}
                                }
                            } else if (msg.rfind("/avatar", 0) == 0) {
                                size_t sp = msg.find(' ');
                                if (sp != std::string::npos && sp + 1 < msg.size()) {
                                    std::string av = FontRenderer::clipUTF8Codepoints(msg.substr(sp + 1), 2);
                                    browser.setPlayerAvatar(av);
                                    if (state == AppState::Multiplayer) client.setAvatar(av);
                                    else if (sandboxWorld) {
                                        if (auto* p = sandboxWorld->getPlayer(1)) p->avatar = av;
                                    }
                                    gameRenderer.addAnnouncement("Avatar set to: " + av, 0xFFE270, 1);
                                    handledLocalCmd = true;
                                }
                            } else if (msg.rfind("/fakeping", 0) == 0 || msg.rfind("/fp", 0) == 0) {
                                std::vector<std::string> tokens;
                                {
                                    std::string cur;
                                    for (char ch : msg) {
                                        if (ch == ' ') {
                                            if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
                                        } else {
                                            cur.push_back(ch);
                                        }
                                    }
                                    if (!cur.empty()) tokens.push_back(cur);
                                }
                                auto parseFakePingU32 = [](const std::string& s) -> uint32_t {
                                    if (!s.empty() && s[0] == '-') return 0u;
                                    unsigned long long v = std::stoull(s);
                                    if (v > static_cast<unsigned long long>(UINT32_MAX)) return UINT32_MAX;
                                    return static_cast<uint32_t>(v);
                                };
                                if (tokens.size() <= 1 || tokens[1] == "off" || tokens[1] == "reset" || tokens[1] == "none") {
                                    client.setFakePingMs(std::nullopt);
                                    gameRenderer.addAnnouncement("Fake Ping: OFF (usage: /fp 666 | /fp flash [9999 666 420] | /fp rand [min max] | /fp wave [min max] | /fp count [start end] | /fp speed [ms])", 0x94A3B8, 1);
                                    handledLocalCmd = true;
                                } else if (tokens[1] == "flash" || tokens[1] == "flasher") {
                                    std::vector<uint32_t> seq;
                                    for (size_t i = 2; i < tokens.size(); ++i) {
                                        try { seq.push_back(parseFakePingU32(tokens[i])); } catch (...) {}
                                    }
                                    if (seq.empty()) seq = {9999, 666, 420, 69};
                                    client.setFakePingFlash(seq);
                                    std::string listStr;
                                    for (size_t i = 0; i < seq.size(); ++i) {
                                        if (i > 0) listStr += " -> ";
                                        listStr += std::to_string(seq[i]);
                                    }
                                    gameRenderer.addAnnouncement("Fake Ping FLASHER: " + listStr, 0xFACC15, 1);
                                    handledLocalCmd = true;
                                } else if (tokens[1] == "rand" || tokens[1] == "random") {
                                    uint32_t lo = 10, hi = 999;
                                    try {
                                        if (tokens.size() >= 3) lo = parseFakePingU32(tokens[2]);
                                        if (tokens.size() >= 4) hi = parseFakePingU32(tokens[3]);
                                    } catch (...) {}
                                    client.setFakePingRange(HaxballClient::FakePingMode::Random, lo, hi);
                                    gameRenderer.addAnnouncement("Fake Ping RANDOM: " + std::to_string(lo) + " .. " + std::to_string(hi) + " ms", 0xFACC15, 1);
                                    handledLocalCmd = true;
                                } else if (tokens[1] == "wave") {
                                    uint32_t lo = 10, hi = 500;
                                    try {
                                        if (tokens.size() >= 3) lo = parseFakePingU32(tokens[2]);
                                        if (tokens.size() >= 4) hi = parseFakePingU32(tokens[3]);
                                    } catch (...) {}
                                    client.setFakePingRange(HaxballClient::FakePingMode::Wave, lo, hi);
                                    gameRenderer.addAnnouncement("Fake Ping WAVE: " + std::to_string(lo) + " <-> " + std::to_string(hi) + " ms", 0x38BDF8, 1);
                                    handledLocalCmd = true;
                                } else if (tokens[1] == "count") {
                                    uint32_t lo = 0, hi = 1000;
                                    try {
                                        if (tokens.size() >= 3) lo = parseFakePingU32(tokens[2]);
                                        if (tokens.size() >= 4) hi = parseFakePingU32(tokens[3]);
                                    } catch (...) {}
                                    client.setFakePingRange(HaxballClient::FakePingMode::Count, lo, hi);
                                    gameRenderer.addAnnouncement("Fake Ping COUNT: " + std::to_string(lo) + " -> " + std::to_string(hi) + " ms", 0x34D399, 1);
                                    handledLocalCmd = true;
                                } else if (tokens[1] == "speed" && tokens.size() >= 3) {
                                    try {
                                        int spd = std::clamp(std::stoi(tokens[2]), 150, 2000);
                                        client.setFakePingIntervalMs(spd);
                                        gameRenderer.addAnnouncement("Fake Ping Speed: " + std::to_string(spd) + " ms", 0x38BDF8, 1);
                                        handledLocalCmd = true;
                                    } catch (...) {}
                                } else {
                                    try {
                                        uint32_t val = parseFakePingU32(tokens[1]);
                                        client.setFakePingMs(val);
                                        gameRenderer.addAnnouncement("Fake Ping set to: " + std::to_string(val) + " ms (visible to everyone, 0 real lag)", 0xFACC15, 1);
                                        handledLocalCmd = true;
                                    } catch (...) {}
                                }
                            } else if (msg == "/antiop" || msg == "/antiopmode") {
                                gameRenderer.assistant.antiOpMode = !gameRenderer.assistant.antiOpMode;
                                client.setAntiOpMode(gameRenderer.assistant.antiOpMode);
                                syncSettingsToConfig();
                                gameRenderer.addAnnouncement(
                                    gameRenderer.assistant.antiOpMode
                                        ? "Anti-OPMode: ON (opponents rendered smoothly at 0ms while you keep high extrap)"
                                        : "Anti-OPMode: OFF",
                                    gameRenderer.assistant.antiOpMode ? 0x34D399 : 0x94A3B8, 1);
                                handledLocalCmd = true;
                            } else if (msg.rfind("/ragequit", 0) == 0 || msg.rfind("/rq", 0) == 0) {
                                std::string farewell = "bye";
                                size_t sp = msg.find(' ');
                                if (sp != std::string::npos && sp + 1 < msg.size()) {
                                    farewell = msg.substr(sp + 1);
                                }
                                if (state == AppState::Multiplayer) {
                                    client.sendChat(farewell);
                                    leaveToBrowser();
                                } else {
                                    leaveToBrowser();
                                }
                                handledLocalCmd = true;
                            }
                            if (!handledLocalCmd) {
                                if (state == AppState::Multiplayer) {
                                    client.sendChat(msg);
                                } else {
                                    gameRenderer.addChatMessage(browser.getPlayerName(), msg, 0xFF9E8A, 1);
                                }
                            }
                            gameRenderer.chatInputBuffer.clear();
                        }
                        gameRenderer.isChatOpen = false;
                        gameRenderer.chatScrollOffset = 0;
                        if (state == AppState::Multiplayer) {
                            client.sendChatIndicator(gameRenderer.assistant.fakeTypingActive);
                        }
                    } else if (event.key.keysym.sym == SDLK_ESCAPE) {
                        gameRenderer.isChatOpen = false;
                        gameRenderer.chatScrollOffset = 0;
                        if (state == AppState::Multiplayer) {
                            client.sendChatIndicator(gameRenderer.assistant.fakeTypingActive);
                        }
                    } else if (event.key.keysym.sym == SDLK_BACKSPACE && !gameRenderer.chatInputBuffer.empty()) {
                        bool ctrlHeld = (SDL_GetModState() & (KMOD_CTRL | KMOD_GUI)) != 0;
                        if (ctrlHeld) {
                            deletePreviousWordUTF8(gameRenderer.chatInputBuffer);
                        } else {
                            std::string& s = gameRenderer.chatInputBuffer;
                            while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) {
                                s.pop_back();
                            }
                            if (!s.empty()) s.pop_back();
                        }
                    } else if (event.key.keysym.sym == SDLK_v && (SDL_GetModState() & (KMOD_CTRL | KMOD_GUI))) {
                        if (SDL_HasClipboardText()) {
                            char* clip = SDL_GetClipboardText();
                            if (clip) {
                                gameRenderer.chatInputBuffer += clip;
                                if (gameRenderer.chatInputBuffer.size() > 140) {
                                    gameRenderer.chatInputBuffer = gameRenderer.chatInputBuffer.substr(0, 140);
                                }
                                SDL_free(clip);
                            }
                        }
                    } else if (event.key.keysym.sym == SDLK_PAGEUP) {
                        gameRenderer.scrollChat(5);
                    } else if (event.key.keysym.sym == SDLK_PAGEDOWN) {
                        gameRenderer.scrollChat(-5);
                    } else if (event.key.keysym.sym == SDLK_UP) {
                        gameRenderer.scrollChat(1);
                    } else if (event.key.keysym.sym == SDLK_DOWN) {
                        gameRenderer.scrollChat(-1);
                    } else if (event.key.keysym.sym == SDLK_HOME && (SDL_GetModState() & (KMOD_CTRL | KMOD_GUI))) {
                        gameRenderer.scrollChat(999);
                    } else if (event.key.keysym.sym == SDLK_END && (SDL_GetModState() & (KMOD_CTRL | KMOD_GUI))) {
                        gameRenderer.chatScrollOffset = 0;
                    }
                } else if (event.type == SDL_MOUSEWHEEL) {
                    if (event.wheel.y > 0) {
                        gameRenderer.scrollChat(2);
                    } else if (event.wheel.y < 0) {
                        gameRenderer.scrollChat(-2);
                    }
                }
            } else {

                if (event.type == SDL_KEYDOWN) {

                    if (!gameRenderer.showSettingsPanel && cockpitMenu.isInitialized()) {
                        auto wPtr = (state == AppState::Multiplayer) ? client.getAuthoritativeWorld() : sandboxWorld;
                        OverlayAction x88Act = OverlayAction::None;
                        if (cockpitMenu.handleX88KeyDown(
                                event.key.keysym.sym,
                                static_cast<SDL_Keymod>(event.key.keysym.mod),
                                event.key.repeat != 0,
                                gameRenderer,
                                client,
                                wPtr.get(),
                                state == AppState::Multiplayer,
                                x88Act)) {
                            if (x88Act != OverlayAction::None) {
                                handleOverlayAction(x88Act);
                            }
                            continue;
                        }
                    }

                    uint32_t prevMask = currentKeyMask;
                    uint64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();
                    int extrapStep = (SDL_GetModState() & KMOD_SHIFT) ? 25 : 5;
                    const auto& cfg = AppConfig::get();

                    switch (event.key.keysym.sym) {
                        case SDLK_w:
                            if (!(rawPhysicalMask & 1u)) {
                                pressTimeUp = nowNs;
                                if ((rawPhysicalMask & 2u) && cfg.socdDelayTicks > 0) {
                                    socdHoldTicksRemainingY = cfg.socdDelayTicks;
                                }
                            }
                            rawPhysicalMask |= 1u;
                            break;
                        case SDLK_s:
                            if (!(rawPhysicalMask & 2u)) {
                                pressTimeDown = nowNs;
                                if ((rawPhysicalMask & 1u) && cfg.socdDelayTicks > 0) {
                                    socdHoldTicksRemainingY = cfg.socdDelayTicks;
                                }
                            }
                            rawPhysicalMask |= 2u;
                            break;
                        case SDLK_a:
                            if (!(rawPhysicalMask & 4u)) {
                                pressTimeLeft = nowNs;
                                if ((rawPhysicalMask & 8u) && cfg.socdDelayTicks > 0) {
                                    socdHoldTicksRemainingX = cfg.socdDelayTicks;
                                }
                            }
                            rawPhysicalMask |= 4u;
                            break;
                        case SDLK_d:
                            if (!(rawPhysicalMask & 8u)) {
                                pressTimeRight = nowNs;
                                if ((rawPhysicalMask & 4u) && cfg.socdDelayTicks > 0) {
                                    socdHoldTicksRemainingX = cfg.socdDelayTicks;
                                }
                            }
                            rawPhysicalMask |= 8u;
                            break;
                        case SDLK_SPACE:
                        case SDLK_x:
                        case SDLK_LCTRL:
                        case SDLK_RCTRL:
                        case SDLK_LSHIFT:
                        case SDLK_RSHIFT:
                            rawPhysicalMask |= 16u;
                            break;
                        case SDLK_RETURN:
                        case SDLK_KP_ENTER:
                        case SDLK_t:
                            if (!gameRenderer.assistant.fakeTypingActive) {
                                rawPhysicalMask = 0;
                                currentKeyMask = 0;
                                socdHoldTicksRemainingX = 0;
                                socdHoldTicksRemainingY = 0;
                                gameRenderer.assistant.holdingRealShotKey = false;
                                gameRenderer.assistant.holdingSlowKey = false;
                                gameRenderer.assistant.holdingShakeKey = false;
                                gameRenderer.assistant.holdingOrbitKey = false;
                                gameRenderer.assistant.cancelMacros();
                                if (state == AppState::Multiplayer) {
                                    client.sendInput(0);
                                    client.sendChatIndicator(true);
                                }
                                gameRenderer.isChatOpen = true;
                            }
                            break;
                        default: {
                            auto sym = event.key.keysym.sym;
                            if (sym == cfg.keyRealShot) {
                                gameRenderer.assistant.holdingRealShotKey = true;
                            } else if (sym == cfg.keySlowMove) {
                                gameRenderer.assistant.holdingSlowKey = true;
                            } else if (sym == cfg.keyAutoShake || sym == SDLK_b) {
                                gameRenderer.assistant.holdingShakeKey = true;
                            } else if (sym == cfg.keyBallOrbit || sym == SDLK_o) {
                                gameRenderer.assistant.holdingOrbitKey = true;
                            } else if (sym == SDLK_9) {
                                gameRenderer.assistant.holdingFastFakeKey = true;
                            } else if (sym == cfg.keyFakeTyping || sym == SDLK_j) {
                                if (event.key.repeat == 0) {
                                    gameRenderer.assistant.fakeTypingActive = !gameRenderer.assistant.fakeTypingActive;
                                    if (state == AppState::Multiplayer) {
                                        client.sendChatIndicator(gameRenderer.assistant.fakeTypingActive);
                                    }
                                    gameRenderer.addAnnouncement(
                                        gameRenderer.assistant.fakeTypingActive
                                            ? "💬 Fake Typing Troll [J]: ACTIVE (Showing '...' to everyone while you play!)"
                                            : "💬 Fake Typing Troll [J]: OFF",
                                        gameRenderer.assistant.fakeTypingActive ? 0x38BDF8 : 0x94A3B8, 1);
                                    syncSettingsToConfig();
                                }
                            } else if (sym == cfg.keyTypingSpam || sym == SDLK_u) {
                                if (event.key.repeat == 0) {
                                    gameRenderer.assistant.typingSpamEnabled = !gameRenderer.assistant.typingSpamEnabled;
                                    if (!gameRenderer.assistant.typingSpamEnabled && state == AppState::Multiplayer) {
                                        client.sendChatIndicator(gameRenderer.assistant.fakeTypingActive);
                                    }
                                    gameRenderer.addAnnouncement(
                                        gameRenderer.assistant.typingSpamEnabled
                                            ? "⚡ Typing Spammer [U]: ACTIVE (Strobe flashing '...'!)"
                                            : "⚡ Typing Spammer [U]: OFF",
                                        gameRenderer.assistant.typingSpamEnabled ? 0xFACC15 : 0x94A3B8, 1);
                                    syncSettingsToConfig();
                                }
                            } else if (sym == cfg.keySlide) {
                                if (event.key.repeat == 0 && gameRenderer.assistant.rsHelperEnabled) {
                                    gameRenderer.assistant.triggerSlideMacro();
                                    gameRenderer.addAnnouncement("👟 Slide Triggered!", 0x34D399, 1);
                                }
                            } else if (sym == cfg.keySprint || sym == cfg.keySprintAlt) {
                                if (event.key.repeat == 0) {
                                    if ((SDL_GetModState() & KMOD_SHIFT) || (!gameRenderer.assistant.rsRoomDetected && !gameRenderer.assistant.rsHelperEnabled)) {
                                        gameRenderer.settings.cameraMode = (gameRenderer.settings.cameraMode + 1) % 4;
                                    } else if (gameRenderer.assistant.rsHelperEnabled) {
                                        gameRenderer.assistant.triggerSprintMacro();
                                        gameRenderer.addAnnouncement("⚡ Sprint Triggered!", 0xFACC15, 1);
                                    } else {
                                        gameRenderer.settings.cameraMode = (gameRenderer.settings.cameraMode + 1) % 4;
                                    }
                                }
                            } else if (sym == cfg.keyFakeToggle) {
                                if (event.key.repeat == 0) {
                                    gameRenderer.assistant.fakeShotEnabled = !gameRenderer.assistant.fakeShotEnabled;
                                    gameRenderer.addAnnouncement(gameRenderer.assistant.fakeShotEnabled ? "Fake Shot: ENABLED" : "Fake Shot: DISABLED", 0xFFB932, 1);
                                    syncSettingsToConfig();
                                }
                            } else if (sym == SDLK_0) {

                                if (event.key.repeat == 0) {
                                    gameRenderer.assistant.autoGoalEnabled = !gameRenderer.assistant.autoGoalEnabled;
                                    syncSettingsToConfig();
                                }
                            } else if (sym == SDLK_1) {
                                if (event.key.repeat == 0) {
                                    gameRenderer.assistant.autoGoalAnyGoal = !gameRenderer.assistant.autoGoalAnyGoal;
                                    syncSettingsToConfig();
                                }
                            } else if (sym == SDLK_2) {
                                if (event.key.repeat == 0) {
                                    gameRenderer.assistant.antiOpMode = !gameRenderer.assistant.antiOpMode;
                                    client.setAntiOpMode(gameRenderer.assistant.antiOpMode);
                                    syncSettingsToConfig();
                                }
                            } else if (sym == SDLK_3) {
                                if (event.key.repeat == 0) {
                                    gameRenderer.assistant.antiFakeShotFilter = !gameRenderer.assistant.antiFakeShotFilter;
                                    client.setAntiFakeShot(gameRenderer.assistant.antiFakeShotFilter);
                                    syncSettingsToConfig();
                                }
                            } else if (sym == SDLK_4) {
                                if (event.key.repeat == 0) {
                                    gameRenderer.assistant.firstTouchCushionEnabled = !gameRenderer.assistant.firstTouchCushionEnabled;
                                    syncSettingsToConfig();
                                }
                            } else if (sym == SDLK_5) {
                                if (event.key.repeat == 0) {
                                    gameRenderer.assistant.fakeShotExtendEnabled = !gameRenderer.assistant.fakeShotExtendEnabled;
                                    syncSettingsToConfig();
                                }
                            } else if (sym == SDLK_6) {

                                if (event.key.repeat == 0) {
                                    gameRenderer.assistant.autoSaveEnabled = !gameRenderer.assistant.autoSaveEnabled;
                                    syncSettingsToConfig();
                                }
                            } else if (sym == SDLK_7) {

                                if (event.key.repeat == 0) {
                                    gameRenderer.assistant.smartGkEnabled = !gameRenderer.assistant.smartGkEnabled;
                                    if (gameRenderer.assistant.smartGkEnabled) {
                                        gameRenderer.assistant.penaltyGkEnabled = false;
                                    }
                                    gameRenderer.addAnnouncement(
                                        gameRenderer.assistant.smartGkEnabled ? "🧠 Smart GK [7]: ON" : "🧠 Smart GK [7]: OFF",
                                        gameRenderer.assistant.smartGkEnabled ? 0x38BDF8 : 0x94A3B8, 1);
                                }
                            } else if (sym == SDLK_8) {

                                if (event.key.repeat == 0) {
                                    gameRenderer.assistant.penaltyGkEnabled = !gameRenderer.assistant.penaltyGkEnabled;
                                    if (gameRenderer.assistant.penaltyGkEnabled) {
                                        gameRenderer.assistant.smartGkEnabled = false;
                                    }
                                    gameRenderer.addAnnouncement(
                                        gameRenderer.assistant.penaltyGkEnabled ? "🧤 Penalty GK Auto-Saver [8]: ON" : "🧤 Penalty GK Auto-Saver [8]: OFF",
                                        gameRenderer.assistant.penaltyGkEnabled ? 0x34D399 : 0x94A3B8, 1);
                                }
                            } else if (sym == SDLK_BACKSPACE && state == AppState::Sandbox && penaltyPracticeMode) {
                                if (event.key.repeat == 0) {
                                    resetPenaltyRound();
                                }
                            }
                            break;
                        }
                        case SDLK_TAB:
                            if (event.key.repeat == 0) {
                                gameRenderer.assistant.cancelMacros();
                                gameRenderer.showRoomMenu = !gameRenderer.showRoomMenu;
                            }
                            break;
                        case SDLK_ESCAPE:
                            if (event.key.repeat == 0) {
                                gameRenderer.assistant.cancelMacros();
                                if (gameRenderer.showSettingsPanel) {
                                    gameRenderer.showSettingsPanel = false;
                                } else {
                                    gameRenderer.showRoomMenu = !gameRenderer.showRoomMenu;
                                }
                            }
                            break;
                        case SDLK_INSERT:
                            if (event.key.repeat == 0) {
                                gameRenderer.showSettingsPanel = !gameRenderer.showSettingsPanel;
                            }
                            break;
                        case SDLK_F1:
                            if (event.key.repeat == 0) {
                                if (gameRenderer.showSettingsPanel && gameRenderer.activeMenuTab == 0) {
                                    gameRenderer.showSettingsPanel = false;
                                } else {
                                    gameRenderer.showSettingsPanel = true;
                                    gameRenderer.activeMenuTab = 0;
                                }
                            }
                            break;
                        case SDLK_F2:
                            if (event.key.repeat == 0) {
                                if (gameRenderer.showSettingsPanel && gameRenderer.activeMenuTab == 1) {
                                    gameRenderer.showSettingsPanel = false;
                                } else {
                                    gameRenderer.showSettingsPanel = true;
                                    gameRenderer.activeMenuTab = 1;
                                }
                            }
                            break;
                        case SDLK_F3:
                            if (event.key.repeat == 0) {
                                if (gameRenderer.showSettingsPanel && gameRenderer.activeMenuTab == 2) {
                                    gameRenderer.showSettingsPanel = false;
                                } else {
                                    gameRenderer.showSettingsPanel = true;
                                    gameRenderer.activeMenuTab = 2;
                                }
                            }
                            break;
                        case SDLK_r:
                            if (event.key.repeat == 0) {
                                gameRenderer.settings.fieldRotation = (gameRenderer.settings.fieldRotation + 1) % 5;
                                static const char* rotNames[] = {
                                    "0° Horizontal (Standard)",
                                    "90° Vertical ↑ (Red Bottom)",
                                    "180° Flipped Horizontal",
                                    "270° Vertical ↓ (Blue Bottom)",
                                    "Auto Vertical ↑ (Always Attack Up)"
                                };
                                gameRenderer.addAnnouncement(std::string("Field Orientation: ") + rotNames[gameRenderer.settings.fieldRotation] + " (WASD Auto-Adjusted)", 0x70E6FF, 1);
                            }
                            break;
                        case SDLK_F4:
                            gameRenderer.settings.showTeamColors = !gameRenderer.settings.showTeamColors;
                            break;
                        case SDLK_F5:
                            gameRenderer.settings.showPlayerIds = !gameRenderer.settings.showPlayerIds;
                            break;
                        case SDLK_F6:
                            gameRenderer.settings.showAvatars = !gameRenderer.settings.showAvatars;
                            break;
                        case SDLK_F7:
                            gameRenderer.settings.showLocalHalo = !gameRenderer.settings.showLocalHalo;
                            break;
                        case SDLK_F8:
                            gameRenderer.settings.showNetGraph = !gameRenderer.settings.showNetGraph;
                            break;
                        case SDLK_F9:
                            gameRenderer.settings.showInvisibleSegments = !gameRenderer.settings.showInvisibleSegments;
                            break;
                        case SDLK_LEFTBRACKET:
                        case SDLK_COMMA:
                            gameRenderer.settings.extrapolationMs = std::clamp(gameRenderer.settings.extrapolationMs - extrapStep, -200, 500);
                            client.setExtrapolationMs(gameRenderer.settings.extrapolationMs);
                            break;
                        case SDLK_RIGHTBRACKET:
                        case SDLK_PERIOD:
                            gameRenderer.settings.extrapolationMs = std::clamp(gameRenderer.settings.extrapolationMs + extrapStep, -200, 500);
                            client.setExtrapolationMs(gameRenderer.settings.extrapolationMs);
                            break;
                        case SDLK_p:
                            if ((SDL_GetModState() & KMOD_SHIFT) != 0) {
                                if (state == AppState::Multiplayer) {
                                    if (client.isLocalAdmin()) client.pauseGame();
                                } else if (sandboxWorld) {
                                    sandboxWorld->pauseResumeGame(sandboxWorld->pauseTickCounter != 120);
                                }
                            } else {
                                gameRenderer.assistant.holdingFastFakeKey = true;
                            }
                            break;
                        case SDLK_EQUALS:
                        case SDLK_PLUS:
                        case SDLK_KP_PLUS:
                            gameRenderer.setZoom(gameRenderer.getZoom() + 0.1f);
                            break;
                        case SDLK_MINUS:
                        case SDLK_KP_MINUS:
                            gameRenderer.setZoom(gameRenderer.getZoom() - 0.1f);
                            break;
                    }

                    currentKeyMask = resolveSOCD(rawPhysicalMask);

                    if (state == AppState::Multiplayer && currentKeyMask != prevMask) {
                        client.update();
                        int myTeam = 1;
                        if (auto w = client.getAuthoritativeWorld()) {
                            if (const auto* me = w->getPlayer(client.getPlayerId())) myTeam = me->team;
                        }
                        uint32_t remapped = gameRenderer.remapInputForRotation(currentKeyMask, myTeam);
                        uint32_t targetFrame = client.getTargetFrame();
                        if (auto w = client.getWorld()) {
                            auto realW = client.getRealExtrapWorld();
                            if (targetFrame != lastAssistantServerFrame) {
                                lastAssistantServerFrame = targetFrame;
                                gameRenderer.assistant.analyze(*w, realW.get(), client.getPlayerId());
                            }
                            remapped = gameRenderer.assistant.computeInputMask(*w, realW.get(), client.getPlayerId(), remapped, client.getLastSentKeyMask(), targetFrame);
                        }
                        uint32_t sendFrame = targetFrame;
                        if (gameRenderer.assistant.forcedReleaseTick == targetFrame &&
                            gameRenderer.assistant.activeKickPulseTick != 0xFFFFFFFFu) {
                            sendFrame = gameRenderer.assistant.activeKickPulseTick + 1;
                        }
                        bool oneTickPulse = gameRenderer.assistant.fakeShotJustPulsed || gameRenderer.assistant.kickJustPulsed;
                        client.sendInput(remapped, sendFrame, oneTickPulse);
                    }
                } else if (event.type == SDL_KEYUP) {
                    uint32_t prevMask = currentKeyMask;
                    const auto& cfg = AppConfig::get();

                    switch (event.key.keysym.sym) {
                        case SDLK_w:
                            rawPhysicalMask &= ~1u;
                            if ((rawPhysicalMask & 2u) && cfg.socdDelayTicks > 0) {
                                socdHoldTicksRemainingY = cfg.socdDelayTicks;
                            }
                            break;
                        case SDLK_s:
                            rawPhysicalMask &= ~2u;
                            if ((rawPhysicalMask & 1u) && cfg.socdDelayTicks > 0) {
                                socdHoldTicksRemainingY = cfg.socdDelayTicks;
                            }
                            break;
                        case SDLK_a:
                            rawPhysicalMask &= ~4u;
                            if ((rawPhysicalMask & 8u) && cfg.socdDelayTicks > 0) {
                                socdHoldTicksRemainingX = cfg.socdDelayTicks;
                            }
                            break;
                        case SDLK_d:
                            rawPhysicalMask &= ~8u;
                            if ((rawPhysicalMask & 4u) && cfg.socdDelayTicks > 0) {
                                socdHoldTicksRemainingX = cfg.socdDelayTicks;
                            }
                            break;
                        case SDLK_SPACE:
                        case SDLK_x:
                        case SDLK_LCTRL:
                        case SDLK_RCTRL:
                        case SDLK_LSHIFT:
                        case SDLK_RSHIFT:
                            rawPhysicalMask &= ~16u;
                            break;
                        default: {
                            if (event.key.keysym.sym == cfg.keyRealShot) {
                                gameRenderer.assistant.holdingRealShotKey = false;
                            } else if (event.key.keysym.sym == cfg.keySlowMove) {
                                gameRenderer.assistant.holdingSlowKey = false;
                            } else if (event.key.keysym.sym == cfg.keyAutoShake || event.key.keysym.sym == SDLK_b) {
                                gameRenderer.assistant.holdingShakeKey = false;
                            } else if (event.key.keysym.sym == cfg.keyBallOrbit || event.key.keysym.sym == SDLK_o) {
                                gameRenderer.assistant.holdingOrbitKey = false;
                            } else if (event.key.keysym.sym == SDLK_p || event.key.keysym.sym == SDLK_9) {
                                gameRenderer.assistant.holdingFastFakeKey = false;
                                gameRenderer.assistant.fastFakeRetreatTicksRemaining = 0;
                            }
                            break;
                        }
                    }

                    currentKeyMask = resolveSOCD(rawPhysicalMask);

                    if (state == AppState::Multiplayer && currentKeyMask != prevMask) {
                        client.update();
                        int myTeam = 1;
                        if (auto w = client.getAuthoritativeWorld()) {
                            if (const auto* me = w->getPlayer(client.getPlayerId())) myTeam = me->team;
                        }
                        uint32_t remapped = gameRenderer.remapInputForRotation(currentKeyMask, myTeam);
                        uint32_t targetFrame = client.getTargetFrame();
                        if (auto w = client.getWorld()) {
                            auto realW = client.getRealExtrapWorld();
                            if (targetFrame != lastAssistantServerFrame) {
                                lastAssistantServerFrame = targetFrame;
                                gameRenderer.assistant.analyze(*w, realW.get(), client.getPlayerId());
                            }
                            remapped = gameRenderer.assistant.computeInputMask(*w, realW.get(), client.getPlayerId(), remapped, client.getLastSentKeyMask(), targetFrame);
                        }
                        uint32_t sendFrame = targetFrame;
                        if (gameRenderer.assistant.forcedReleaseTick == targetFrame &&
                            gameRenderer.assistant.activeKickPulseTick != 0xFFFFFFFFu) {
                            sendFrame = gameRenderer.assistant.activeKickPulseTick + 1;
                        }
                        bool oneTickPulse = gameRenderer.assistant.fakeShotJustPulsed || gameRenderer.assistant.kickJustPulsed;
                        client.sendInput(remapped, sendFrame, oneTickPulse);
                    }
                } else if (event.type == SDL_MOUSEWHEEL) {
                    int mx = 0, my = 0;
                    SDL_GetMouseState(&mx, &my);
                    int screenW = 1280, screenH = 720;
                    SDL_GetRendererOutputSize(sdlRenderer, &screenW, &screenH);
                    float boxX = 14.0f;
                    float boxW = std::min(560.0f, screenW * 0.48f);
                    float chatY = screenH - 180.0f;
                    if (gameRenderer.showSettingsPanel) {
                        gameRenderer.scrollSettingsPanel(-event.wheel.y * 28.0f, screenH);
                    } else if (mx >= boxX && mx <= boxX + boxW && my >= chatY) {
                        if (event.wheel.y > 0) gameRenderer.scrollChat(2);
                        else if (event.wheel.y < 0) gameRenderer.scrollChat(-2);
                    } else {
                        if (event.wheel.y > 0) gameRenderer.setZoom(gameRenderer.getZoom() + 0.08f);
                        else if (event.wheel.y < 0) gameRenderer.setZoom(gameRenderer.getZoom() - 0.08f);
                    }
                }
            }
        }
    }
}

void App::drainClientEvents() {
    std::vector<ClientUiEvent> events;
    {
        std::lock_guard<std::mutex> lock(clientEventMutex);
        if (pendingClientEvents.empty()) return;
        events.swap(pendingClientEvents);
    }

    auto checkCooldownNotice = [this](const std::string& text) {
        if (text.find("Cooldown:") != std::string::npos) {
            int sec = 0;
            const char* p = std::strstr(text.c_str(), "Cooldown:");
            if (p && std::sscanf(p, "Cooldown: %ds", &sec) == 1) {
                gameRenderer.assistant.onCooldownNoticeReceived(sec);
            }
        }
        if (text.find("jakjus") != std::string::npos || text.find("slide") != std::string::npos || text.find("sprint") != std::string::npos) {
            gameRenderer.assistant.rsRoomDetected = true;
        }
    };

    for (const auto& ev : events) {
        switch (ev.type) {
            case ClientUiEvent::Type::Connected: {
                state = AppState::Multiplayer;
                sound.play(SoundEffect::Join);
                initialConnectedFrame = client.getCurrentFrame();
                connectedTestTimer = 0.0;

                auto w = client.getAuthoritativeWorld();
                std::string rName = w ? w->roomName : "Haxball Room";
                lastAssistantServerFrame = 0xFFFFFFFFu;
                gameRenderer.assistant.resetTickSync();
                gameRenderer.assistant.resetMatchPowerTracking(rName);
                gameRenderer.addAnnouncement("Connected to " + rName + "  ([F1] Network | [F2] Assist | [F3] Camera | [R] Rotate)", 0x88FF99, 1);
                break;
            }
            case ClientUiEvent::Type::Disconnected: {
                sound.play(SoundEffect::Leave);
                gameRenderer.addAnnouncement("Disconnected: " + ev.text, 0xFF8888, 1);
                state = AppState::Browser;
                break;
            }
            case ClientUiEvent::Type::Chat: {
                sound.play(SoundEffect::Chat);
                checkCooldownNotice(ev.text);
                uint32_t col = 0xF2F5FA;
                if (ev.isAdmin) col = 0xFFE270;
                else if (ev.team == 1) col = 0xFF9E8A;
                else if (ev.team == 2) col = 0x8AB8FF;
                gameRenderer.addChatMessage(ev.sender, ev.text, col, ev.team, ev.isAdmin);
                break;
            }
            case ClientUiEvent::Type::Announcement: {
                if (ev.sound != 0) {
                    sound.play(SoundEffect::Chat);
                }
                checkCooldownNotice(ev.text);
                gameRenderer.addAnnouncement(ev.text, ev.color, ev.style);
                break;
            }
            case ClientUiEvent::Type::GoalReset: {
                gameRenderer.assistant.onMatchReset();
                break;
            }
        }
    }
}

void App::processServerTickAssistant(double frameDt) {
    if (state != AppState::Multiplayer || client.getStatus() != ConnectionStatus::Connected) {
        return;
    }

    float targetExtrap = static_cast<float>(gameRenderer.settings.extrapolationMs);
    float smoothAlpha = std::clamp(static_cast<float>(frameDt * 14.0), 0.01f, 1.0f);
    currentSmoothedExtrapolation += (targetExtrap - currentSmoothedExtrapolation) * smoothAlpha;
    client.setExtrapolationMs(currentSmoothedExtrapolation);

    uint32_t targetFrame = client.getTargetFrame();
    if (targetFrame == lastAssistantServerFrame) {
        return;
    }
    lastAssistantServerFrame = targetFrame;

    uint32_t finalMask = currentKeyMask;
    if (auto w = client.getWorld()) {
        auto realW = client.getRealExtrapWorld();
        int myTeam = 1;
        if (const auto* me = w->getPlayer(client.getPlayerId())) myTeam = me->team;
        uint32_t worldManualMask = gameRenderer.remapInputForRotation(currentKeyMask, myTeam);
        gameRenderer.assistant.analyze(*w, realW.get(), client.getPlayerId());

        finalMask = gameRenderer.assistant.computeInputMask(
            *w, realW.get(), client.getPlayerId(), worldManualMask, client.getLastSentKeyMask(), targetFrame);

        frameRenderWorld = std::move(w);
        frameRealWorld = std::move(realW);
    }
    uint32_t sendFrame = targetFrame;
    if (gameRenderer.assistant.forcedReleaseTick == targetFrame &&
        gameRenderer.assistant.activeKickPulseTick != 0xFFFFFFFFu) {
        sendFrame = gameRenderer.assistant.activeKickPulseTick + 1;
    }
    bool oneTickPulse = gameRenderer.assistant.fakeShotJustPulsed || gameRenderer.assistant.kickJustPulsed;
    uint32_t prevSent = client.getLastSentKeyMask();
    client.sendInput(finalMask, sendFrame, oneTickPulse);
    if (finalMask != prevSent) {

        frameRenderWorld.reset();
        frameRealWorld.reset();
    }
}

void App::update(double dt) {
    gameRenderer.assistant.updateTimers(static_cast<float>(dt));

    if (socdHoldTicksRemainingX > 0) {
        socdHoldTicksRemainingX--;
        if (socdHoldTicksRemainingX == 0) {
            currentKeyMask = resolveSOCD(rawPhysicalMask);
        }
    }
    if (socdHoldTicksRemainingY > 0) {
        socdHoldTicksRemainingY--;
        if (socdHoldTicksRemainingY == 0) {
            currentKeyMask = resolveSOCD(rawPhysicalMask);
        }
    }

    if (state == AppState::Browser) {
        browser.update(static_cast<float>(dt));
    } else if (state == AppState::Connecting) {
        client.update();
        if (client.getStatus() == ConnectionStatus::Connected) {
            state = AppState::Multiplayer;
        } else if (client.getStatus() == ConnectionStatus::Failed) {
            state = AppState::Browser;
        }
    } else if (state == AppState::Multiplayer) {
        client.update();

        if (gameRenderer.assistant.typingSpamEnabled && !gameRenderer.isChatOpen) {
            client.sendChatIndicator(gameRenderer.assistant.typingSpamState);
        }

        gameRenderer.update(dt, client.getPingMs());
    } else if (state == AppState::Sandbox) {
        if (sandboxWorld) {
            uint32_t lastMask = 0;
            int myTeam = 1;
            if (auto* p = sandboxWorld->getPlayer(1)) {
                lastMask = p->input;
                myTeam = p->team;
            }
            uint32_t worldManualMask = gameRenderer.remapInputForRotation(currentKeyMask, myTeam);

            gameRenderer.assistant.analyze(
                sandboxRenderWorld ? *sandboxRenderWorld : *sandboxWorld,
                sandboxWorld.get(),
                1);

            uint32_t finalMask = gameRenderer.assistant.computeInputMask(
                sandboxRenderWorld ? *sandboxRenderWorld : *sandboxWorld,
                sandboxWorld.get(), 1, worldManualMask, lastMask, sandboxWorld->currentTick);

            sandboxWorld->setPlayerInput(1, finalMask);

            if (penaltyPracticeMode) {
                if (penaltyResetDelayTicks > 0) {
                    sandboxWorld->setPlayerInput(2, 0);
                    penaltyResetDelayTicks--;
                    if (penaltyResetDelayTicks == 0) {
                        resetPenaltyRound();
                    }
                } else {
                    uint32_t botMask = gameRenderer.assistant.computePenaltyGkInput(*sandboxWorld, 2);
                    if (!penaltyKickTaken) {
                        botMask &= ~12u;
                    }
                    if ((penaltyBotLastMask & 16u) != 0 && (botMask & 16u) != 0) {
                        botMask &= ~16u;
                    }
                    penaltyBotLastMask = botMask;
                    sandboxWorld->setPlayerInput(2, botMask);

                    if (!penaltyKickTaken && !sandboxWorld->discs.empty()) {
                        if (const auto* p1 = sandboxWorld->getPlayer(1)) {
                            if (p1->discIndex >= 0 && p1->discIndex < (int)sandboxWorld->discs.size()) {
                                const auto& p1D = sandboxWorld->discs[p1->discIndex];
                                const auto& bD = sandboxWorld->discs[0];
                                double gap = std::hypot(bD.pos.x - p1D.pos.x, bD.pos.y - p1D.pos.y) - (bD.radius + p1D.radius);
                                if (p1->isKicking && p1->kickCooldown <= 0 && gap < 4.0) {
                                    double stInvMass = (sandboxWorld->stadium && !sandboxWorld->stadium->discs.empty())
                                        ? sandboxWorld->stadium->discs[0].invMass : 1.235;
                                    sandboxWorld->discs[0].invMass = stInvMass;
                                }
                            }
                        }
                    }
                }
            }

            sandboxWorld->step(1.0);

            if (penaltyPracticeMode && !sandboxWorld->discs.empty()) {
                auto& ballD = sandboxWorld->discs[0];
                double bSpd = std::hypot(ballD.speed.x, ballD.speed.y);

                if (!penaltyKickTaken && !penaltyRoundFinished) {
                    ballD.pos = Vec2(935.0, 0.0);
                    ballD.speed = Vec2(0.0, 0.0);
                    ballD.invMass = 0.0;

                    if (auto* gkPl = sandboxWorld->getPlayer(2)) {
                        if (gkPl->discIndex >= 0 && gkPl->discIndex < (int)sandboxWorld->discs.size()) {
                            auto& gkD = sandboxWorld->discs[gkPl->discIndex];
                            gkD.pos.x = 1150.0;
                            gkD.speed.x = 0.0;
                        }
                    }

                    if (auto* p1 = sandboxWorld->getPlayer(1)) {
                        if (p1->discIndex >= 0 && p1->discIndex < (int)sandboxWorld->discs.size()) {
                            const auto& p1D = sandboxWorld->discs[p1->discIndex];
                            double p1Spd = std::hypot(p1D.speed.x, p1D.speed.y);
                            const auto& pp = sandboxWorld->stadium ? sandboxWorld->stadium->playerPhysics : PlayerPhysics{};
                            double vMax = (pp.damping < 1.0 && pp.damping > 0.5) ? (pp.acceleration / (1.0 - pp.damping)) : 2.97;
                            double minSpeed5Pct = vMax * 0.05;

                            if (!penaltyRunupStarted) {
                                if (p1Spd > vMax * 0.15) {
                                    penaltyRunupStarted = true;
                                    penaltyStopTicks = 0;
                                }
                            } else {
                                if (p1Spd <= minSpeed5Pct) {
                                    penaltyStopTicks++;
                                    if (penaltyStopTicks >= 2) {
                                        gameRenderer.addAnnouncement("⛔ SPEED <= 5%! Penalty reset.", 0xF59E0B, 1);
                                        resetPenaltyRound();
                                    }
                                } else {
                                    penaltyStopTicks = 0;
                                }
                            }
                        }
                    }

                    if (!penaltyRoundFinished) {
                        penaltyRoundTicks++;
                        if (penaltyRoundTicks >= 600) {
                            penaltyRoundFinished = true;
                            penaltyResetDelayTicks = 70;
                            penaltyMisses++;
                            gameRenderer.addAnnouncement(
                                "⏱️ TIME UP (10s)!  (Goals: " + std::to_string(penaltyGoals) +
                                " | Saved: " + std::to_string(penaltySaves) +
                                " | Missed: " + std::to_string(penaltyMisses) + ")",
                                0xF59E0B, 1);
                        }
                    }
                } else if (penaltyKickTaken && !penaltyRoundFinished) {
                    penaltyPostKickTicks++;

                    if (auto* gkPl = sandboxWorld->getPlayer(2)) {
                        if (gkPl->discIndex >= 0 && gkPl->discIndex < (int)sandboxWorld->discs.size()) {
                            const auto& gkD = sandboxWorld->discs[gkPl->discIndex];
                            double gkDist = std::hypot(ballD.pos.x - gkD.pos.x, ballD.pos.y - gkD.pos.y);
                            if (gkDist <= gkD.radius + ballD.radius + 4.5) {
                                penaltyGkTouched = true;
                            }
                        }
                    }

                    bool outWideOrBehind = (ballD.pos.x > 1158.0 && std::abs(ballD.pos.y) > 124.0) || std::abs(ballD.pos.y) > 340.0;
                    bool clearedBack = (penaltyGkTouched && ballD.speed.x < -0.25 && penaltyPostKickTicks > 6);
                    bool stoppedDead = (penaltyPostKickTicks > 25 && bSpd < 0.25) || (penaltyPostKickTicks > 150) || (ballD.pos.x < 850.0);

                    if (outWideOrBehind || clearedBack || stoppedDead) {
                        penaltyRoundFinished = true;
                        penaltyResetDelayTicks = 75;
                        if (penaltyGkTouched) {
                            penaltySaves++;
                            gameRenderer.addAnnouncement(
                                "🧤 SAVED BY GK BOT!  (Goals: " + std::to_string(penaltyGoals) +
                                " | Saved: " + std::to_string(penaltySaves) +
                                " | Missed: " + std::to_string(penaltyMisses) + ")",
                                0x38BDF8, 1);
                        } else {
                            penaltyMisses++;
                            gameRenderer.addAnnouncement(
                                "❌ MISSED!  (Goals: " + std::to_string(penaltyGoals) +
                                " | Saved: " + std::to_string(penaltySaves) +
                                " | Missed: " + std::to_string(penaltyMisses) + ")",
                                0xEF4444, 1);
                        }
                    }
                }
            }

            sandboxHistory.push_back(sandboxWorld->cloneState());
            if (sandboxHistory.size() > 60) {
                sandboxHistory.pop_front();
            }

            if (!sandboxRenderWorld) {
                sandboxRenderWorld = std::make_shared<World>(sandboxWorld->cloneState());
            }

            float targetExtrap = static_cast<float>(gameRenderer.settings.extrapolationMs);
            float smoothAlpha = std::clamp(static_cast<float>(dt * 14.0), 0.05f, 1.0f);
            currentSmoothedExtrapolation += (targetExtrap - currentSmoothedExtrapolation) * smoothAlpha;

            int extrapTicks = static_cast<int>(std::round(currentSmoothedExtrapolation * 0.06));
            if (extrapTicks > 0) {
                *sandboxRenderWorld = sandboxWorld->cloneState();
                int ticksAhead = std::min(extrapTicks, 60);

                if (auto* rp = sandboxRenderWorld->getPlayer(1)) {
                    if (gameRenderer.assistant.antiFakeShotFilter) {
                        double minRealGap = 1e9;
                        if (!sandboxWorld->discs.empty() && rp->discIndex >= 0 && rp->discIndex < (int)sandboxWorld->discs.size()) {
                            const auto& pD = sandboxWorld->discs[rp->discIndex];
                            const auto& bD = sandboxWorld->discs[0];
                            minRealGap = std::hypot(bD.pos.x - pD.pos.x, bD.pos.y - pD.pos.y) - bD.radius - pD.radius;
                        }
                        rp->allowSpeculativeKick = (minRealGap < 4.0);
                    } else {
                        rp->allowSpeculativeKick = true;
                    }
                }

                bool releaseNextTick = gameRenderer.assistant.antiFakeShotFilter &&
                                       (gameRenderer.assistant.fakeShotJustPulsed || gameRenderer.assistant.kickJustPulsed);
                for (int t = 1; t <= ticksAhead; ++t) {
                    uint32_t stepMask = releaseNextTick ? (finalMask & ~16u)
                        : (t == 1 ? finalMask : (finalMask | (sandboxRenderWorld->getPlayer(1) ? (sandboxRenderWorld->getPlayer(1)->input & 16u) : 0u)));
                    sandboxRenderWorld->setPlayerInput(1, stepMask);
                    sandboxRenderWorld->step(1.0);
                }
            } else if (extrapTicks < 0 && !sandboxHistory.empty()) {
                int pastIdx = std::clamp((int)sandboxHistory.size() - 1 + extrapTicks, 0, (int)sandboxHistory.size() - 1);
                *sandboxRenderWorld = sandboxHistory[pastIdx].cloneState();
            } else {
                *sandboxRenderWorld = sandboxWorld->cloneState();
            }
        }
        gameRenderer.update(dt, 0);
    }

    if (testMode) {
        testTimer += dt;
        if (state == AppState::Multiplayer && client.getStatus() == ConnectionStatus::Connected) {
            connectedTestTimer += dt;
            if (connectedTestTimer >= 5.0) {
                auto w = client.getAuthoritativeWorld();
                uint32_t curFrame = client.getCurrentFrame();
                std::cout << "[TestMode] Connected to '" << (w ? w->roomName : "") << "' | "
                          << "Players: " << (w ? w->players.size() : 0) << " | "
                          << "Discs: " << (w ? w->discs.size() : 0) << " | "
                          << "Stadium: '" << (w && w->stadium ? w->stadium->name : "") << "' ("
                          << (w && w->stadium ? w->stadium->segments.size() : 0) << " segs, "
                          << (w && w->stadium ? w->stadium->joints.size() : 0) << " joints) | "
                          << "Frame advanced: " << initialConnectedFrame << " -> " << curFrame
                          << " (+" << (curFrame - initialConnectedFrame) << " ticks) | "
                          << "Ping: " << client.getPingMs() << "ms | "
                          << "FPS: " << fps << std::endl;
                isRunning = false;
            }
        } else if (state == AppState::Sandbox && testTimer >= 2.5) {
            Vec2 ballPos = (!sandboxWorld->discs.empty()) ? sandboxWorld->discs[0].pos : Vec2(0.0, 0.0);
            Vec2 ballSpd = (!sandboxWorld->discs.empty()) ? sandboxWorld->discs[0].speed : Vec2(0.0, 0.0);
            Vec2 myPos(0.0, 0.0);
            if (auto* p1 = sandboxWorld->getPlayer(1)) {
                if (p1->discIndex >= 0 && p1->discIndex < (int)sandboxWorld->discs.size()) {
                    myPos = sandboxWorld->discs[p1->discIndex].pos;
                }
            }
            std::cout << "[TestMode] Sandbox physics & TacticalAssistant ("
                      << "manualAimPts=" << gameRenderer.assistant.manualAimPath.size()
                      << ", myPos=(" << myPos.x << "," << myPos.y << ")"
                      << ", ballPos=(" << ballPos.x << "," << ballPos.y << ")"
                      << ", ballSpeed=(" << ballSpd.x << "," << ballSpd.y << ")"
                      << ", fakeShot=" << (gameRenderer.assistant.fakeShotEnabled ? "ON" : "OFF")
                      << ") verified! Exiting test." << std::endl;
            isRunning = false;
        } else if (testTimer >= 18.0) {
            std::cerr << "[TestMode] Timed out waiting for connection." << std::endl;
            isRunning = false;
        }
    }
}

void App::render(double frameDt) {
    int screenW = 1280, screenH = 720;
    SDL_GetRendererOutputSize(sdlRenderer, &screenW, &screenH);

    if (state == AppState::Browser) {
        browser.render(sdlRenderer, screenW, screenH);
    } else if (state == AppState::Connecting) {
        SDL_SetRenderDrawColor(sdlRenderer, 16, 20, 28, 255);
        SDL_RenderClear(sdlRenderer);

        GameRenderer::drawRoundedRect(sdlRenderer, screenW * 0.5f - 230.0f, screenH * 0.5f - 70.0f, 460.0f, 140.0f, 10.0f, {24, 30, 44, 245}, {75, 135, 220, 220}, 1.8f);
        connectingFont.drawTextShadow(sdlRenderer, client.getStatusText(), screenW * 0.5f, screenH * 0.5f - 28.0f, {235, 242, 255, 255}, TextAlign::Center);
        connectingFont.drawText(sdlRenderer, "Press ESC to return to Room Browser", screenW * 0.5f, screenH * 0.5f + 22.0f, {145, 165, 190, 255}, TextAlign::Center, 0.72f);
    } else if (state == AppState::Multiplayer) {
        std::shared_ptr<World> w = std::move(frameRenderWorld);
        std::shared_ptr<World> realW = std::move(frameRealWorld);
        if (!w) {
            w = client.getWorld();
            realW = client.getRealExtrapWorld();
            if (w) {
                gameRenderer.assistant.analyze(*w, realW.get(), client.getPlayerId());
            }
        }
        if (w) {
            gameRenderer.renderWorld(sdlRenderer, *w, client.getPlayerId(), client.getPingMs(), client.getMaxPingMs(), fps, frameDt, false, realW.get());
        }
        if ((gameRenderer.showSettingsPanel || cockpitMenu.shouldRenderOverlay()) && cockpitMenu.isInitialized()) {
            auto act = cockpitMenu.render(gameRenderer, client, w.get(), client.getPingMs(), fps, false, screenW, screenH);
            if (act != OverlayAction::None) handleOverlayAction(act);
        }
    } else if (state == AppState::Sandbox) {
        const World* activeW = sandboxRenderWorld ? sandboxRenderWorld.get() : sandboxWorld.get();
        if (sandboxRenderWorld) {
            gameRenderer.assistant.analyze(*sandboxRenderWorld, sandboxWorld.get(), 1);
            gameRenderer.renderWorld(sdlRenderer, *sandboxRenderWorld, 1, 0, 0, fps, frameDt, true, sandboxWorld.get());
        } else if (sandboxWorld) {
            gameRenderer.assistant.analyze(*sandboxWorld, sandboxWorld.get(), 1);
            gameRenderer.renderWorld(sdlRenderer, *sandboxWorld, 1, 0, 0, fps, frameDt, true, sandboxWorld.get());
        }
        if ((gameRenderer.showSettingsPanel || cockpitMenu.shouldRenderOverlay()) && cockpitMenu.isInitialized()) {
            auto act = cockpitMenu.render(gameRenderer, client, activeW, 0, fps, true, screenW, screenH);
            if (act != OverlayAction::None) handleOverlayAction(act);
        }
    }

    SDL_RenderPresent(sdlRenderer);

    frameCount++;
    Uint32 now = SDL_GetTicks();
    if (now - fpsTimer >= 1000) {
        fps = frameCount;
        frameCount = 0;
        fpsTimer = now;
    }
}

void App::run() {
    using clock = std::chrono::steady_clock;
    auto lastTime = clock::now();
    double accumulator = 0.0;
    const double fixedDt = 1.0 / 60.0;

    while (isRunning) {
        auto currentTime = clock::now();
        double frameTime = std::chrono::duration<double>(currentTime - lastTime).count();
        lastTime = currentTime;

        if (frameTime > 0.25) frameTime = 0.25;
        accumulator += frameTime;

        drainClientEvents();
        handleEvents();

        processServerTickAssistant(frameTime);

        while (accumulator >= fixedDt) {
            update(fixedDt);
            accumulator -= fixedDt;
        }

        render(frameTime);

        if (!gameRenderer.settings.vsync && gameRenderer.settings.fpsCap > 0) {
            double targetFrameSec = 1.0 / gameRenderer.settings.fpsCap;
            auto elapsed = std::chrono::duration<double>(clock::now() - currentTime).count();
            if (elapsed < targetFrameSec) {
                double sleepMs = (targetFrameSec - elapsed) * 1000.0;
                if (sleepMs > 2.2) {
                    SDL_Delay(static_cast<Uint32>(sleepMs - 1.5));
                }
                while (std::chrono::duration<double>(clock::now() - currentTime).count() < targetFrameSec) {
                    std::this_thread::yield();
                }
            }
        }
    }
}

}
