#include "stadium.hpp"
#include "../net/buffer.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <limits>

namespace haxball {

void Segment::setCurveDeg(double deg) {
    double rad = deg * 0.017453292519943295;
    if (rad < 0.0) {
        rad = -rad;
        std::swap(v0, v1);
        bias = -bias;
    }
    if (rad > 0.17435839227423353 && rad < 5.934119456780721) {
        curveF = 1.0 / std::tan(rad * 0.5);
    } else {
        curveF = std::numeric_limits<double>::infinity();
    }
}

void Segment::update(const std::vector<Vertex>& vertices) {
    if (v0 < 0 || v0 >= (int)vertices.size() || v1 < 0 || v1 >= (int)vertices.size()) return;
    const auto& p = vertices[v0].pos;
    const auto& O = vertices[v1].pos;

    if (std::isfinite(curveF)) {
        isArc = true;
        double F = 0.5 * (O.x - p.x);
        double I = 0.5 * (O.y - p.y);
        double B = p.x + F - I * curveF;
        double s = p.y + I + F * curveF;

        double dx = p.x - B;
        double dy = p.y - s;
        arcRadius = std::sqrt(dx * dx + dy * dy);

        v0Normal = Vec2(s - p.y, p.x - B);
        v1Normal = Vec2(O.y - s, B - O.x);
        if (curveF <= 0.0) {
            v0Normal = -v0Normal;
            v1Normal = -v1Normal;
        }
        center = Vec2(B, s);
    } else {
        isArc = false;
        double F = p.x - O.x;
        double I = p.y - O.y;
        double s = std::sqrt(I * I + F * F);
        if (s > 1e-9) {
            normal = Vec2(-I / s, F / s);
        } else {
            normal = Vec2(0.0, 1.0);
        }
    }
}

bool Goal::check(const Vec2& cur, const Vec2& prev) const {
    double K = prev.x - cur.x;
    double R = prev.y - cur.y;
    if ((R * (p0.x - cur.x) - K * (p0.y - cur.y) > 0.0) ==
        (R * (p1.x - cur.x) - K * (p1.y - cur.y) > 0.0)) {
        return false;
    }
    K = p1.x - p0.x;
    R = p1.y - p0.y;
    return (R * (cur.x - p0.x) - K * (cur.y - p0.y) > 0.0) !=
           (R * (prev.x - p0.x) - K * (prev.y - p0.y) > 0.0);
}

void Stadium::recompute() {
    for (auto& seg : segments) {
        seg.update(vertices);
    }
}

Disc Stadium::makeDefaultBall() {
    Disc ball;
    ball.pos = Vec2(0.0, 0.0);
    ball.speed = Vec2(0.0, 0.0);
    ball.gravity = Vec2(0.0, 0.0);
    ball.radius = 10.0;
    ball.damping = 0.99;
    ball.invMass = 1.0;
    ball.bCoef = 0.5;
    ball.color = 0xFFFFFF;
    ball.cMask = 63;
    ball.cGroup = 193;
    return ball;
}

void Stadium::addGoalPostAndNet(double x, double dir, double goalY, uint32_t postColor, int team, int32_t cMask, int32_t cGroup) {
    int base = (int)vertices.size();
    vertices.push_back({Vec2(x + 8.0 * dir, -goalY), 0.1, cMask, cGroup, base});
    vertices.push_back({Vec2(x + 8.0 * dir, goalY), 0.1, cMask, cGroup, base + 1});
    vertices.push_back({Vec2(x + 30.0 * dir, 22.0 - goalY), 0.1, cMask, cGroup, base + 2});
    vertices.push_back({Vec2(x + 30.0 * dir, goalY - 22.0), 0.1, cMask, cGroup, base + 3});

    Segment s1;
    s1.v0 = base;
    s1.v1 = base + 2;
    s1.bCoef = 0.1;
    s1.cMask = cMask;
    s1.cGroup = cGroup;
    s1.visible = true;
    s1.color = 0;
    s1.setCurveDeg(90.0 * dir);

    Segment s2;
    s2.v0 = base + 3;
    s2.v1 = base + 2;
    s2.bCoef = 0.1;
    s2.cMask = cMask;
    s2.cGroup = cGroup;
    s2.visible = true;
    s2.color = 0;

    Segment s3;
    s3.v0 = base + 3;
    s3.v1 = base + 1;
    s3.bCoef = 0.1;
    s3.cMask = cMask;
    s3.cGroup = cGroup;
    s3.visible = true;
    s3.color = 0;
    s3.setCurveDeg(90.0 * dir);

    segments.push_back(s1);
    segments.push_back(s2);
    segments.push_back(s3);

    Disc pTop;
    pTop.pos = Vec2(x, -goalY);
    pTop.radius = 8.0;
    pTop.invMass = 0.0;
    pTop.bCoef = 0.5;
    pTop.color = postColor;
    pTop.cMask = 63;
    pTop.cGroup = 63;

    Disc pBot = pTop;
    pBot.pos = Vec2(x, goalY);

    discs.push_back(pTop);
    discs.push_back(pBot);

    Goal g;
    g.p0 = Vec2(x, -goalY);
    g.p1 = Vec2(x, goalY);
    g.team = team;
    goals.push_back(g);
}

void Stadium::addKickOffBarrier(double koR, double h) {
    int base = (int)vertices.size();
    vertices.push_back({Vec2(0.0, -h), 0.1, 6, 24, base});
    vertices.push_back({Vec2(0.0, -koR), 0.1, 6, 24, base + 1});
    vertices.push_back({Vec2(0.0, koR), 0.1, 6, 24, base + 2});
    vertices.push_back({Vec2(0.0, h), 0.1, 6, 24, base + 3});

    Segment s1;
    s1.v0 = base; s1.v1 = base + 1;
    s1.bCoef = 0.1; s1.cMask = 6; s1.cGroup = 24; s1.visible = false;

    Segment s2;
    s2.v0 = base + 2; s2.v1 = base + 3;
    s2.bCoef = 0.1; s2.cMask = 6; s2.cGroup = 24; s2.visible = false;

    Segment s3;
    s3.v0 = base + 1; s3.v1 = base + 2;
    s3.bCoef = 0.1; s3.cMask = 6; s3.cGroup = 8; s3.visible = false;
    s3.setCurveDeg(180.0);

    Segment s4;
    s4.v0 = base + 2; s4.v1 = base + 1;
    s4.bCoef = 0.1; s4.cMask = 6; s4.cGroup = 16; s4.visible = false;
    s4.setCurveDeg(180.0);

    segments.push_back(s1);
    segments.push_back(s2);
    segments.push_back(s3);
    segments.push_back(s4);
}

void Stadium::addRoundedCorners(double bW, double bH, double cornerR) {
    if (cornerR <= 0.0) return;
    int base = (int)vertices.size();
    vertices.push_back({Vec2(-bW + cornerR, -bH), 1.0, 0, 32, base});
    vertices.push_back({Vec2(-bW, -bH + cornerR), 1.0, 0, 32, base + 1});
    vertices.push_back({Vec2(-bW + cornerR, bH), 1.0, 0, 32, base + 2});
    vertices.push_back({Vec2(-bW, bH - cornerR), 1.0, 0, 32, base + 3});
    vertices.push_back({Vec2(bW - cornerR, bH), 1.0, 0, 32, base + 4});
    vertices.push_back({Vec2(bW, bH - cornerR), 1.0, 0, 32, base + 5});
    vertices.push_back({Vec2(bW - cornerR, -bH), 1.0, 0, 32, base + 6});
    vertices.push_back({Vec2(bW, -bH + cornerR), 1.0, 0, 32, base + 7});

    auto addCornerSeg = [&](int a, int b, double deg) {
        Segment s;
        s.v0 = a; s.v1 = b;
        s.bCoef = 1.0; s.cMask = 1; s.cGroup = 32; s.visible = false;
        s.setCurveDeg(deg);
        segments.push_back(s);
    };
    addCornerSeg(base, base + 1, -90.0);
    addCornerSeg(base + 2, base + 3, 90.0);
    addCornerSeg(base + 4, base + 5, -90.0);
    addCornerSeg(base + 6, base + 7, 90.0);
}

void Stadium::initStandard(const std::string& stName, double w, double h, double bW, double bH, double goalY, double koR, double cornerR) {
    name = stName;
    discs.push_back(makeDefaultBall());
    width = w;
    height = h;
    bgType = 1;
    bgColor = 0x718C5A;
    bgWidth = bW;
    bgHeight = bH;
    bgKickOffRadius = koR;
    bgCornerRadius = cornerR;
    goalWidth = goalY;
    spawnDistance = std::min(400.0, 0.75 * bW);

    planes.push_back({Vec2(0.0, 1.0), -h, 0.0, 63, 32});
    planes.push_back({Vec2(0.0, -1.0), -h, 0.0, 63, 32});
    planes.push_back({Vec2(1.0, 0.0), -w, 0.0, 63, 32});
    planes.push_back({Vec2(-1.0, 0.0), -w, 0.0, 63, 32});

    addGoalPostAndNet(bW, 1.0, goalY, 13421823, 2);
    addGoalPostAndNet(-bW, -1.0, goalY, 16764108, 1);
    addKickOffBarrier(koR, h);

    planes.push_back({Vec2(0.0, 1.0), -bH, 1.0, 1, 32});
    planes.push_back({Vec2(0.0, -1.0), -bH, 1.0, 1, 32});

    int base = (int)vertices.size();
    vertices.push_back({Vec2(-bW, -bH), 1.0, 0, 32, base});
    vertices.push_back({Vec2(bW, -bH), 1.0, 0, 32, base + 1});
    vertices.push_back({Vec2(bW, -goalY), 1.0, 0, 32, base + 2});
    vertices.push_back({Vec2(bW, goalY), 1.0, 0, 32, base + 3});
    vertices.push_back({Vec2(bW, bH), 1.0, 0, 32, base + 4});
    vertices.push_back({Vec2(-bW, bH), 1.0, 0, 32, base + 5});
    vertices.push_back({Vec2(-bW, goalY), 1.0, 0, 32, base + 6});
    vertices.push_back({Vec2(-bW, -goalY), 1.0, 0, 32, base + 7});

    auto addWallSeg = [&](int a, int b) {
        Segment s;
        s.v0 = a; s.v1 = b;
        s.bCoef = 1.0; s.cMask = 1; s.cGroup = 32; s.visible = false;
        segments.push_back(s);
    };
    addWallSeg(base + 1, base + 2);
    addWallSeg(base + 3, base + 4);
    addWallSeg(base + 5, base + 6);
    addWallSeg(base + 7, base);

    addRoundedCorners(bW, bH, cornerR);
    recompute();
}

void Stadium::initHockey(const std::string& stName, double w, double h, double bW, double bH, double goalY, double goalLine, double koR, double cornerR) {
    name = stName;
    discs.push_back(makeDefaultBall());
    width = w;
    height = h;
    bgType = 2;
    bgColor = 0x718C5A;
    bgWidth = bW;
    bgHeight = bH;
    bgKickOffRadius = koR;
    bgCornerRadius = cornerR;
    bgGoalLine = goalLine;
    goalWidth = goalY;
    spawnDistance = std::min(400.0, 0.75 * (bW - goalLine));

    planes.push_back({Vec2(0.0, 1.0), -h, 0.0, 63, 32});
    planes.push_back({Vec2(0.0, -1.0), -h, 0.0, 63, 32});
    planes.push_back({Vec2(1.0, 0.0), -w, 0.0, 63, 32});
    planes.push_back({Vec2(-1.0, 0.0), -w, 0.0, 63, 32});

    addGoalPostAndNet(bW - goalLine, 1.0, goalY, 13421823, 2, 63, 32);
    addGoalPostAndNet(-bW + goalLine, -1.0, goalY, 16764108, 1, 63, 32);
    addKickOffBarrier(koR, h);

    planes.push_back({Vec2(0.0, 1.0), -bH, 1.0, 1, 32});
    planes.push_back({Vec2(0.0, -1.0), -bH, 1.0, 1, 32});
    planes.push_back({Vec2(1.0, 0.0), -bW, 1.0, 1, 32});
    planes.push_back({Vec2(-1.0, 0.0), -bW, 1.0, 1, 32});

    addRoundedCorners(bW, bH, cornerR);
    recompute();
}

std::shared_ptr<Stadium> Stadium::createDefault(uint8_t id) {
    auto st = std::make_shared<Stadium>();
    st->defaultStadiumId = id;
    switch (id) {
        case 0: st->initStandard("Classic", 420, 200, 370, 170, 64, 75); break;
        case 1: st->initStandard("Easy", 420, 200, 370, 170, 90, 75); break;
        case 2: st->initStandard("Small", 420, 200, 320, 130, 55, 70); break;
        case 3: st->initStandard("Big", 600, 270, 550, 240, 80, 80); break;
        case 4: st->initStandard("Rounded", 420, 200, 370, 170, 64, 75, 75); break;
        case 5: st->initHockey("Hockey", 420, 204, 398, 182, 68, 120, 75, 100); break;
        case 6: st->initHockey("Big Hockey", 600, 270, 550, 240, 90, 160, 75, 150); break;
        case 7: st->initStandard("Big Easy", 600, 270, 550, 240, 95, 80); break;
        case 8: st->initStandard("Big Rounded", 600, 270, 550, 240, 80, 75, 100); break;
        case 9: st->initStandard("Huge", 750, 350, 700, 320, 100, 80); break;
        default: st->initStandard("Classic", 420, 200, 370, 170, 64, 75); break;
    }
    return st;
}

std::shared_ptr<Stadium> Stadium::createClassic() { return createDefault(0); }
std::shared_ptr<Stadium> Stadium::createSmall()   { return createDefault(2); }
std::shared_ptr<Stadium> Stadium::createBig()     { return createDefault(3); }
std::shared_ptr<Stadium> Stadium::createRounded() { return createDefault(4); }

std::shared_ptr<Stadium> Stadium::deserialize(::BufferReader& sr) {
    uint8_t idx = sr.readU8();
    if (idx != 255) {
        return createDefault(idx);
    }

    auto st = std::make_shared<Stadium>();
    st->defaultStadiumId = 255;
    auto optName = sr.readOptStr();
    st->name = optName.value_or("Custom");
    st->bgType = sr.readI32BE();
    st->bgWidth = sr.readF64BE();
    st->bgHeight = sr.readF64BE();
    st->bgKickOffRadius = sr.readF64BE();
    st->bgCornerRadius = sr.readF64BE();
    st->bgGoalLine = sr.readF64BE();
    st->bgColor = (uint32_t)sr.readI32BE();
    st->width = sr.readF64BE();
    st->height = sr.readF64BE();
    st->spawnDistance = sr.readF64BE();

    st->playerPhysics.deserialize(sr);

    st->maxViewWidth = sr.readU16BE();
    st->cameraFollow = sr.readU8();
    st->canBeStored = sr.readU8() != 0;
    st->kickOffResetFull = sr.readU8() != 0;

    uint8_t vCount = sr.readU8();
    st->vertices.reserve(vCount);
    for (uint8_t i = 0; i < vCount; ++i) {
        Vertex v;
        v.id = i;
        v.pos.x = sr.readF64BE();
        v.pos.y = sr.readF64BE();
        v.bCoef = sr.readF64BE();
        v.cMask = sr.readI32BE();
        v.cGroup = sr.readI32BE();
        st->vertices.push_back(v);
    }

    uint8_t sCount = sr.readU8();
    st->segments.reserve(sCount);
    for (uint8_t i = 0; i < sCount; ++i) {
        Segment s;
        uint8_t flags = sr.readU8();
        s.v0 = sr.readU8();
        s.v1 = sr.readU8();
        s.bias = (flags & 1) ? sr.readF64BE() : 0.0;
        s.curveF = (flags & 2) ? sr.readF64BE() : std::numeric_limits<double>::infinity();
        s.color = (flags & 4) ? sr.readI32BE() : 0;
        s.visible = (flags & 8) != 0;
        s.bCoef = sr.readF64BE();
        s.cMask = sr.readI32BE();
        s.cGroup = sr.readI32BE();
        st->segments.push_back(s);
    }

    uint8_t pCount = sr.readU8();
    st->planes.reserve(pCount);
    for (uint8_t i = 0; i < pCount; ++i) {
        Plane p;
        p.normal.x = sr.readF64BE();
        p.normal.y = sr.readF64BE();
        p.dist = sr.readF64BE();
        p.bCoef = sr.readF64BE();
        p.cMask = sr.readI32BE();
        p.cGroup = sr.readI32BE();
        st->planes.push_back(p);
    }

    uint8_t gCount = sr.readU8();
    st->goals.reserve(gCount);
    for (uint8_t i = 0; i < gCount; ++i) {
        Goal g;
        g.p0.x = sr.readF64BE();
        g.p0.y = sr.readF64BE();
        g.p1.x = sr.readF64BE();
        g.p1.y = sr.readF64BE();
        g.team = sr.readI8();
        st->goals.push_back(g);
    }

    uint8_t dCount = sr.readU8();
    st->discs.reserve(dCount);
    for (uint8_t i = 0; i < dCount; ++i) {
        Disc d;
        d.deserialize(sr);
        st->discs.push_back(d);
    }

    uint8_t jCount = sr.readU8();
    st->joints.reserve(jCount);
    for (uint8_t i = 0; i < jCount; ++i) {
        Joint j;
        j.d0 = sr.readU8();
        j.d1 = sr.readU8();
        j.minLength = sr.readF64BE();
        j.maxLength = sr.readF64BE();
        j.strength = sr.readF64BE();
        j.color = sr.readI32BE();
        st->joints.push_back(j);
    }

    uint8_t rCount = sr.readU8();
    st->redSpawnPoints.reserve(rCount);
    for (uint8_t i = 0; i < rCount; ++i) {
        double x = sr.readF64BE();
        double y = sr.readF64BE();
        st->redSpawnPoints.emplace_back(x, y);
    }

    uint8_t bCount = sr.readU8();
    st->blueSpawnPoints.reserve(bCount);
    for (uint8_t i = 0; i < bCount; ++i) {
        double x = sr.readF64BE();
        double y = sr.readF64BE();
        st->blueSpawnPoints.emplace_back(x, y);
    }

    st->recompute();
    return st;
}

static int32_t parseHbsCollisionFlags(const nlohmann::json& arr, int32_t defaultVal) {
    if (!arr.is_array()) return defaultVal;
    int32_t mask = 0;
    for (const auto& item : arr) {
        if (!item.is_string()) continue;
        const std::string s = item.get<std::string>();
        if (s == "ball") mask |= 1;
        else if (s == "red") mask |= 2;
        else if (s == "blue") mask |= 4;
        else if (s == "redKO") mask |= 8;
        else if (s == "blueKO") mask |= 16;
        else if (s == "wall") mask |= 32;
        else if (s == "kick") mask |= 64;
        else if (s == "score") mask |= 128;
        else if (s == "c0") mask |= (1 << 28);
        else if (s == "c1") mask |= (1 << 29);
        else if (s == "c2") mask |= (1 << 30);
        else if (s == "c3") mask |= static_cast<int32_t>(1u << 31);
        else if (s == "all") mask |= 63;
    }
    return mask;
}

static int32_t parseHbsColor(const nlohmann::json& val, int32_t defaultVal) {
    if (val.is_string()) {
        std::string s = val.get<std::string>();
        if (s == "transparent") return -1;
        if (!s.empty() && s[0] == '#') s = s.substr(1);
        try {
            return static_cast<int32_t>(std::stoul(s, nullptr, 16));
        } catch (...) {
            return defaultVal;
        }
    } else if (val.is_array() && val.size() >= 3) {
        int r = std::clamp(val[0].get<int>(), 0, 255);
        int g = std::clamp(val[1].get<int>(), 0, 255);
        int b = std::clamp(val[2].get<int>(), 0, 255);
        return (r << 16) | (g << 8) | b;
    } else if (val.is_number_integer()) {
        return val.get<int32_t>();
    }
    return defaultVal;
}

std::shared_ptr<Stadium> Stadium::loadFromHbsFile(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) return nullptr;

