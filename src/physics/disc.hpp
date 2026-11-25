#pragma once

#include "../math/vec2.hpp"
#include "../net/buffer.hpp"
#include <cstdint>

namespace haxball {

enum CollisionMask : int32_t {
    COLL_BALL    = 1 << 0,
    COLL_RED     = 1 << 1,
    COLL_BLUE    = 1 << 2,
    COLL_RED_KO  = 1 << 3,
    COLL_BLUE_KO = 1 << 4,
    COLL_WALL    = 1 << 5,
    COLL_KICK    = 1 << 6,
    COLL_SCORE   = 1 << 7,
    COLL_ALL     = 63
};

struct Disc {
    Vec2 pos{0.0, 0.0};
    Vec2 speed{0.0, 0.0};
    Vec2 gravity{0.0, 0.0};
    double radius = 10.0;
    double bCoef = 0.5;
    double invMass = 1.0;
    double damping = 0.99;
    uint32_t color = 0xFFFFFF;
    int32_t cMask = 63;
    int32_t cGroup = 193;
    int32_t playerId = -1;

    bool isImmovable() const { return invMass == 0.0; }

    void applyImpulse(double mag, double dx, double dy) {
        speed.x += mag * dx * invMass;
        speed.y += mag * dy * invMass;
    }

    void deserialize(::BufferReader& sr) {
        pos.x = sr.readF64BE();
        pos.y = sr.readF64BE();
        speed.x = sr.readF64BE();
        speed.y = sr.readF64BE();
        gravity.x = sr.readF64BE();
        gravity.y = sr.readF64BE();
        radius = sr.readF64BE();
        bCoef = sr.readF64BE();
        invMass = sr.readF64BE();
        damping = sr.readF64BE();
        color = sr.readU32BE();
        cMask = sr.readI32BE();
        cGroup = sr.readI32BE();
    }
};

}
