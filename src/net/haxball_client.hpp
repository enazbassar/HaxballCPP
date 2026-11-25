#pragma once

#include "buffer.hpp"
#include "compression.hpp"
#include "crypto_auth.hpp"
#include "../physics/world.hpp"

#include <rtc/rtc.hpp>
#include <rtc/websocket.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <functional>
#include <chrono>
#include <mutex>
#include <atomic>

namespace haxball {

enum class ConnectionStatus {
    Disconnected,
    ConnectingSignaling,
    NegotiatingWebRTC,
    Authenticating,
    Synchronizing,
    Connected,
    Failed
};

class WeightedQuantileFilter {
public:
    explicit WeightedQuantileFilter(size_t maxSamples = 50) : maxCount(maxSamples) {}

    void clear() {
        samples.clear();
        totalWeight = 0.0;
    }

    bool empty() const { return samples.empty(); }

    void add(double value) {
        totalWeight = 0.0;
        size_t oldestIdx = 0;
        for (size_t i = 0; i < samples.size(); ++i) {
            samples[i].age++;
            samples[i].weight *= 0.97;
            if (samples[oldestIdx].age < samples[i].age) {
                oldestIdx = i;
            }
            totalWeight += samples[i].weight;
        }

        Sample s{value, 1.0, 0};
        if (samples.size() >= maxCount) {
            totalWeight -= samples[oldestIdx].weight;
            samples.erase(samples.begin() + oldestIdx);
        }
        totalWeight += s.weight;

        auto it = std::lower_bound(samples.begin(), samples.end(), s,
            [](const Sample& a, const Sample& b) { return a.value < b.value; });
        samples.insert(it, s);
    }

    double quantile(double q) const {
        if (samples.empty()) return 0.0;
        if (samples.size() == 1) return samples[0].value;
        double target = q * totalWeight;
        double accum = samples[0].weight;
        size_t idx = 0;
        while (idx < samples.size() - 1 && accum < target) {
            accum += samples[++idx].weight;
        }
        return samples[idx].value;
    }

    double max() const {
        return samples.empty() ? 0.0 : samples.back().value;
    }

private:
    struct Sample {
        double value = 0.0;
        double weight = 1.0;
        int age = 0;
    };
    size_t maxCount = 50;
    std::vector<Sample> samples;
    double totalWeight = 0.0;
};

struct GameAction {
    uint32_t frame = 0;
    uint32_t order = 0;
    uint16_t senderId = 0;
    uint32_t clientSeq = 0;
    uint8_t eventType = 0;

    bool isPredicted = false;
    bool isTimed = false;

    std::string strVal;
    std::optional<std::string> optStr1;
    std::optional<std::string> optStr2;
    std::optional<std::string> optStr3;
    int32_t i32Val1 = 0;
    int32_t i32Val2 = 0;
    int32_t i32Val3 = 0;
    uint32_t u32Val = 0;
    uint8_t u8Val1 = 0;
    uint8_t u8Val2 = 0;
    bool boolVal = false;
    bool hasExtraChatInd = false;

    std::vector<uint8_t> rawBytes;
    std::vector<uint32_t> u32List;
    std::vector<int32_t> i32List;
    std::shared_ptr<Stadium> stadium;
    TeamColors teamColors;
    std::optional<float> fProps[10];
    std::optional<int32_t> iProps[3];

    void deserializePayload(::BufferReader& r, bool isSinglePacket);
};

struct AnnouncementMsg {
    std::string text;
    uint32_t color = 0xFFFFFF;
    uint8_t style = 0;
    uint8_t sound = 1;
};

class HaxballClient {
public:
    HaxballClient();
    ~HaxballClient();

    void connect(const std::string& roomId, const std::string& name, const std::string& flag = "nl", const std::string& avatar = "", const std::string& password = "", const std::string& authKey = "");
    void setAuthKey(const std::string& key) { activeAuthKey = key; }
    std::string getAuthKey() const { return activeAuthKey; }
    void disconnect();
    void update();