    nlohmann::json j;
    try {
        ifs >> j;
    } catch (...) {
        return nullptr;
    }

    auto st = std::make_shared<Stadium>();
    st->defaultStadiumId = 255;
    st->name = j.value("name", "Custom Stadium");
    st->width = j.value("width", 420.0);
    st->height = j.value("height", 200.0);
    st->spawnDistance = j.value("spawnDistance", 170.0);
    st->maxViewWidth = static_cast<uint16_t>(j.value("maxViewWidth", 0));
    st->canBeStored = j.value("canBeStored", true);

    if (j.contains("bg") && j["bg"].is_object()) {
        const auto& bg = j["bg"];
        std::string bgT = bg.value("type", "none");
        if (bgT == "grass") st->bgType = 1;
        else if (bgT == "hockey") st->bgType = 2;
        else st->bgType = 0;
        st->bgWidth = bg.value("width", 0.0);
        st->bgHeight = bg.value("height", 0.0);
        st->bgKickOffRadius = bg.value("kickOffRadius", 0.0);
        st->bgCornerRadius = bg.value("cornerRadius", 0.0);
        st->bgGoalLine = bg.value("goalLine", 0.0);
        if (bg.contains("color")) {
            int32_t col = parseHbsColor(bg["color"], 0x718C5A);
            if (col >= 0) st->bgColor = static_cast<uint32_t>(col);
        }
    }

