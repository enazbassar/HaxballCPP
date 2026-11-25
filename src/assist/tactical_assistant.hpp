#pragma once

#include "../math/vec2.hpp"
#include "../physics/world.hpp"
#include <vector>
#include <cstdint>
#include <string>
#include <mutex>

namespace haxball {

struct GoalShotOption {
    bool available = false;
    Vec2 targetPos{0.0, 0.0};
    Vec2 shotDir{1.0, 0.0};
    double distance = 0.0;
    double openAngleDeg = 0.0;
    std::string targetDesc;
    double alignment = 0.0;
    bool isAligned = false;
};

class TacticalAssistant {
public:

    bool showLiveAimTrajectory = true;
    bool showBallRollTrajectory = true;
    bool showGkDangerCone = true;
    bool showOtherGoalsCone = true;
    bool autoGoalEnabled = true;
    bool autoGoalAnyGoal = false;
    int32_t autoGoalExcludeColor = -1;
    int autoGoalAccuracy = 95;
    int autoGoalHoldTicks = 0;
    bool autoSaveEnabled = true;
    int autoSaveHoldTicks = 0;
    bool penaltyGkEnabled = false;
    bool smartGkEnabled = false;
    int aimLineDistance = 40;
    bool fakeShotEnabled = true;
    bool fakeShotExtendEnabled = true;
    int fakeShotExtendTicks = 0;
    int fakeShotHoldRemainingTicks = 0;
    bool realShotEnabled = true;
    bool antiFakeShotFilter = true;
    bool antiOpMode = false;
    bool firstTouchCushionEnabled = true;
    bool fastFakeEnabled = true;
    bool holdingFastFakeKey = false;
    int fastFakeSpeedTicks = 2;
    int fastFakeRetreatTicksRemaining = 0;

    float lineOpacity = 0.55f;
    float lineThickness = 2.4f;
    int trajectoryPreviewTicks = 85;
    int fakeShotIntervalMs = 500;
    int realShotIntervalMs = 60;
    int shotIntervalMs = 60;

    bool holdingRealShotKey = false;
    bool holdingSlowKey = false;
    int slowSpeedPercent = 50;
    int slowAccumulator = 0;
    bool autoShakeEnabled = true;
    int autoShakeFrequencyTicks = 4;
    bool holdingShakeKey = false;
    bool isAutoShaking = false;
    int autoShakeTickCounter = 0;

    bool ballOrbitEnabled = true;
    int ballOrbitDistance = 0;
    bool holdingOrbitKey = false;
    bool isOrbitingBall = false;
    int orbitDirection = 1;

    bool fakeTypingActive = false;

    bool typingSpamEnabled = false;
    int typingSpamSpeedMs = 100;
    float typingSpamTimer = 0.0f;
    bool typingSpamState = false;

    bool isFakeShooting = false;
    bool isRealShooting = false;
    bool suppressLocalKickHalo = false;
    bool fakeShotJustPulsed = false;
    bool kickJustPulsed = false;
    int kickCooldownWaitTicks = 0;
    int fakeShotCooldownRemainingTicks = 0;
    int realShotCadenceCounter = 0;
    int fakeShotCadenceCounter = 0;

    uint32_t lastProcessedTick = 0xFFFFFFFFu;
    uint32_t activeKickPulseTick = 0xFFFFFFFFu;
    uint32_t forcedReleaseTick = 0xFFFFFFFFu;
    int suppressHaloReleaseTicks = 0;
    bool lastSlowMoveAllowed = true;
    void resetTickSync();

    std::vector<Vec2> ballRollPath;
    std::vector<Vec2> manualAimPath;
    bool manualAimIsNearBall = false;
    bool manualAimCanKick = false;

    enum class RSSuperpowerState {
        None,
        Charging,
        SlideReady,
        SprintReady,
        Cancelled
    };

    bool rsHelperEnabled = true;
    bool rsOneTouchSlideEnabled = true;
    bool rsOneTouchSprintEnabled = true;
    bool rsRoomDetected = false;

