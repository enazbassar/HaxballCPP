#pragma once

#include <cstdint>

#include "stadium.hpp"
#include "disc.hpp"
#include "../net/buffer.hpp"
#include <vector>
#include <string>
#include <optional>
#include <memory>
#include <functional>
#include <unordered_map>

namespace haxball {

struct BallKickEvent {
    uint32_t tick = 0;
    uint32_t playerId = 0;
    int team = 0;
    Vec2 preBallVel{0.0, 0.0};
    Vec2 postBallVel{0.0, 0.0};
    Vec2 kickNormal{1.0, 0.0};
    double baseDeltaV = 5.0;
    double ballDamping = 0.99;
};

struct TeamColors {
    uint8_t angle = 0;
    int32_t textColor = 0xFFFFFF;
    std::vector<int32_t> colors;

    void deserialize(::BufferReader& sr) {
        angle = sr.readU8();
        textColor = sr.readI32BE();
        uint8_t count = sr.readU8();
        colors.clear();
        for (uint8_t i = 0; i < count; ++i) {
            int32_t c = sr.readI32BE();
            if (i < 4) colors.push_back(c);
        }
    }
};

struct Player {
    uint32_t id = 0;
    std::string name = "Player";
    std::string flag = "nl";
    std::string avatar = "";
    std::optional<std::string> headlessAvatar;
    int team = 0;
    int32_t teamOrder = 0;
    bool admin = false;
    bool desynced = false;
    int32_t ping = 0;
    uint32_t input = 0;
    bool isKicking = false;
    uint8_t kickCooldown = 0;
    int16_t kickRateCounter = 0;
    bool chatIndicator = false;
    int discIndex = -1;
    bool allowSpeculativeKick = true;
    bool fakeShotDetected = false;

    std::string getDisplayAvatar() const {
        if (headlessAvatar.has_value()) return *headlessAvatar;
        return avatar;
    }
};

class World {
public:
    std::string roomName = "Haxball Room";
    std::shared_ptr<Stadium> stadium;
    bool hasMatch = true;
    std::vector<Disc> discs;
    std::vector<Player> players;

    uint32_t currentTick = 0;
    int32_t redScore = 0;
    int32_t blueScore = 0;
    int32_t scoreLimit = 3;
    int32_t timeLimit = 3;
    double timeElapsed = 0.0;
    int32_t matchState = 0;
    int32_t goalTickCounter = 0;
    int32_t pauseTickCounter = 0;
    int goalConcedingTeam = 1;
    bool teamsLocked = false;

    int16_t kickBurst = 1;
    uint8_t kickRate = 0;
    uint8_t kickMin = 2;

    TeamColors teamColors[3];

    std::function<void()> onKick;
    std::function<void(int scoringTeam)> onGoal;
    std::function<void(const BallKickEvent&)> onPlayerBallKick;

    World();
    explicit World(std::shared_ptr<Stadium> st);

    World cloneState() const;
    void deserialize(::BufferReader& sr);

    void startGame();
    void stopGame();
    void pauseResumeGame(bool pause);
    void resetKickOffPositions();
    void resetPositions() { resetKickOffPositions(); }
    void resetMatch() { stopGame(); startGame(); }

    void step(double dt = 1.0);

    Player* getPlayer(uint32_t id);
    const Player* getPlayer(uint32_t id) const;
    Player* addPlayer(uint32_t id, const std::string& name, const std::string& flag, const std::string& avatar);
    void removePlayer(uint32_t id);
    void setPlayerTeam(uint32_t id, int team);
    void setPlayerInput(uint32_t id, uint32_t inputMask);
    void autoTeams();
    void reorderPlayers(const std::vector<int32_t>& idList, bool moveToTop);
    void setKickRateLimit(int32_t min, int32_t rate, int32_t burst);
    void setDiscProperties(int32_t id, bool isPlayer, const std::optional<float> fProps[10], const std::optional<int32_t> iProps[3]);

private:
    void stepPhysics(double dt);
    void removePlayerDisc(Player& p);
    void spawnOrResetPlayerDisc(Player& p);
    void updatePlayerDisc(Player& p);
};

}