    if (j.contains("playerPhysics") && j["playerPhysics"].is_object()) {
        const auto& pp = j["playerPhysics"];
        st->playerPhysics.bCoef = pp.value("bCoef", 0.5);
        st->playerPhysics.invMass = pp.value("invMass", 0.5);
        st->playerPhysics.damping = pp.value("damping", 0.96);
        st->playerPhysics.acceleration = pp.value("acceleration", 0.1);
        st->playerPhysics.kickingAcceleration = pp.value("kickingAcceleration", 0.07);
        st->playerPhysics.kickingDamping = pp.value("kickingDamping", 0.96);
        st->playerPhysics.kickStrength = pp.value("kickStrength", 5.0);
        st->playerPhysics.kickback = pp.value("kickback", 0.0);
        st->playerPhysics.radius = pp.value("radius", 15.0);
        if (pp.contains("gravity") && pp["gravity"].is_array() && pp["gravity"].size() >= 2) {
            st->playerPhysics.gravity = Vec2(pp["gravity"][0].get<double>(), pp["gravity"][1].get<double>());
        }
        if (pp.contains("cGroup")) {
            st->playerPhysics.cGroup = parseHbsCollisionFlags(pp["cGroup"], 0);
        }
    }

    if (j.contains("vertexes") && j["vertexes"].is_array()) {
        int idx = 0;
        for (const auto& vj : j["vertexes"]) {
            Vertex v;
            v.id = idx++;
            v.pos.x = vj.value("x", 0.0);
            v.pos.y = vj.value("y", 0.0);
            v.bCoef = vj.value("bCoef", 1.0);
            v.cMask = vj.contains("cMask") ? parseHbsCollisionFlags(vj["cMask"], 63) : 63;
            v.cGroup = vj.contains("cGroup") ? parseHbsCollisionFlags(vj["cGroup"], 32) : 32;
            st->vertices.push_back(v);
        }
    }