    bool rsMacroSlideTriggered = false;
    bool rsMacroSprintTriggered = false;
    int rsMacroElapsedTicks = 0;
    int rsManualKickTicks = 0;

    float rsCooldownRemainingMs = 0.0f;
    RSSuperpowerState rsCurrentState = RSSuperpowerState::None;
    float rsChargeProgress = 0.0f;

    bool rsIsPowerAvailable() const { return rsCooldownRemainingMs <= 0.0f; }
    bool rsIsSlideAvailable() const { return true; }
    bool rsIsSprintAvailable() const { return true; }

    void triggerSlideMacro();
    void triggerSprintMacro();
    void cancelMacros();
    void onCooldownNoticeReceived(int seconds);
    void updateTimers(float dtSeconds);

    void resetMatchPowerTracking(const std::string& roomName);
    void onMatchReset();
    void onAuthoritativePlayerKick(const BallKickEvent& ev);
    void onAuthoritativeBallSpeedSet(uint32_t tick, const Vec2& newBallVel);

    void analyze(const World& renderWorld, const World* realWorld, uint32_t myPlayerId);

    void simulateBallTrajectory(const World& world, Vec2 pos, Vec2 vel, int maxTicks, std::vector<Vec2>& outPoints) const;

    uint32_t computeInputMask(const World& renderWorld, const World* realWorld, uint32_t myPlayerId, uint32_t manualKeyMask, uint32_t lastSentMask, uint32_t simTick = 0xFFFFFFFFu);

    uint32_t computePenaltyGkInput(const World& authW, uint32_t gkPlayerId) const;

    uint32_t computeSmartGkInput(const World& authW, uint32_t gkPlayerId) const;

private:
    static constexpr int kMaxPassChainBins = 8;
    mutable std::mutex powerMutex;
    bool isLdcCustomRoom = false;
    bool ldcBoostObserved = false;
    bool useMultiplicativeBoost = true;
    int modelVoteMult = 0;
    int modelVoteImp = 0;

    int passChainTeam = 0;
    int passChainCount = 0;
    uint32_t lastKickerId = 0;
    int lastKickerTeam = 0;
    uint32_t lastKickTick = 0;
    uint32_t lastTrackedWorldTick = 0;
    bool wasMatchPaused = false;
    bool pendingAutoExcludeRefresh = false;
    int lastPlayerDiscIndex = -1;
    std::vector<uint32_t> lastStadiumGoalColors;

    const Stadium* cachedRollStadium = nullptr;
    Vec2 cachedRollBallPos{1e18, 1e18};
    Vec2 cachedRollBallVel{1e18, 1e18};
    int cachedRollTicks = -1;
    bool cachedRollValid = false;

    const Stadium* cachedAimStadium = nullptr;
    Vec2 cachedAimBallPos{1e18, 1e18};
    Vec2 cachedAimBallVel{1e18, 1e18};
    Vec2 cachedAimMyPos{1e18, 1e18};
    int cachedAimTicks = -1;
    int cachedAimTeam = -1;
    int cachedAimChain = -1;
    bool cachedAimValid = false;

    bool hasPendingKickCalib = false;
    BallKickEvent pendingKickEv{};
    int pendingKickChain = 0;

    double chainMult[kMaxPassChainBins] = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
    double chainImpMult[kMaxPassChainBins] = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
    bool chainCalibrated[kMaxPassChainBins] = {false, false, false, false, false, false, false, false};

    Vec2 predictPostKickBallVelocity(const World& authW, const Disc& ball, const Vec2& ballVel, const Vec2& kickNormal,
                                     int myTeam, uint32_t myPlayerId, bool* outCalibrated = nullptr, int* outNextChain = nullptr) const;
    double evaluateAutoGoalAccuracy(const World& authW, uint32_t myPlayerId, int myTeam,
                                    const Vec2& pPos, const Vec2& bPos, const Vec2& bVel,
                                    int timeShiftTicks, double speedScale = 1.0) const;

public:
    static uint32_t getGoalColor(const Stadium& stadium, const Goal& g);
    static std::vector<uint32_t> getAvailableGoalColors(const Stadium& stadium);
};

}