    void sendInput(uint32_t keyMask, std::optional<uint32_t> explicitFrame = std::nullopt, bool isOneTickPulse = false);
    void sendChat(const std::string& msg);
    void sendChatIndicator(bool active);
    void setAvatar(const std::string& avatar);
    void setTeam(int teamId);
    void setPlayerTeam(uint32_t playerId, int teamId);
    void startGame();
    void stopGame();
    void pauseGame();
    void autoTeams();
    void toggleLockTeams();

    void setExtrapolationMs(double ms) { extrapolationMs = std::clamp(ms, -200.0, 500.0); }
    double getExtrapolationMs() const { return extrapolationMs; }

    void setAntiFakeShot(bool enabled) { antiFakeShot = enabled; }
    bool getAntiFakeShot() const { return antiFakeShot; }

    void setAntiOpMode(bool enabled) { antiOpMode = enabled; }
    bool getAntiOpMode() const { return antiOpMode; }

    enum class FakePingMode {
        Off,
        Static,
        Flash,
        Random,
        Wave,
        Count
    };

    void setFakePingMs(std::optional<uint32_t> ms) {
        std::lock_guard<std::recursive_mutex> lock(clientMutex);
        fakePingMs = ms;
        fakePingMode = ms.has_value() ? FakePingMode::Static : FakePingMode::Off;
        fakePingStep = 0;
    }
    void setFakePingFlash(std::vector<uint32_t> seq) {
        std::lock_guard<std::recursive_mutex> lock(clientMutex);
        if (seq.empty()) seq = {9999, 666, 420, 69};
        fakePingList = std::move(seq);
        fakePingMode = FakePingMode::Flash;
        fakePingStep = 0;
    }
    void setFakePingRange(FakePingMode mode, uint32_t minVal, uint32_t maxVal) {
        std::lock_guard<std::recursive_mutex> lock(clientMutex);
        fakePingMode = mode;
        fakePingMin = minVal;
        fakePingMax = maxVal;
        fakePingStep = 0;
    }
    void setFakePingIntervalMs(int ms) {
        std::lock_guard<std::recursive_mutex> lock(clientMutex);
        fakePingIntervalMs = std::clamp(ms, 150, 2000);
    }
    std::optional<uint32_t> getFakePingMs() const {
        std::lock_guard<std::recursive_mutex> lock(clientMutex);
        return fakePingMs;
    }

    void setVerbose(bool v) { verbose = v; }
    bool isVerbose() const { return verbose; }

    void setHandicapMs(int ms) {
        handicapMs = std::clamp(ms, 0, 300);
        handicapFrames = static_cast<int>(handicapMs * 0.06);
    }
    int getHandicapMs() const { return handicapMs; }

    bool isLocalAdmin() const;
    bool canChangeOwnTeam() const;
    uint32_t getLastSentKeyMask() const { return lastSentKeyMask.load(std::memory_order_relaxed); }

    ConnectionStatus getStatus() const { return status; }
    std::string getStatusText() const {
        std::lock_guard<std::recursive_mutex> lock(clientMutex);
        return statusMessage;
    }
    uint32_t getPlayerId() const { return myPlayerId; }
    int getPingMs() const { return pingMs; }
    int getMaxPingMs() const { return maxPingMs; }
    uint32_t getCurrentFrame() const { return currentFrame; }
    uint32_t getTargetFrame() const;
    double getPredictedFramesAhead() const { return dD; }

    std::shared_ptr<World> getWorld();
    std::shared_ptr<World> getRealExtrapWorld() const;
    std::shared_ptr<World> getAuthoritativeWorld() const;

    std::function<void(const std::string& sender, int team, bool isAdmin, const std::string& message)> onChatReceived;
    std::function<void(const AnnouncementMsg& ann)> onAnnouncement;
    std::function<void(const std::string& reason)> onDisconnected;
    std::function<void()> onConnected;
    std::function<void()> onKickSound;
    std::function<void(int scoringTeam)> onGoalSound;
    std::function<void(const BallKickEvent&)> onPlayerBallKick;
    std::function<void(uint32_t tick, const Vec2& newBallVel)> onServerBallSpeedSet;

private:
    double getNowMs() const;
    void sendPing();
    void sendAction(uint8_t eventType, bool isTimed, bool isPredicted, const std::function<void(BufferWriter&)>& writePayload, const GameAction* localPred = nullptr, std::optional<uint32_t> explicitFrame = std::nullopt);