    if (j.contains("segments") && j["segments"].is_array()) {
        for (const auto& sj : j["segments"]) {
            Segment s;
            s.v0 = sj.value("v0", 0);
            s.v1 = sj.value("v1", 0);
            s.bCoef = sj.value("bCoef", 1.0);
            s.bias = sj.value("bias", 0.0);
            s.visible = sj.value("vis", true);
            s.cMask = sj.contains("cMask") ? parseHbsCollisionFlags(sj["cMask"], 63) : 63;
            s.cGroup = sj.contains("cGroup") ? parseHbsCollisionFlags(sj["cGroup"], 32) : 32;
            if (sj.contains("color")) {
                s.color = parseHbsColor(sj["color"], 0);
            }
            if (sj.contains("curve") && sj["curve"].is_number()) {
                s.setCurveDeg(sj["curve"].get<double>());
            } else if (sj.contains("curveF") && sj["curveF"].is_number()) {
                s.curveF = sj["curveF"].get<double>();
            }
            st->segments.push_back(s);
        }
    }

    if (j.contains("planes") && j["planes"].is_array()) {
        for (const auto& pj : j["planes"]) {
            Plane p;
            if (pj.contains("normal") && pj["normal"].is_array() && pj["normal"].size() >= 2) {
                double nx = pj["normal"][0].get<double>();
                double ny = pj["normal"][1].get<double>();
                double len = std::hypot(nx, ny);
                if (len > 1e-9) { nx /= len; ny /= len; }
                p.normal = Vec2(nx, ny);
            }
            p.dist = pj.value("dist", 0.0);
            p.bCoef = pj.value("bCoef", 1.0);
            p.cMask = pj.contains("cMask") ? parseHbsCollisionFlags(pj["cMask"], 63) : 63;
            p.cGroup = pj.contains("cGroup") ? parseHbsCollisionFlags(pj["cGroup"], 32) : 32;
            st->planes.push_back(p);
        }
    }

