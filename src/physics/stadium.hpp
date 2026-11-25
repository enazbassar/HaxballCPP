#pragma once

#include <cstdint>

#include "../math/vec2.hpp"
#include "../net/buffer.hpp"
#include "disc.hpp"
#include <string>
#include <vector>
#include <memory>
#include <cmath>
#include <limits>

namespace haxball {

struct Vertex {
    Vec2 pos{0.0, 0.0};
    double bCoef = 1.0;
    int32_t cMask = 63;
    int32_t cGroup = 32;
    int id = 0;
};

struct Segment {
    int v0 = 0;
    int v1 = 0;
    double bCoef = 1.0;
    int32_t cMask = 63;
    int32_t cGroup = 32;
    double bias = 0.0;
    double curveF = std::numeric_limits<double>::infinity();
    bool visible = true;
    int32_t color = 0;

    bool isArc = false;
    Vec2 center{0.0, 0.0};
    double arcRadius = 0.0;
    Vec2 v0Normal{0.0, 0.0};
    Vec2 v1Normal{0.0, 0.0};
    Vec2 normal{0.0, 0.0};

    void setCurveDeg(double deg);
    void update(const std::vector<Vertex>& vertices);
};

struct Plane {
    Vec2 normal{0.0, 1.0};
    double dist = 0.0;
    double bCoef = 1.0;
    int32_t cMask = 63;
    int32_t cGroup = 32;
};

struct Goal {
    Vec2 p0{0.0, 0.0};
    Vec2 p1{0.0, 0.0};
    int team = 0;

    bool check(const Vec2& curPos, const Vec2& prevPos) const;
};

struct Joint {
    uint8_t d0 = 0;
    uint8_t d1 = 0;
    double minLength = 100.0;
    double maxLength = 100.0;
    double strength = std::numeric_limits<double>::infinity();
    int32_t color = 0;
};

struct PlayerPhysics {
    double bCoef = 0.5;
    double invMass = 0.5;
    double damping = 0.96;
    double acceleration = 0.1;
    double kickingAcceleration = 0.07;
    double kickingDamping = 0.96;
    double kickStrength = 5.0;
    Vec2 gravity{0.0, 0.0};
    int32_t cGroup = 0;
    double radius = 15.0;
    double kickback = 0.0;

    void deserialize(::BufferReader& sr) {
        bCoef = sr.readF64BE();
        invMass = sr.readF64BE();
        damping = sr.readF64BE();
        acceleration = sr.readF64BE();
        kickingAcceleration = sr.readF64BE();
        kickingDamping = sr.readF64BE();
        kickStrength = sr.readF64BE();
        gravity.x = sr.readF64BE();
        gravity.y = sr.readF64BE();
        cGroup = sr.readI32BE();
        radius = sr.readF64BE();
        kickback = sr.readF64BE();
    }
};

class Stadium {
public:
    uint8_t defaultStadiumId = 255;
    std::string name = "Classic";
    int32_t bgType = 1;
    double width = 420.0;
    double height = 200.0;
    double spawnDistance = 170.0;
    double bgCornerRadius = 0.0;
    double bgKickOffRadius = 75.0;
    double bgWidth = 370.0;
    double bgHeight = 170.0;
    double bgGoalLine = 0.0;
    double goalWidth = 64.0;
    uint32_t bgColor = 0x718C5A;
    uint16_t maxViewWidth = 0;
    uint8_t cameraFollow = 0;
    bool canBeStored = true;
    bool kickOffResetFull = false;

    PlayerPhysics playerPhysics;
    std::vector<Vertex> vertices;
    std::vector<Segment> segments;
    std::vector<Plane> planes;
    std::vector<Goal> goals;
    std::vector<Disc> discs;
    std::vector<Joint> joints;
    std::vector<Vec2> redSpawnPoints;
    std::vector<Vec2> blueSpawnPoints;

    static std::shared_ptr<Stadium> createDefault(uint8_t id);
    static std::shared_ptr<Stadium> createClassic();
    static std::shared_ptr<Stadium> createSmall();
    static std::shared_ptr<Stadium> createBig();
    static std::shared_ptr<Stadium> createRounded();
    static std::shared_ptr<Stadium> loadFromHbsFile(const std::string& path);
    static std::shared_ptr<Stadium> deserialize(::BufferReader& sr);

    void recompute();

private:
    static Disc makeDefaultBall();
    void initStandard(const std::string& stName, double w, double h, double bW, double bH, double goalY, double koR, double cornerR = 0.0);
    void initHockey(const std::string& stName, double w, double h, double bW, double bH, double goalY, double goalLine, double koR, double cornerR);
    void addGoalPostAndNet(double x, double dir, double goalY, uint32_t postColor, int team, int32_t cMask = 1, int32_t cGroup = 32);
    void addKickOffBarrier(double koR, double h);
    void addRoundedCorners(double bW, double bH, double cornerR);
};

}