    void handleChannelMessage(int channelIdx, const uint8_t* data, size_t size);
    void handleChallenge(BufferReader& r);
    void handleGameState(BufferReader& r);
    void handleBroadcastAction(BufferReader& r);
    void handleFrameSync(BufferReader& r);
    void handlePingReply(BufferReader& r);
    void handleDisconnect(BufferReader& r);
    void handleImmediateAction(BufferReader& r);

    GameAction readBroadcastAction(BufferReader& r, bool isSinglePacket);
    void enqueueAction(GameAction act);
    void processSyncQueue();
    void stepAuthoritative(uint32_t steps);
    void applyAction(World& w, const GameAction& act, bool isAuthoritative);
    void rebuildRenderWorld();
    double computeOffsetLocked(double nowMs) const;
    uint32_t computeTargetFrameLocked(double nowMs) const;

    struct OpponentFakeMem {
        bool prevIsKicking = false;
        int32_t prevKickCooldown = 0;
        double prevGap = 999.0;
        double kickStartGap = 999.0;
        int consecutiveOutOfRangeKickTicks = 0;
        uint32_t lastFakeFrame = 0;
        int recentFakeCount = 0;
    };
    std::unordered_map<uint32_t, OpponentFakeMem> opponentFakeMemory;

    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::WebSocket> ws;
    std::shared_ptr<rtc::DataChannel> dcs[3];

    std::unique_ptr<AuthSigner> signer;
    std::string roomId;
    std::string roomPassword;
    std::string playerName;
    std::string playerFlag;
    std::string playerAvatar;
    std::string activeAuthKey;
    bool verbose = false;

    std::atomic<ConnectionStatus> status{ConnectionStatus::Disconnected};
    std::string statusMessage;
    uint32_t myPlayerId = 0;
    uint32_t actionSequence = 1;
    std::atomic<uint32_t> lastSentKeyMask{0};
    uint32_t predictedPulseReleaseFrame = 0;
    bool lastSentChatIndicator = false;

    uint32_t currentFrame = 0;
    uint32_t totalActionsProcessed = 0;
    uint32_t actionsAtCurrentFrame = 0;
    double dD = 10.0;
    double extrapolationMs = 0.0;
    bool antiFakeShot = true;
    bool antiOpMode = false;
    int handicapMs = 0;
    int handicapFrames = 0;

    struct FrameSync {
        uint32_t targetFrame = 0;
        uint32_t expectedActions = 0;
    };
    std::vector<FrameSync> syncQueue;
    std::vector<GameAction> actionQueue;
    std::vector<GameAction> localPredictions;

    WeightedQuantileFilter clockOffsetFilter{50};
    WeightedQuantileFilter pingFilter{50};
    int pingMs = 0;
    int maxPingMs = 0;
    std::optional<uint32_t> fakePingMs;
    FakePingMode fakePingMode = FakePingMode::Off;
    std::vector<uint32_t> fakePingList = {9999, 666, 420, 69};
    uint32_t fakePingMin = 10;
    uint32_t fakePingMax = 999;
    int fakePingStep = 0;
    int fakePingIntervalMs = 500;
    int initialPingsSent = 0;
    std::chrono::steady_clock::time_point clientStartTime;
    std::chrono::steady_clock::time_point lastPingTime;

    std::shared_ptr<World> baseWorld;
    std::shared_ptr<World> realExtrapWorld;
    std::shared_ptr<World> renderWorld;
    mutable std::recursive_mutex clientMutex;

    std::vector<nlohmann::json> localCandidates;
    std::vector<std::string> remoteIpv4Candidates;
    std::string offerSdp;
    int channelsOpen = 0;
};

}