    if (j.contains("goals") && j["goals"].is_array()) {
        for (const auto& gj : j["goals"]) {
            Goal g;
            if (gj.contains("p0") && gj["p0"].is_array() && gj["p0"].size() >= 2) {
                g.p0 = Vec2(gj["p0"][0].get<double>(), gj["p0"][1].get<double>());
            }
            if (gj.contains("p1") && gj["p1"].is_array() && gj["p1"].size() >= 2) {
                g.p1 = Vec2(gj["p1"][0].get<double>(), gj["p1"][1].get<double>());
            }
            std::string tStr = gj.value("team", "red");
            g.team = (tStr == "blue") ? 2 : 1;
            st->goals.push_back(g);
        }
        if (!st->goals.empty()) {
            st->goalWidth = std::abs(st->goals[0].p0.y - st->goals[0].p1.y) * 0.5;
        }
    }

    bool ballIsDisc0 = (j.contains("ballPhysics") && j["ballPhysics"].is_string() && j["ballPhysics"].get<std::string>() == "disc0");
    if (!ballIsDisc0) {
        Disc ball = makeDefaultBall();
        if (j.contains("ballPhysics") && j["ballPhysics"].is_object()) {
            const auto& bp = j["ballPhysics"];
            ball.radius = bp.value("radius", ball.radius);
            ball.bCoef = bp.value("bCoef", ball.bCoef);
            ball.invMass = bp.value("invMass", ball.invMass);
            ball.damping = bp.value("damping", ball.damping);
            if (bp.contains("color")) ball.color = static_cast<uint32_t>(parseHbsColor(bp["color"], 0xFFFFFF));
            if (bp.contains("cMask")) ball.cMask = parseHbsCollisionFlags(bp["cMask"], ball.cMask);
            if (bp.contains("cGroup")) ball.cGroup = parseHbsCollisionFlags(bp["cGroup"], ball.cGroup);
        }
        st->discs.push_back(ball);
    }

    if (j.contains("discs") && j["discs"].is_array()) {
        for (size_t di = 0; di < j["discs"].size(); ++di) {
            const auto& dj = j["discs"][di];
            bool isBall = (ballIsDisc0 && di == 0);
            Disc d = isBall ? makeDefaultBall() : Disc{};
            if (!isBall) {
                d.radius = 10.0;
                d.bCoef = 0.5;
                d.invMass = 0.0;
                d.damping = 0.99;
                d.color = 0xFFFFFF;
                d.cMask = 63;
                d.cGroup = 32;
            }
            if (dj.contains("pos") && dj["pos"].is_array() && dj["pos"].size() >= 2) {
                d.pos = Vec2(dj["pos"][0].get<double>(), dj["pos"][1].get<double>());
            }
            if (dj.contains("speed") && dj["speed"].is_array() && dj["speed"].size() >= 2) {
                d.speed = Vec2(dj["speed"][0].get<double>(), dj["speed"][1].get<double>());
            }
            if (dj.contains("gravity") && dj["gravity"].is_array() && dj["gravity"].size() >= 2) {
                d.gravity = Vec2(dj["gravity"][0].get<double>(), dj["gravity"][1].get<double>());
            }
            d.radius = dj.value("radius", d.radius);
            d.bCoef = dj.value("bCoef", d.bCoef);
            d.invMass = dj.value("invMass", d.invMass);
            d.damping = dj.value("damping", d.damping);
            if (dj.contains("color")) {
                d.color = static_cast<uint32_t>(parseHbsColor(dj["color"], static_cast<int32_t>(d.color)));
            }
            if (dj.contains("cMask")) d.cMask = parseHbsCollisionFlags(dj["cMask"], d.cMask);
            if (dj.contains("cGroup")) d.cGroup = parseHbsCollisionFlags(dj["cGroup"], d.cGroup);
            st->discs.push_back(d);
        }
    }

    if (j.contains("joints") && j["joints"].is_array()) {
        for (const auto& jj : j["joints"]) {
            Joint jt;
            int offset = ballIsDisc0 ? 0 : 1;
            jt.d0 = static_cast<uint8_t>(jj.value("d0", 0) + offset);
            jt.d1 = static_cast<uint8_t>(jj.value("d1", 0) + offset);
            if (jj.contains("length")) {
                if (jj["length"].is_number()) {
                    jt.minLength = jt.maxLength = jj["length"].get<double>();
                } else if (jj["length"].is_array() && jj["length"].size() >= 2) {
                    jt.minLength = jj["length"][0].get<double>();
                    jt.maxLength = jj["length"][1].get<double>();
                }
            }
            if (jj.contains("strength")) {
                if (jj["strength"].is_number()) jt.strength = jj["strength"].get<double>();
                else jt.strength = std::numeric_limits<double>::infinity();
            }
            if (jj.contains("color")) {
                jt.color = parseHbsColor(jj["color"], 0);
            }
            st->joints.push_back(jt);
        }
    }

    st->recompute();
    return st;
}

}
