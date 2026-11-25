#include "tactical_assistant.hpp"
#include <cmath>
#include <algorithm>

namespace haxball {

static inline double fastHypot(double dx, double dy) {
    return std::sqrt(dx * dx + dy * dy);
}

static Vec2 safeNormalize(const Vec2& v, const Vec2& fallback = Vec2(1.0, 0.0)) {
    double len = fastHypot(v.x, v.y);
    if (len < 1e-8) return fallback;
    return Vec2(v.x / len, v.y / len);
}

static Vec2 computeVelocityCompensatedKickNormal(const Vec2& ballVel, double kickImpulse, const Vec2& desiredDir) {
    Vec2 d = safeNormalize(desiredDir);
    double parallel = ballVel.x * d.x + ballVel.y * d.y;
    Vec2 vPerp(ballVel.x - parallel * d.x, ballVel.y - parallel * d.y);
    double perpMag = fastHypot(vPerp.x, vPerp.y);

    if (kickImpulse <= 1e-5 || perpMag >= kickImpulse * 0.88) {
        return d;
    }
    double forwardComp = std::sqrt(std::max(0.0, kickImpulse * kickImpulse - perpMag * perpMag));
    Vec2 comp(forwardComp * d.x - vPerp.x, forwardComp * d.y - vPerp.y);
    return safeNormalize(comp, d);
}

static void resolveBallStaticCollisions(const World& world, const Disc& ball, Vec2& pos, Vec2& vel,
                                        int& bounceCount, double& minPostClearance) {
    const auto& st = *world.stadium;
    const auto& planes = st.planes;
    const auto& segments = st.segments;
    const auto& vertices = st.vertices;
    const int numVertices = static_cast<int>(vertices.size());

    double r = ball.radius > 1.0 ? ball.radius : 10.0;
    double bCoef = ball.bCoef;
    int32_t cMask = ball.cMask;
    int32_t cGroup = ball.cGroup;

    const size_t numDiscs = world.discs.size();
    for (size_t di = 1; di < numDiscs; ++di) {
        const auto& d = world.discs[di];
        if (d.invMass != 0.0) continue;
        if ((d.cMask & cGroup) == 0 || (cMask & d.cGroup) == 0) continue;
        double rx = pos.x - d.pos.x;
        double ry = pos.y - d.pos.y;
        double dist = fastHypot(rx, ry);
        double sumR = r + d.radius;
        double clearance = dist - sumR;
        if (clearance < minPostClearance) minPostClearance = clearance;
        if (dist > 1e-6 && dist < sumR) {
            double nx = rx / dist;
            double ny = ry / dist;
            pos.x += nx * (sumR - dist);
            pos.y += ny * (sumR - dist);
            double vn = vel.x * nx + vel.y * ny;
            if (vn < 0.0) {
                double restitution = bCoef * d.bCoef + 1.0;
                vel.x -= nx * vn * restitution;
                vel.y -= ny * vn * restitution;
                bounceCount++;
            }
        }
    }

    for (const auto& q : planes) {
        if ((q.cMask & cGroup) == 0 || (cMask & q.cGroup) == 0) continue;
        double H = q.normal.x;
        double W = q.normal.y;
        double s = q.dist - (H * pos.x + W * pos.y) + r;
        if (s > 0.0) {
            pos.x += H * s;
            pos.y += W * s;
            double velDot = vel.x * H + vel.y * W;
            if (velDot < 0.0) {
                velDot *= bCoef * q.bCoef + 1.0;
                vel.x -= velDot * H;
                vel.y -= velDot * W;
                bounceCount++;
            }
        }
    }

    for (const auto& q : segments) {
        if ((q.cMask & cGroup) == 0 || (cMask & q.cGroup) == 0) continue;
        double H = 0.0, W = 0.0, T = 0.0;
        double gx = pos.x;
        double by = pos.y;

        if (q.isArc && std::isfinite(q.curveF)) {
            H = gx - q.center.x;
            W = by - q.center.y;
            const Vec2& Tn = q.v0Normal;
            const Vec2& n2 = q.v1Normal;
            if (((Tn.x * H + Tn.y * W > 0.0 && n2.x * H + n2.y * W > 0.0) == (q.curveF <= 0.0))) {
                continue;
            }
            double dist = std::sqrt(H * H + W * W);
            if (dist <= 1e-9) continue;
            T = dist - q.arcRadius;
            H /= dist;
            W /= dist;
        } else {
            if (q.v0 < 0 || q.v0 >= numVertices ||
                q.v1 < 0 || q.v1 >= numVertices) continue;
            const Vec2& v0 = vertices[q.v0].pos;
            const Vec2& v1 = vertices[q.v1].pos;
            W = v1.x - v0.x;
            H = v1.y - v0.y;
            T = gx - v1.x;
            double dy1 = by - v1.y;
            if ((gx - v0.x) * W + (by - v0.y) * H <= 0.0 || T * W + dy1 * H >= 0.0) {
                continue;
            }
            H = q.normal.x;
            W = q.normal.y;
            T = H * T + W * dy1;
        }

        double bias = q.bias;
        if (bias == 0.0) {
            if (T < 0.0) { T = -T; H = -H; W = -W; }
        } else {
            if (bias < 0.0) { bias = -bias; T = -T; H = -H; W = -W; }
            if (T < -bias) continue;
        }

        if (T < r) {
            double pen = r - T;
            pos.x += H * pen;
            pos.y += W * pen;
            double vn = H * vel.x + W * vel.y;
            if (vn < 0.0) {
                vn *= bCoef * q.bCoef + 1.0;
                vel.x -= H * vn;
                vel.y -= W * vn;
                bounceCount++;
            }
        }
    }

    double radiusSq = r * r;
    for (const auto& q : vertices) {
        if ((q.cMask & cGroup) == 0 || (cMask & q.cGroup) == 0) continue;
        double s = pos.x - q.pos.x;
        double F = pos.y - q.pos.y;
        double distSq = s * s + F * F;
        if (distSq > 0.0 && distSq <= radiusSq) {
            double dist = std::sqrt(distSq);
            s /= dist;
            F /= dist;
            double overlap = r - dist;
            pos.x += s * overlap;
            pos.y += F * overlap;
            double velDot = s * vel.x + F * vel.y;
            if (velDot < 0.0) {
                velDot *= bCoef * q.bCoef + 1.0;
                vel.x -= s * velDot;
                vel.y -= F * velDot;
                bounceCount++;
            }
        }
    }
}

static std::string normalizeAsciiUpper(const std::string& text) {
    std::string out;
    size_t i = 0, n = text.size();
    while (i < n) {
        uint8_t b0 = static_cast<uint8_t>(text[i]);
        uint32_t cp = 0;
        size_t step = 1;
        if ((b0 & 0x80) == 0) { cp = b0; step = 1; }
        else if ((b0 & 0xE0) == 0xC0 && i + 1 < n) {
            cp = ((b0 & 0x1F) << 6) | (static_cast<uint8_t>(text[i + 1]) & 0x3F); step = 2;
        } else if ((b0 & 0xF0) == 0xE0 && i + 2 < n) {
            cp = ((b0 & 0x0F) << 12) | ((static_cast<uint8_t>(text[i + 1]) & 0x3F) << 6) | (static_cast<uint8_t>(text[i + 2]) & 0x3F); step = 3;
        } else if ((b0 & 0xF8) == 0xF0 && i + 3 < n) {
            cp = ((b0 & 0x07) << 18) | ((static_cast<uint8_t>(text[i + 1]) & 0x3F) << 12) | ((static_cast<uint8_t>(text[i + 2]) & 0x3F) << 6) | (static_cast<uint8_t>(text[i + 3]) & 0x3F); step = 4;
        }
        i += step;

        static const uint32_t mathAlphaStarts[] = {
            0x1D400, 0x1D434, 0x1D468, 0x1D49C, 0x1D4D0, 0x1D504,
            0x1D538, 0x1D56C, 0x1D5A0, 0x1D5D4, 0x1D608, 0x1D63C, 0x1D670
        };
        for (uint32_t start : mathAlphaStarts) {
            if (cp >= start && cp < start + 26) { cp = 'A' + (cp - start); break; }
            if (cp >= start + 26 && cp < start + 52) { cp = 'A' + (cp - (start + 26)); break; }
        }
        if (cp >= 'a' && cp <= 'z') cp -= 32;
        if (cp >= 32 && cp < 127) out.push_back(static_cast<char>(cp));
    }
    return out;
}

void TacticalAssistant::resetMatchPowerTracking(const std::string& roomName) {
    std::lock_guard<std::mutex> lock(powerMutex);
    autoGoalHoldTicks = 0;
    std::string norm = normalizeAsciiUpper(roomName);
    isLdcCustomRoom = (norm.find("LDC") != std::string::npos || norm.find("REAL SOCCER") != std::string::npos);
    ldcBoostObserved = false;
    useMultiplicativeBoost = true;
    modelVoteMult = 0;
    modelVoteImp = 0;
    passChainTeam = 0;
    passChainCount = 0;
    lastKickerId = 0;
    lastKickerTeam = 0;
    lastKickTick = 0;
    lastTrackedWorldTick = 0;
    hasPendingKickCalib = false;
    cachedRollValid = false;
    cachedAimValid = false;
    for (int i = 0; i < kMaxPassChainBins; ++i) {
        chainMult[i] = 1.0;
        chainImpMult[i] = 1.0;
        chainCalibrated[i] = false;
    }
}

void TacticalAssistant::onMatchReset() {
    std::lock_guard<std::mutex> lock(powerMutex);
    autoGoalHoldTicks = 0;
    autoSaveHoldTicks = 0;
    fakeShotHoldRemainingTicks = 0;
    passChainTeam = 0;
    passChainCount = 0;
    lastKickerId = 0;
    lastKickerTeam = 0;
    hasPendingKickCalib = false;
    wasMatchPaused = false;
    pendingAutoExcludeRefresh = true;
    lastPlayerDiscIndex = -1;
    cachedRollValid = false;
    cachedAimValid = false;
}

void TacticalAssistant::onAuthoritativePlayerKick(const BallKickEvent& ev) {
    std::lock_guard<std::mutex> lock(powerMutex);
    if (ev.team != 1 && ev.team != 2) return;

    int newChain = 0;
    if (lastKickerTeam == ev.team && lastKickerId != 0 && lastKickerId != ev.playerId &&
        ev.tick >= lastKickTick && (ev.tick - lastKickTick) <= 450) {
        newChain = std::min(passChainCount + 1, kMaxPassChainBins - 1);
    } else {
        newChain = 0;
    }

    passChainTeam = ev.team;
    passChainCount = newChain;
    lastKickerId = ev.playerId;
    lastKickerTeam = ev.team;
    lastKickTick = ev.tick;

    hasPendingKickCalib = true;
    pendingKickEv = ev;
    pendingKickChain = newChain;
}

void TacticalAssistant::onAuthoritativeBallSpeedSet(uint32_t tick, const Vec2& newBallVel) {
    std::lock_guard<std::mutex> lock(powerMutex);
    if (!hasPendingKickCalib) return;
    if (tick < pendingKickEv.tick || (tick - pendingKickEv.tick) > 3) return;

    uint32_t dt = tick - pendingKickEv.tick;
    double decay = std::pow(pendingKickEv.ballDamping, static_cast<double>(dt));
    Vec2 expectedVanilla(pendingKickEv.postBallVel.x * decay, pendingKickEv.postBallVel.y * decay);
    Vec2 expectedPre(pendingKickEv.preBallVel.x * decay, pendingKickEv.preBallVel.y * decay);
    double expectedDeltaV = pendingKickEv.baseDeltaV * decay;

    double vanMag = std::hypot(expectedVanilla.x, expectedVanilla.y);
    double actMag = std::hypot(newBallVel.x, newBallVel.y);
    if (vanMag < 0.5 || actMag < 0.5 || expectedDeltaV < 0.2) {

        return;
    }

    double dirCos = (newBallVel.x * expectedVanilla.x + newBallVel.y * expectedVanilla.y) / (actMag * vanMag);
    if (dirCos < 0.65) {

        return;
    }

    double obsMult = std::clamp(actMag / vanMag, 0.6, 2.5);
    Vec2 vPredMult(expectedVanilla.x * obsMult, expectedVanilla.y * obsMult);
    double errMult = std::hypot(newBallVel.x - vPredMult.x, newBallVel.y - vPredMult.y);

    Vec2 deltaFromPre(newBallVel.x - expectedPre.x, newBallVel.y - expectedPre.y);
    double projN = deltaFromPre.x * pendingKickEv.kickNormal.x + deltaFromPre.y * pendingKickEv.kickNormal.y;
    double obsImpMult = std::clamp(projN / expectedDeltaV, 0.6, 2.5);
    Vec2 vPredImp(expectedPre.x + pendingKickEv.kickNormal.x * (obsImpMult * expectedDeltaV),
                  expectedPre.y + pendingKickEv.kickNormal.y * (obsImpMult * expectedDeltaV));
    double errImp = std::hypot(newBallVel.x - vPredImp.x, newBallVel.y - vPredImp.y);

    if (std::abs(errMult - errImp) > 0.12) {
        if (errMult < errImp) modelVoteMult++;
        else modelVoteImp++;
        useMultiplicativeBoost = (modelVoteMult >= modelVoteImp);
    }

    int b = std::clamp(pendingKickChain, 0, kMaxPassChainBins - 1);
    if (!chainCalibrated[b]) {
        chainMult[b] = obsMult;
        chainImpMult[b] = obsImpMult;
        chainCalibrated[b] = true;
    } else {
        chainMult[b] = chainMult[b] * 0.55 + obsMult * 0.45;
        chainImpMult[b] = chainImpMult[b] * 0.55 + obsImpMult * 0.45;
    }

    if (obsMult > 1.03 || obsImpMult > 1.03) {
        ldcBoostObserved = true;
    }
    hasPendingKickCalib = false;
}

Vec2 TacticalAssistant::predictPostKickBallVelocity(const World& authW, const Disc& ball, const Vec2& ballVel,
                                                    const Vec2& kickNormal, int myTeam, uint32_t myPlayerId,
                                                    bool* outCalibrated, int* outNextChain) const {
    const auto& pp = authW.stadium ? authW.stadium->playerPhysics : PlayerPhysics{};
    double baseKick = (pp.kickStrength > 0.05) ? pp.kickStrength : 5.0;
    double ballInvMass = (ball.invMass > 0.01) ? ball.invMass : 1.0;
    double baseDeltaV = baseKick * ballInvMass;

    int nextChain = 0;
    double mult = 1.0;
    double impMult = 1.0;
    bool useMult = true;
    bool calibrated = true;

    {
        std::lock_guard<std::mutex> lock(powerMutex);
        if (lastKickerTeam == myTeam && lastKickerId != 0 && lastKickerId != myPlayerId &&
            authW.currentTick >= lastKickTick && (authW.currentTick - lastKickTick) <= 450) {
            nextChain = std::min(passChainCount + 1, kMaxPassChainBins - 1);
        } else {
            nextChain = 0;
        }
        useMult = useMultiplicativeBoost;

        if (chainCalibrated[nextChain]) {
            mult = chainMult[nextChain];
            impMult = chainImpMult[nextChain];
            calibrated = true;
        } else if (ldcBoostObserved) {

            double baseM = chainCalibrated[0] ? chainMult[0] : 1.0;
            double baseI = chainCalibrated[0] ? chainImpMult[0] : 1.0;
            double stepM = 0.0, stepI = 0.0;
            int stepSamples = 0;
            for (int c = 1; c < kMaxPassChainBins; ++c) {
                if (chainCalibrated[c]) {
                    stepM += (chainMult[c] - baseM) / static_cast<double>(c);
                    stepI += (chainImpMult[c] - baseI) / static_cast<double>(c);
                    stepSamples++;
                }
            }
            if (stepSamples > 0) {
                stepM = std::clamp(stepM / stepSamples, 0.0, 0.25);
                stepI = std::clamp(stepI / stepSamples, 0.0, 0.25);
            } else {
                stepM = 0.08;
                stepI = 0.08;
            }
            mult = std::clamp(baseM + nextChain * stepM, 0.85, 2.20);
            impMult = std::clamp(baseI + nextChain * stepI, 0.85, 2.20);
            calibrated = (stepSamples >= 2);
        } else if (isLdcCustomRoom || rsRoomDetected) {

            double prior = 1.0 + std::min(nextChain, 6) * 0.08;
            mult = prior;
            impMult = prior;
            calibrated = (nextChain == 0);
        } else {
            mult = 1.0;
            impMult = 1.0;
            calibrated = true;
        }
    }

    if (outCalibrated) *outCalibrated = calibrated;
    if (outNextChain) *outNextChain = nextChain;

    if (useMult) {
        Vec2 vVanilla(ballVel.x + baseDeltaV * kickNormal.x, ballVel.y + baseDeltaV * kickNormal.y);
        return Vec2(vVanilla.x * mult, vVanilla.y * mult);
    } else {
        double effDelta = baseDeltaV * impMult;
        return Vec2(ballVel.x + effDelta * kickNormal.x, ballVel.y + effDelta * kickNormal.y);
    }
}

static uint32_t computeGoalColorUncached(const Stadium& stadium, const Goal& g) {
    auto isChromatic = [](uint32_t c) -> bool {
        uint32_t rgb = c & 0xFFFFFFu;
        if (rgb == 0xC7E6BDu) return false;
        int r = static_cast<int>((rgb >> 16) & 0xFF);
        int gr = static_cast<int>((rgb >> 8) & 0xFF);
        int b = static_cast<int>(rgb & 0xFF);
        int maxC = std::max({r, gr, b});
        int minC = std::min({r, gr, b});
        return (maxC - minC) >= 35;
    };

    auto distPointToSeg = [](const Vec2& p, const Vec2& a, const Vec2& b) -> double {
        double abx = b.x - a.x, aby = b.y - a.y;
        double lenSq = abx * abx + aby * aby;
        if (lenSq < 1e-9) return fastHypot(p.x - a.x, p.y - a.y);
        double t = std::clamp(((p.x - a.x) * abx + (p.y - a.y) * aby) / lenSq, 0.0, 1.0);
        return fastHypot(p.x - (a.x + t * abx), p.y - (a.y + t * aby));
    };

    Vec2 mid((g.p0.x + g.p1.x) * 0.5, (g.p0.y + g.p1.y) * 0.5);

    double bestChromDiscDist = 40.0;
    int32_t bestChromDiscCol = -1;
    double bestAnyDiscDist = 40.0;
    int32_t bestAnyDiscCol = -1;

    for (size_t i = 1; i < stadium.discs.size(); ++i) {
        const auto& d = stadium.discs[i];
        if (!d.isImmovable() || d.color > 0xFFFFFFu) continue;
        double dm = std::min({
            fastHypot(d.pos.x - g.p0.x, d.pos.y - g.p0.y),
            fastHypot(d.pos.x - g.p1.x, d.pos.y - g.p1.y),
            distPointToSeg(d.pos, g.p0, g.p1)
        });
        uint32_t rgb = d.color & 0xFFFFFFu;
        if (dm < bestAnyDiscDist) {
            bestAnyDiscDist = dm;
            bestAnyDiscCol = static_cast<int32_t>(rgb);
        }
        if (isChromatic(rgb) && dm < bestChromDiscDist) {
            bestChromDiscDist = dm;
            bestChromDiscCol = static_cast<int32_t>(rgb);
        }
    }

    double bestChromSegDist = 65.0;
    int32_t bestChromSegCol = -1;
    double bestAnySegDist = 50.0;
    int32_t bestAnySegCol = -1;

    for (const auto& seg : stadium.segments) {
        if (!seg.visible || seg.color < 0) continue;
        if (seg.v0 < 0 || seg.v0 >= (int)stadium.vertices.size() ||
            seg.v1 < 0 || seg.v1 >= (int)stadium.vertices.size()) continue;
        uint32_t rgb = static_cast<uint32_t>(seg.color) & 0xFFFFFFu;
        if (rgb == 0xC7E6BDu) continue;

        const Vec2& v0 = stadium.vertices[seg.v0].pos;
        const Vec2& v1 = stadium.vertices[seg.v1].pos;
        double dm = std::min({
            distPointToSeg(g.p0, v0, v1),
            distPointToSeg(g.p1, v0, v1),
            distPointToSeg(mid, v0, v1),
            distPointToSeg(v0, g.p0, g.p1),
            distPointToSeg(v1, g.p0, g.p1)
        });
        if (dm < bestAnySegDist) {
            bestAnySegDist = dm;
            bestAnySegCol = static_cast<int32_t>(rgb);
        }
        if (isChromatic(rgb) && dm < bestChromSegDist) {
            bestChromSegDist = dm;
            bestChromSegCol = static_cast<int32_t>(rgb);
        }
    }

    if (bestChromDiscCol >= 0) return static_cast<uint32_t>(bestChromDiscCol);
    if (bestChromSegCol >= 0) return static_cast<uint32_t>(bestChromSegCol);
    if (bestAnyDiscCol >= 0) return static_cast<uint32_t>(bestAnyDiscCol);
    if (bestAnySegCol >= 0) return static_cast<uint32_t>(bestAnySegCol);

    if (g.team == 1) return 0xE56E56u;
    if (g.team == 2) return 0x5689E5u;
    return 0x94A3B8u;
}

struct StadiumGoalColorCache {
    const Stadium* stadiumPtr = nullptr;
    size_t nGoals = 0;
    size_t nDiscs = 0;
    size_t nSegs = 0;
    size_t nVerts = 0;
    std::string name;
    Vec2 firstGoalP0{0.0, 0.0};
    std::vector<uint32_t> perGoalColor;
    std::vector<uint32_t> uniqueColors;

    void ensureBuilt(const Stadium& st) {
        Vec2 p0 = st.goals.empty() ? Vec2{0.0, 0.0} : st.goals[0].p0;
        if (stadiumPtr == &st &&
            nGoals == st.goals.size() &&
            nDiscs == st.discs.size() &&
            nSegs == st.segments.size() &&
            nVerts == st.vertices.size() &&
            firstGoalP0.x == p0.x && firstGoalP0.y == p0.y &&
            name == st.name) {
            return;
        }
        stadiumPtr = &st;
        nGoals = st.goals.size();
        nDiscs = st.discs.size();
        nSegs = st.segments.size();
        nVerts = st.vertices.size();
        firstGoalP0 = p0;
        name = st.name;

        perGoalColor.clear();
        uniqueColors.clear();
        perGoalColor.reserve(nGoals);
        for (const auto& g : st.goals) {
            uint32_t col = computeGoalColorUncached(st, g);
            perGoalColor.push_back(col);
            if (std::find(uniqueColors.begin(), uniqueColors.end(), col) == uniqueColors.end()) {
                uniqueColors.push_back(col);
            }
        }
    }
};

static StadiumGoalColorCache& getStadiumGoalColorCache(const Stadium& st) {
    thread_local StadiumGoalColorCache cache;
    cache.ensureBuilt(st);
    return cache;
}

uint32_t TacticalAssistant::getGoalColor(const Stadium& stadium, const Goal& g) {
    const auto& cache = getStadiumGoalColorCache(stadium);
    if (!stadium.goals.empty()) {
        const Goal* base = stadium.goals.data();
        if (&g >= base && &g < base + stadium.goals.size()) {
            return cache.perGoalColor[static_cast<size_t>(&g - base)];
        }
        for (size_t i = 0; i < stadium.goals.size(); ++i) {
            if (stadium.goals[i].p0.x == g.p0.x && stadium.goals[i].p0.y == g.p0.y &&
                stadium.goals[i].p1.x == g.p1.x && stadium.goals[i].p1.y == g.p1.y) {
                return cache.perGoalColor[i];
            }
        }
    }
    return computeGoalColorUncached(stadium, g);
}

std::vector<uint32_t> TacticalAssistant::getAvailableGoalColors(const Stadium& stadium) {
    return getStadiumGoalColorCache(stadium).uniqueColors;
}

double TacticalAssistant::evaluateAutoGoalAccuracy(const World& authW, uint32_t myPlayerId, int myTeam,
                                                   const Vec2& pPos, const Vec2& bPos, const Vec2& bVel,
                                                   int timeShiftTicks, double speedScale) const {
    if (!authW.hasMatch || !authW.stadium || authW.discs.empty() || authW.stadium->goals.empty()) return 0.0;
    if (myTeam != 1 && myTeam != 2) return 0.0;

    const Goal* targetGoal = nullptr;
    const Goal* ownGoal = nullptr;
    if (!autoGoalAnyGoal) {
        for (const auto& g : authW.stadium->goals) {
            if (g.team == myTeam) ownGoal = &g;
            else if (g.team != 0) targetGoal = &g;
        }
        if (!targetGoal) {
            for (const auto& g : authW.stadium->goals) {
                double gx = (g.p0.x + g.p1.x) * 0.5;
                if ((myTeam == 1 && gx > 0.0) || (myTeam == 2 && gx < 0.0)) targetGoal = &g;
                else ownGoal = &g;
            }
        }
        if (!targetGoal) return 0.0;
    }

    const Player* me = authW.getPlayer(myPlayerId);
    if (!me || me->discIndex < 0 || me->discIndex >= (int)authW.discs.size()) return 0.0;

    const Disc& ball = authW.discs[0];
    const Disc& myDisc = authW.discs[me->discIndex];
    double sumR = ball.radius + myDisc.radius;
    double dx = bPos.x - pPos.x;
    double dy = bPos.y - pPos.y;
    double dist = std::hypot(dx, dy);
    if (dist < 1e-6 || (dist - sumR) >= 3.98) return 0.0;

    Vec2 kickNormal(dx / dist, dy / dist);
    bool chainCalib = true;
    int nextChain = 0;
    Vec2 vel = predictPostKickBallVelocity(authW, ball, bVel, kickNormal, myTeam, myPlayerId, &chainCalib, &nextChain);
    vel.x *= speedScale;
    vel.y *= speedScale;

    double launchSpeed = std::hypot(vel.x, vel.y);
    if (launchSpeed < 1.2) return 0.0;

    double damp = (ball.damping > 0.5 && ball.damping < 1.0) ? ball.damping : 0.99;

    constexpr int kMaxSimTicks = 180;
    Vec2 pathPos[kMaxSimTicks + 1];
    Vec2 pos = bPos;
    pathPos[0] = pos;

    bool scored = false;
    const Goal* scoredGoal = nullptr;
    int arrivalTick = 0;
    double arrivalSpeed = 0.0;
    double hitFrac = 0.5;
    Vec2 hitPoint{0.0, 0.0};
    int bounceCount = 0;
    double minPostClearance = 1e9;

    for (int t = 1; t <= kMaxSimTicks; ++t) {
        Vec2 prev = pos;
        pos.x += vel.x;
        pos.y += vel.y;
        vel.x = damp * (vel.x + ball.gravity.x);
        vel.y = damp * (vel.y + ball.gravity.y);

        resolveBallStaticCollisions(authW, ball, pos, vel, bounceCount, minPostClearance);
        pathPos[t] = pos;

        if (!autoGoalAnyGoal) {
            if (ownGoal && ownGoal->check(pos, prev)) {
                return 0.0;
            }

            if (targetGoal->check(pos, prev)) {
                Vec2 goalCenter((targetGoal->p0.x + targetGoal->p1.x) * 0.5, (targetGoal->p0.y + targetGoal->p1.y) * 0.5);
                Vec2 inwardDir = safeNormalize(goalCenter);
                Vec2 segStep(pos.x - prev.x, pos.y - prev.y);
                if (segStep.x * inwardDir.x + segStep.y * inwardDir.y <= 0.0) {
                    return 0.0;
                }
                scoredGoal = targetGoal;
            }
        } else {
            for (const auto& g : authW.stadium->goals) {
                if (g.check(pos, prev)) {
                    if (autoGoalExcludeColor >= 0 &&
                        getGoalColor(*authW.stadium, g) == static_cast<uint32_t>(autoGoalExcludeColor)) {
                        return 0.0;
                    }
                    Vec2 goalCenter((g.p0.x + g.p1.x) * 0.5, (g.p0.y + g.p1.y) * 0.5);
                    Vec2 inwardDir = safeNormalize(goalCenter);
                    Vec2 segStep(pos.x - prev.x, pos.y - prev.y);
                    if ((inwardDir.x != 0.0 || inwardDir.y != 0.0) &&
                        (segStep.x * inwardDir.x + segStep.y * inwardDir.y <= 0.0)) {
                        return 0.0;
                    }
                    scoredGoal = &g;
                    break;
                }
            }
        }

        if (scoredGoal) {
            double rX = pos.x - prev.x, rY = pos.y - prev.y;
            double sX = scoredGoal->p1.x - scoredGoal->p0.x;
            double sY = scoredGoal->p1.y - scoredGoal->p0.y;
            double denom = rX * sY - rY * sX;
            if (std::abs(denom) > 1e-9) {
                double u = ((scoredGoal->p0.x - prev.x) * rY - (scoredGoal->p0.y - prev.y) * rX) / denom;
                hitFrac = std::clamp(u, 0.0, 1.0);
            }
            hitPoint = Vec2(scoredGoal->p0.x + sX * hitFrac, scoredGoal->p0.y + sY * hitFrac);
            scored = true;
            arrivalTick = t;
            arrivalSpeed = std::hypot(vel.x, vel.y);
            break;
        }

        if (std::hypot(vel.x, vel.y) < 0.08) {
            break;
        }
    }

    if (!scored || !scoredGoal || arrivalTick <= 0) return 0.0;

    double goalLen = std::hypot(scoredGoal->p1.x - scoredGoal->p0.x, scoredGoal->p1.y - scoredGoal->p0.y);
    double postMargin = std::min(hitFrac, 1.0 - hitFrac) * goalLen;
    double shotDist = std::hypot(hitPoint.x - bPos.x, hitPoint.y - bPos.y);
    double effectiveMargin = postMargin - shotDist * 0.013;
    if (effectiveMargin <= 0.0) return 0.0;

    double rB = ball.radius > 1.0 ? ball.radius : 10.0;
    double postScore = (effectiveMargin >= rB * 1.6)
        ? 1.0
        : (0.80 + 0.20 * (effectiveMargin / (rB * 1.6)));
    if (minPostClearance < 1.5) {
        postScore *= (0.88 + 0.08 * std::max(0.0, minPostClearance));
    }

    const auto& pp = authW.stadium->playerPhysics;
    double pAccel = (pp.acceleration > 0.01) ? pp.acceleration : 0.1;
    double pDamp  = (pp.damping > 0.5 && pp.damping < 1.0) ? pp.damping : 0.96;
    double oneMinusD = std::max(1e-4, 1.0 - pDamp);
    double maxSpeed = pAccel / oneMinusD;

    double minOppMargin = 1e9;
    double teammateScore = 1.0;

    for (const auto& p : authW.players) {
        if (p.id == myPlayerId || p.team == 0 || p.discIndex < 0 || p.discIndex >= (int)authW.discs.size()) continue;
        const Disc& oDisc = authW.discs[p.discIndex];

        bool isDefender = autoGoalAnyGoal
            ? (scoredGoal->team != 0 ? (p.team == scoredGoal->team) : (p.team != myTeam))
            : (p.team != myTeam);

        if (!isDefender) {
            double tmSumR = rB + oDisc.radius;
            for (int t = 1; t <= arrivalTick; ++t) {
                double effT = static_cast<double>(t + timeShiftTicks);
                double coastFactor = (1.0 - std::pow(pDamp, effT)) / oneMinusD;
                Vec2 qCoast(oDisc.pos.x + oDisc.speed.x * coastFactor, oDisc.pos.y + oDisc.speed.y * coastFactor);
                double d = std::hypot(pathPos[t].x - qCoast.x, pathPos[t].y - qCoast.y);
                if (d < tmSumR + 0.5) return 0.0;
                if (d < tmSumR + 5.0) {
                    teammateScore = std::min(teammateScore, 0.82 + 0.18 * ((d - (tmSumR + 0.5)) / 4.5));
                }
            }
        } else {
            bool isSprinting = (p.headlessAvatar.has_value() && (*p.headlessAvatar == "⚡" || *p.headlessAvatar == "💨"));
            double sprintMult = isSprinting ? 1.25 : 1.0;
            double blockRadius = rB + oDisc.radius + 4.0;
            double oppSpd = std::hypot(oDisc.speed.x, oDisc.speed.y);

            for (int t = 1; t <= arrivalTick; ++t) {
                double effT = static_cast<double>(t + timeShiftTicks);
                double coastFactor = (1.0 - std::pow(pDamp, effT)) / oneMinusD;
                Vec2 qCoast(oDisc.pos.x + oDisc.speed.x * coastFactor, oDisc.pos.y + oDisc.speed.y * coastFactor);
                double d = std::hypot(pathPos[t].x - qCoast.x, pathPos[t].y - qCoast.y);
                if (d <= blockRadius) return 0.0;

                double reactTicks = 7.0;
                if (oppSpd > 0.25) {
                    Vec2 toBall = safeNormalize(Vec2(pathPos[t].x - oDisc.pos.x, pathPos[t].y - oDisc.pos.y));
                    double align = (oDisc.speed.x * toBall.x + oDisc.speed.y * toBall.y) / oppSpd;
                    if (align > 0.5) reactTicks = 2.5;
                    else if (align < -0.3) reactTicks = 9.0;
                }

                double moveT = std::max(0.0, effT - reactTicks);
                double powD = std::pow(pDamp, moveT);
                double oppReach = sprintMult * maxSpeed * (moveT - pDamp * (1.0 - powD) / oneMinusD);
                double margin = d - (blockRadius + oppReach);
                if (margin < minOppMargin) minOppMargin = margin;
            }
        }
    }

    double defenseScore = 1.0;
    if (minOppMargin >= 10.0) {
        defenseScore = 1.0;
    } else if (minOppMargin >= 0.0) {
        defenseScore = 0.94 + 0.06 * (minOppMargin / 10.0);
    } else if (minOppMargin > -16.0) {
        defenseScore = 0.94 * (1.0 + minOppMargin / 16.0);
    } else {
        return 0.0;
    }

    double speedScore = 1.0;
    if (arrivalSpeed >= 1.8) {
        speedScore = 1.0;
    } else if (arrivalSpeed >= 0.5) {
        speedScore = 0.90 + 0.10 * ((arrivalSpeed - 0.5) / 1.3);
    } else {
        speedScore = 0.90 * (arrivalSpeed / 0.5);
    }

    double flightScore = 1.0;
    if (bounceCount == 1) flightScore *= 0.96;
    else if (bounceCount >= 2) flightScore *= 0.86;
    if (arrivalTick > 130) {
        flightScore *= std::max(0.80, 1.0 - (arrivalTick - 130) * 0.0012);
    }

    double acc = 100.0 * postScore * defenseScore * teammateScore * speedScore * flightScore;

    if (speedScale == 1.0 && nextChain > 0 && !chainCalib && acc >= 80.0) {
        double accLow = evaluateAutoGoalAccuracy(authW, myPlayerId, myTeam, pPos, bPos, bVel, timeShiftTicks, 0.92);
        if (accLow <= 0.0) {
            acc *= 0.85;
        } else {
            acc = std::min(acc, 0.5 * acc + 0.5 * accLow);
        }
    }

    return std::clamp(acc, 0.0, 100.0);
}

void TacticalAssistant::simulateBallTrajectory(const World& world, Vec2 pos, Vec2 vel, int maxTicks, std::vector<Vec2>& outPoints) const {
    outPoints.clear();
    if (!world.stadium || world.discs.empty()) return;

    const Disc& ball = world.discs[0];
    double damp = (ball.damping > 0.5 && ball.damping < 1.0) ? ball.damping : 0.99;

    outPoints.reserve(maxTicks / 2 + 2);
    outPoints.push_back(pos);

    int bounceCount = 0;
    double minPostClearance = 1e9;
    int ticks = std::clamp(maxTicks, 10, 200);
    for (int t = 1; t <= ticks; ++t) {
        pos.x += vel.x;
        pos.y += vel.y;
        vel.x = damp * (vel.x + ball.gravity.x);
        vel.y = damp * (vel.y + ball.gravity.y);

        resolveBallStaticCollisions(world, ball, pos, vel, bounceCount, minPostClearance);

        if ((t % 2 == 0) || t == ticks) {
            outPoints.push_back(pos);
        }
    }
}

void TacticalAssistant::triggerSlideMacro() {
    if (!rsHelperEnabled || !rsOneTouchSlideEnabled) return;
    rsMacroSprintTriggered = false;
    rsMacroSlideTriggered = true;
    rsMacroElapsedTicks = 0;
}

void TacticalAssistant::triggerSprintMacro() {
    if (!rsHelperEnabled || !rsOneTouchSprintEnabled) return;
    rsMacroSlideTriggered = false;
    rsMacroSprintTriggered = true;
    rsMacroElapsedTicks = 0;
}

void TacticalAssistant::cancelMacros() {
    rsMacroSlideTriggered = false;
    rsMacroSprintTriggered = false;
    rsMacroElapsedTicks = 0;
    rsCurrentState = RSSuperpowerState::None;
    rsChargeProgress = 0.0f;
}

void TacticalAssistant::onCooldownNoticeReceived(int seconds) {
    rsCooldownRemainingMs = static_cast<float>(seconds * 1000);
}

void TacticalAssistant::updateTimers(float dtSeconds) {
    float dtMs = dtSeconds * 1000.0f;
    if (rsCooldownRemainingMs > 0.0f) {
        rsCooldownRemainingMs = std::max(0.0f, rsCooldownRemainingMs - dtMs);
    }

    if (typingSpamEnabled) {
        typingSpamTimer += dtMs;
        int interval = std::clamp(typingSpamSpeedMs, 5, 500);
        if (typingSpamTimer >= interval) {
            typingSpamTimer = 0.0f;
            typingSpamState = !typingSpamState;
        }
    } else {
        typingSpamTimer = 0.0f;
        typingSpamState = false;
    }
}

void TacticalAssistant::analyze(const World& renderWorld, const World* realWorld, uint32_t myPlayerId) {
    manualAimIsNearBall = false;
    manualAimCanKick = false;

    const World& authW = (realWorld && realWorld->hasMatch && !realWorld->discs.empty()) ? *realWorld : renderWorld;

    if (!authW.hasMatch || authW.discs.empty() || !authW.stadium) {
        ballRollPath.clear();
        manualAimPath.clear();
        cachedRollValid = false;
        cachedAimValid = false;
        return;
    }

    {
        const auto& curGoalColors = getStadiumGoalColorCache(*authW.stadium).uniqueColors;
        if (curGoalColors != lastStadiumGoalColors) {
            lastStadiumGoalColors = curGoalColors;
            pendingAutoExcludeRefresh = true;
        }

        const Player* meTrack = authW.getPlayer(myPlayerId);
        int curDiscIdx = (meTrack && meTrack->discIndex >= 0 && meTrack->discIndex < (int)authW.discs.size())
            ? meTrack->discIndex : -1;
        if (curDiscIdx >= 0 && lastPlayerDiscIndex < 0) {
            pendingAutoExcludeRefresh = true;
        }
        lastPlayerDiscIndex = curDiscIdx;

        if (authW.pauseTickCounter > 0) {
            wasMatchPaused = true;
        } else if (wasMatchPaused) {
            wasMatchPaused = false;
            pendingAutoExcludeRefresh = true;
        }

        if (pendingAutoExcludeRefresh && authW.pauseTickCounter == 0 && autoGoalAnyGoal &&
            !authW.stadium->goals.empty() && curDiscIdx >= 0) {
            const Vec2& myPos = authW.discs[curDiscIdx].pos;
            Vec2 arenaCenter{0.0, 0.0};
            for (const auto& g : authW.stadium->goals) {
                arenaCenter.x += (g.p0.x + g.p1.x) * 0.5;
                arenaCenter.y += (g.p0.y + g.p1.y) * 0.5;
            }
            arenaCenter.x /= static_cast<double>(authW.stadium->goals.size());
            arenaCenter.y /= static_cast<double>(authW.stadium->goals.size());

            double bestDist = 1e18;
            double secondDist = 1e18;
            const Goal* closestGoal = nullptr;
            for (const auto& g : authW.stadium->goals) {
                double abx = g.p1.x - g.p0.x, aby = g.p1.y - g.p0.y;
                double lenSq = abx * abx + aby * aby;
                double d = 0.0;
                if (lenSq < 1e-9) {
                    d = std::hypot(myPos.x - g.p0.x, myPos.y - g.p0.y);
                } else {
                    double t = std::clamp(((myPos.x - g.p0.x) * abx + (myPos.y - g.p0.y) * aby) / lenSq, 0.0, 1.0);
                    d = std::hypot(myPos.x - (g.p0.x + t * abx), myPos.y - (g.p0.y + t * aby));
                }
                if (d < bestDist) {
                    secondDist = bestDist;
                    bestDist = d;
                    closestGoal = &g;
                } else if (d < secondDist) {
                    secondDist = d;
                }
            }

            double distFromCenter = std::hypot(myPos.x - arenaCenter.x, myPos.y - arenaCenter.y);
            bool atAGoal = closestGoal && (bestDist < distFromCenter) &&
                           (authW.stadium->goals.size() == 1 || bestDist < secondDist * 0.75);

            if (atAGoal) {
                autoGoalExcludeColor = static_cast<int32_t>(getGoalColor(*authW.stadium, *closestGoal));
                pendingAutoExcludeRefresh = false;
            } else {

                bool oldColorStillValid = (autoGoalExcludeColor >= 0) &&
                    (std::find(curGoalColors.begin(), curGoalColors.end(),
                               static_cast<uint32_t>(autoGoalExcludeColor)) != curGoalColors.end());
                if (oldColorStillValid) {
                    pendingAutoExcludeRefresh = false;
                }
            }
        }
    }

    const Disc& ball = authW.discs[0];

    {
        std::lock_guard<std::mutex> lock(powerMutex);
        if (authW.currentTick != lastTrackedWorldTick) {
            lastTrackedWorldTick = authW.currentTick;

            if (authW.matchState != 1) {
                passChainTeam = 0;
                passChainCount = 0;
                lastKickerId = 0;
                lastKickerTeam = 0;
                hasPendingKickCalib = false;
            } else {

                if (hasPendingKickCalib && authW.currentTick > pendingKickEv.tick + 4) {
                    int b = std::clamp(pendingKickChain, 0, kMaxPassChainBins - 1);
                    if (!chainCalibrated[b]) {
                        chainMult[b] = 1.0;
                        chainImpMult[b] = 1.0;
                        chainCalibrated[b] = true;
                    } else {
                        chainMult[b] = chainMult[b] * 0.70 + 1.0 * 0.30;
                        chainImpMult[b] = chainImpMult[b] * 0.70 + 1.0 * 0.30;
                    }
                    hasPendingKickCalib = false;
                }

                if (passChainTeam != 0) {
                    if (authW.currentTick > lastKickTick + 450) {
                        passChainTeam = 0;
                        passChainCount = 0;
                    } else {
                        for (const auto& p : authW.players) {
                            if (p.team == 0 || p.team == passChainTeam) continue;
                            if (p.discIndex < 0 || p.discIndex >= (int)authW.discs.size()) continue;
                            const Disc& oD = authW.discs[p.discIndex];
                            double d = std::hypot(ball.pos.x - oD.pos.x, ball.pos.y - oD.pos.y);
                            if (d <= ball.radius + oD.radius + 1.5) {
                                passChainTeam = 0;
                                passChainCount = 0;
                                lastKickerId = p.id;
                                lastKickerTeam = p.team;
                                lastKickTick = authW.currentTick;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    if (showBallRollTrajectory) {
        double ballSpeed = fastHypot(ball.speed.x, ball.speed.y);
        if (ballSpeed > 0.08) {
            bool rollHit = cachedRollValid &&
                           cachedRollStadium == authW.stadium.get() &&
                           cachedRollTicks == trajectoryPreviewTicks &&
                           cachedRollBallPos.x == ball.pos.x && cachedRollBallPos.y == ball.pos.y &&
                           cachedRollBallVel.x == ball.speed.x && cachedRollBallVel.y == ball.speed.y;
            if (!rollHit) {
                simulateBallTrajectory(authW, ball.pos, ball.speed, trajectoryPreviewTicks, ballRollPath);
                cachedRollStadium = authW.stadium.get();
                cachedRollBallPos = ball.pos;
                cachedRollBallVel = ball.speed;
                cachedRollTicks = trajectoryPreviewTicks;
                cachedRollValid = true;
            }
        } else {
            ballRollPath.clear();
            cachedRollValid = false;
        }
    } else {
        ballRollPath.clear();
        cachedRollValid = false;
    }

    const Player* me = authW.getPlayer(myPlayerId);
    if (!me || me->discIndex < 0 || me->discIndex >= (int)authW.discs.size()) {
        manualAimPath.clear();
        cachedAimValid = false;
        return;
    }

    const Disc& myDisc = authW.discs[me->discIndex];
    double sumR = ball.radius + myDisc.radius;
    double distMeBall = fastHypot(ball.pos.x - myDisc.pos.x, ball.pos.y - myDisc.pos.y);
    double gap = distMeBall - sumR;

    double maxAimGap = static_cast<double>(std::clamp(aimLineDistance, 20, 300));
    manualAimIsNearBall = (gap <= maxAimGap);
    manualAimCanKick = (gap < 4.0) && (me->kickCooldown <= 0 && me->kickRateCounter >= 0);

    if (showLiveAimTrajectory && manualAimIsNearBall) {
        int curChain = 0;
        {
            std::lock_guard<std::mutex> lock(powerMutex);
            curChain = passChainCount;
        }
        bool aimHit = cachedAimValid &&
                      cachedAimStadium == authW.stadium.get() &&
                      cachedAimTicks == trajectoryPreviewTicks &&
                      cachedAimTeam == me->team &&
                      cachedAimChain == curChain &&
                      cachedAimBallPos.x == ball.pos.x && cachedAimBallPos.y == ball.pos.y &&
                      cachedAimBallVel.x == ball.speed.x && cachedAimBallVel.y == ball.speed.y &&
                      cachedAimMyPos.x == myDisc.pos.x && cachedAimMyPos.y == myDisc.pos.y;
        if (!aimHit) {
            Vec2 contactNormal = safeNormalize(Vec2(ball.pos.x - myDisc.pos.x, ball.pos.y - myDisc.pos.y));
            Vec2 kickVel = predictPostKickBallVelocity(authW, ball, ball.speed, contactNormal, me->team, myPlayerId);
            simulateBallTrajectory(authW, ball.pos, kickVel, trajectoryPreviewTicks, manualAimPath);
            cachedAimStadium = authW.stadium.get();
            cachedAimBallPos = ball.pos;
            cachedAimBallVel = ball.speed;
            cachedAimMyPos = myDisc.pos;
            cachedAimTicks = trajectoryPreviewTicks;
            cachedAimTeam = me->team;
            cachedAimChain = curChain;
            cachedAimValid = true;
        }
    } else {
        manualAimPath.clear();
        cachedAimValid = false;
    }

    for (const auto& p : authW.players) {
        if (p.headlessAvatar.has_value()) {
            const std::string& hav = *p.headlessAvatar;
            if (hav == "👟" || hav == "⚡" || hav == "💨" || hav == "🚫") {
                rsRoomDetected = true;
                break;
            }
        }
    }

    if (me) {
        if (me->headlessAvatar.has_value() && !me->headlessAvatar->empty()) {
            rsRoomDetected = true;
            const std::string& myAv = *me->headlessAvatar;
            if (myAv == "👟") {
                rsCurrentState = RSSuperpowerState::SlideReady;
            } else if (myAv == "⚡" || myAv == "💨") {
                rsCurrentState = RSSuperpowerState::SprintReady;
            } else if (myAv == "🚫") {
                cancelMacros();
                rsCurrentState = RSSuperpowerState::None;
            }
        } else {

            if (rsCurrentState == RSSuperpowerState::SlideReady || rsCurrentState == RSSuperpowerState::SprintReady) {

                if (rsManualKickTicks > 0) {
                    rsCurrentState = RSSuperpowerState::Cancelled;
                } else {
                    rsCurrentState = RSSuperpowerState::None;
                }
            } else if (rsCurrentState == RSSuperpowerState::Charging && rsManualKickTicks == 0 && !rsMacroSlideTriggered && !rsMacroSprintTriggered) {
                rsCurrentState = RSSuperpowerState::None;
            }
        }
    }
}

void TacticalAssistant::resetTickSync() {
    lastProcessedTick = 0xFFFFFFFFu;
    activeKickPulseTick = 0xFFFFFFFFu;
    forcedReleaseTick = 0xFFFFFFFFu;
    suppressHaloReleaseTicks = 0;
    suppressLocalKickHalo = false;
    isFakeShooting = false;
    isRealShooting = false;
    fakeShotJustPulsed = false;
    kickJustPulsed = false;
    autoGoalHoldTicks = 0;
    autoSaveHoldTicks = 0;
    fakeShotHoldRemainingTicks = 0;
    kickCooldownWaitTicks = 0;
    fakeShotCooldownRemainingTicks = 0;
    fastFakeRetreatTicksRemaining = 0;
    lastSlowMoveAllowed = true;
}

uint32_t TacticalAssistant::computeInputMask(const World& renderWorld, const World* realWorld, uint32_t myPlayerId, uint32_t manualKeyMask, uint32_t lastSentMask, uint32_t simTick) {
    if (!renderWorld.hasMatch || renderWorld.discs.empty() || !renderWorld.stadium) {
        isFakeShooting = false;
        isRealShooting = false;
        suppressLocalKickHalo = false;
        return manualKeyMask;
    }

    bool isNewTick = (simTick == 0xFFFFFFFFu) || (simTick != lastProcessedTick);
    if (isNewTick && simTick != 0xFFFFFFFFu) {
        lastProcessedTick = simTick;
    }

    if (isNewTick) {
        isFakeShooting = false;
        isRealShooting = false;
        if (suppressHaloReleaseTicks > 0) {
            suppressHaloReleaseTicks--;
        }
        suppressLocalKickHalo = (suppressHaloReleaseTicks > 0) && ((manualKeyMask & 16u) == 0);
    } else if ((manualKeyMask & 16u) != 0) {
        suppressLocalKickHalo = false;
    }

    uint32_t outMask = manualKeyMask;

    if (penaltyGkEnabled || smartGkEnabled) {
        const World& authW = (realWorld && realWorld->hasMatch && !realWorld->discs.empty()) ? *realWorld : renderWorld;
        uint32_t gkMask = penaltyGkEnabled
            ? computePenaltyGkInput(authW, myPlayerId)
            : computeSmartGkInput(authW, myPlayerId);

        if (isNewTick && (lastSentMask & 16u) != 0 && (gkMask & 16u) != 0) {
            gkMask &= ~16u;
        }
        return gkMask;
    }

    if (autoShakeEnabled && holdingShakeKey) {
        isAutoShaking = true;
        if (isNewTick) {
            autoShakeTickCounter++;
        }
        int period = std::clamp(autoShakeFrequencyTicks, 1, 12);
        bool phase = ((autoShakeTickCounter / period) & 1) == 0;
        uint32_t dirBits = outMask & 15u;
        if (dirBits == 0) {
            outMask = (outMask & ~15u) | (phase ? 4u : 8u);
        } else if ((dirBits & 12u) != 0 && (dirBits & 3u) == 0) {
            outMask = (outMask & ~3u) | (phase ? 1u : 2u);
        } else {
            outMask = (outMask & ~12u) | (phase ? 4u : 8u);
        }
    } else {
        isAutoShaking = false;
        if (isNewTick) autoShakeTickCounter = 0;
    }

    const World& authWorld = (realWorld && realWorld->hasMatch && !realWorld->discs.empty()) ? *realWorld : renderWorld;
    const Player* myPl = authWorld.getPlayer(myPlayerId);

    if (ballOrbitEnabled && holdingOrbitKey && myPl && myPl->discIndex >= 0 &&
        myPl->discIndex < (int)authWorld.discs.size() && !authWorld.discs.empty()) {
        const Disc& meD = authWorld.discs[myPl->discIndex];
        const Disc& bD = authWorld.discs[0];

        double clearance = std::clamp(ballOrbitDistance, 0, 20);
        double targetDist = bD.radius + meD.radius + (clearance == 0 ? 0.08 : clearance);

        double rx = meD.pos.x - bD.pos.x;
        double ry = meD.pos.y - bD.pos.y;
        double currentDist = std::hypot(rx, ry);

        if (currentDist > 0.001) {
            isOrbitingBall = true;
            double ux = rx / currentDist;
            double uy = ry / currentDist;

            double effectiveOrbitDir = (orbitDirection >= 0 ? 1.0 : -1.0);
            double tx = -uy * effectiveOrbitDir;
            double ty = ux * effectiveOrbitDir;

            double distDiff = currentDist - targetDist;
            double accelVal = (authWorld.stadium) ? authWorld.stadium->playerPhysics.acceleration : 0.1;
            double playerDamp = (authWorld.stadium) ? authWorld.stadium->playerPhysics.damping : 0.96;

            Vec2 nextBallPos(bD.pos.x + bD.speed.x, bD.pos.y + bD.speed.y);

            static const uint32_t candidateMasks[9] = { 1, 2, 4, 8, 5, 9, 6, 10, 0 };
            uint32_t bestMask = 0;
            double bestScore = -1e9;
            double minAllowedDist = bD.radius + meD.radius + 0.01;

            for (uint32_t mask : candidateMasks) {
                double ix = ((mask & 8) > 0) - ((mask & 4) > 0);
                double iy = ((mask & 2) > 0) - ((mask & 1) > 0);
                if (ix != 0.0 && iy != 0.0) {
                    ix *= 0.7071067811865475;
                    iy *= 0.7071067811865475;
                }

                Vec2 testSpeed = meD.speed;
                testSpeed.x += ix * accelVal;
                testSpeed.y += iy * accelVal;

                Vec2 testPos(meD.pos.x + testSpeed.x, meD.pos.y + testSpeed.y);
                testSpeed.x *= playerDamp;
                testSpeed.y *= playerDamp;

                double nextRx = testPos.x - nextBallPos.x;
                double nextRy = testPos.y - nextBallPos.y;
                double nextDist = std::hypot(nextRx, nextRy);

                double penalty = 0.0;
                if (nextDist < minAllowedDist) {
                    penalty = (minAllowedDist - nextDist) * 1000.0;
                }

                double tangProgress = (nextRx * tx + nextRy * ty);
                double distError = std::abs(nextDist - targetDist);
                double score = tangProgress * 5.0 - distError * 25.0 - penalty;

                if (score > bestScore) {
                    bestScore = score;
                    bestMask = mask;
                }
            }

            if (bestMask != 0 || distDiff <= 0.0) {
                outMask = (outMask & ~15u) | bestMask;
            }
        } else {
            isOrbitingBall = false;
        }
    } else {
        isOrbitingBall = false;
    }

    if (holdingSlowKey && (outMask & 15u) != 0) {
        if (isNewTick) {
            slowAccumulator += std::clamp(slowSpeedPercent, 10, 90);
            if (slowAccumulator >= 100) {
                slowAccumulator -= 100;
                lastSlowMoveAllowed = true;
            } else {
                lastSlowMoveAllowed = false;
            }
        }
        if (!lastSlowMoveAllowed) {
            outMask &= ~15u;
        }
    } else {
        if (isNewTick) {
            slowAccumulator = 0;
            lastSlowMoveAllowed = true;
        }
    }

    if (firstTouchCushionEnabled && !isOrbitingBall && !isAutoShaking &&
        (manualKeyMask & 16u) == 0 && !holdingRealShotKey &&
        myPl && myPl->discIndex >= 0 && myPl->discIndex < (int)authWorld.discs.size() && !authWorld.discs.empty()) {
        const Disc& meD = authWorld.discs[myPl->discIndex];
        const Disc& bD = authWorld.discs[0];
        double bSpeed = std::hypot(bD.speed.x, bD.speed.y);
        double dx0 = bD.pos.x - meD.pos.x;
        double dy0 = bD.pos.y - meD.pos.y;
        double dist0 = std::hypot(dx0, dy0);
        double minDist = bD.radius + meD.radius;
        double gap0 = dist0 - minDist;

        if (bSpeed >= 2.5 && dist0 > 1e-4 && gap0 > 0.02 && gap0 <= 8.5) {
            double nx = dx0 / dist0;
            double ny = dy0 / dist0;

            double closingSpeed = -((bD.speed.x - meD.speed.x) * nx + (bD.speed.y - meD.speed.y) * ny);
            if (closingSpeed >= 2.35) {
                const auto& pp = authWorld.stadium ? authWorld.stadium->playerPhysics : PlayerPhysics{};
                double pAcc = (pp.acceleration > 0.001) ? pp.acceleration : 0.1;
                uint32_t curDir = outMask & 15u;
                double cix = ((curDir & 8u) ? 1.0 : 0.0) - ((curDir & 4u) ? 1.0 : 0.0);
                double ciy = ((curDir & 2u) ? 1.0 : 0.0) - ((curDir & 1u) ? 1.0 : 0.0);
                if (cix != 0.0 && ciy != 0.0) { cix *= 0.7071067811865475; ciy *= 0.7071067811865475; }

                Vec2 bPos1(bD.pos.x + bD.speed.x, bD.pos.y + bD.speed.y);
                Vec2 pPos1Cur(meD.pos.x + meD.speed.x + cix * pAcc, meD.pos.y + meD.speed.y + ciy * pAcc);
                double gap1Cur = std::hypot(bPos1.x - pPos1Cur.x, bPos1.y - pPos1Cur.y) - minDist;

                if (gap1Cur <= 0.30) {
                    static const uint32_t kDirs[8] = { 1u, 2u, 4u, 8u, 5u, 9u, 6u, 10u };
                    uint32_t bestCushionDir = curDir;
                    double bestDot = 1e9;
                    for (uint32_t dMask : kDirs) {
                        double ix = ((dMask & 8u) ? 1.0 : 0.0) - ((dMask & 4u) ? 1.0 : 0.0);
                        double iy = ((dMask & 2u) ? 1.0 : 0.0) - ((dMask & 1u) ? 1.0 : 0.0);
                        if (ix != 0.0 && iy != 0.0) { ix *= 0.7071067811865475; iy *= 0.7071067811865475; }

                        Vec2 pPos1Test(meD.pos.x + meD.speed.x + ix * pAcc, meD.pos.y + meD.speed.y + iy * pAcc);
                        double gap1Test = std::hypot(bPos1.x - pPos1Test.x, bPos1.y - pPos1Test.y) - minDist;

                        if (gap1Test <= 0.55) {
                            double dotN = ix * nx + iy * ny;
                            if (dotN < bestDot) {
                                bestDot = dotN;
                                bestCushionDir = dMask;
                            }
                        }
                    }
                    outMask = (outMask & ~15u) | bestCushionDir;
                }
            }
        }
    }

    if (isNewTick) {
        if (fakeShotCooldownRemainingTicks > 0) {
            fakeShotCooldownRemainingTicks--;
        }
        if (kickCooldownWaitTicks > 0) {
            kickCooldownWaitTicks--;
        }
    }

    if (realShotEnabled && holdingRealShotKey) {
        isRealShooting = true;
        if (isNewTick) {
            realShotCadenceCounter++;
        }

        if (realShotIntervalMs <= 0) {
            if ((realShotCadenceCounter & 1) == 1) {
                outMask |= 16u;
            } else {
                outMask &= ~16u;
            }
        } else {
            int cadenceTicks = std::max(2, static_cast<int>(std::round(std::clamp(realShotIntervalMs, 0, 1000) / 16.6667)));
            if ((realShotCadenceCounter % cadenceTicks) == 1) {
                outMask |= 16u;
            } else {
                outMask &= ~16u;
            }
        }
        return outMask;
    } else {
        isRealShooting = false;
        if (isNewTick) realShotCadenceCounter = 0;
    }

    const World& authW = (realWorld && realWorld->hasMatch && !realWorld->discs.empty()) ? *realWorld : renderWorld;
    const Player* realMe = authW.getPlayer(myPlayerId);

    if (rsHelperEnabled && (rsMacroSlideTriggered || rsMacroSprintTriggered)) {
        if (isNewTick) rsMacroElapsedTicks++;
        std::string currentAv = (realMe && realMe->headlessAvatar.has_value()) ? *realMe->headlessAvatar : "";

        if (rsMacroSlideTriggered) {

            if (currentAv == "👟" || rsMacroElapsedTicks >= 50) {
                rsMacroSlideTriggered = false;
                rsMacroElapsedTicks = 0;
                rsCurrentState = RSSuperpowerState::None;
                rsCooldownRemainingMs = 18000.0f;
                outMask &= ~16u;
                return outMask;
            } else {
                outMask |= 16u;
                return outMask;
            }
        } else if (rsMacroSprintTriggered) {

            if (currentAv == "⚡" || currentAv == "💨" || rsMacroElapsedTicks >= 95) {
                rsMacroSprintTriggered = false;
                rsMacroElapsedTicks = 0;
                rsCurrentState = RSSuperpowerState::None;
                rsCooldownRemainingMs = 18000.0f;
                outMask &= ~16u;
                return outMask;
            } else {
                outMask |= 16u;
                return outMask;
            }
        }
    }

    if (!realMe || realMe->discIndex < 0 || realMe->discIndex >= (int)authW.discs.size() || authW.discs.empty()) {
        return outMask;
    }

    const Disc& rMeDisc = authW.discs[realMe->discIndex];
    const Disc& rBall = authW.discs[0];
    double sumR = rBall.radius + rMeDisc.radius;
    double realDx = rBall.pos.x - rMeDisc.pos.x;
    double realDy = rBall.pos.y - rMeDisc.pos.y;
    double realDist = std::hypot(realDx, realDy);
    double realGap = realDist - sumR;

    if (!isNewTick) {
        if (activeKickPulseTick == simTick) {
            outMask |= 16u;
            return outMask;
        }
        if (forcedReleaseTick == simTick) {
            if ((manualKeyMask & 16u) == 0 || realGap > 4.0) {
                outMask &= ~16u;
            }
            return outMask;
        }

        return outMask;
    }

    if (fakeShotHoldRemainingTicks > 0) {
        if (!fakeShotEnabled || (manualKeyMask & 16u) != 0 || realMe->kickCooldown > 0 || realMe->kickRateCounter < 0) {
            fakeShotHoldRemainingTicks = 0;
            fakeShotJustPulsed = false;
            isFakeShooting = false;
        } else {
            const auto& ppH = authW.stadium ? authW.stadium->playerPhysics : PlayerPhysics{};
            double pAccH     = (ppH.acceleration > 0.001) ? ppH.acceleration : 0.1;
            double pKickAccH = (ppH.kickingAcceleration > 0.001) ? ppH.kickingAcceleration : 0.07;
            double pDampH    = (ppH.damping > 0.5 && ppH.damping < 1.0) ? ppH.damping : 0.96;
            double pKDampH   = (ppH.kickingDamping > 0.5 && ppH.kickingDamping < 1.0) ? ppH.kickingDamping : 0.96;
            double bDampH    = (rBall.damping > 0.5 && rBall.damping < 1.0) ? rBall.damping : 0.99;

            double axH = ((outMask & 8) ? 1.0 : 0.0) - ((outMask & 4) ? 1.0 : 0.0);
            double ayH = ((outMask & 2) ? 1.0 : 0.0) - ((outMask & 1) ? 1.0 : 0.0);
            if (axH != 0.0 && ayH != 0.0) { axH *= 0.7071067811865475; ayH *= 0.7071067811865475; }

            Vec2 pK = rMeDisc.pos, vK = rMeDisc.speed;
            if (axH != 0.0 || ayH != 0.0) { vK.x += axH * pKickAccH; vK.y += ayH * pKickAccH; }
            pK.x += vK.x; pK.y += vK.y;

            Vec2 pN = rMeDisc.pos, vN = rMeDisc.speed;
            if (axH != 0.0 || ayH != 0.0) { vN.x += axH * pAccH; vN.y += ayH * pAccH; }
            pN.x += vN.x; pN.y += vN.y;

            Vec2 bN = rBall.pos, bvN = rBall.speed;
            bN.x += bvN.x; bN.y += bvN.y;
            bvN.x = bDampH * (bvN.x + rBall.gravity.x);
            bvN.y = bDampH * (bvN.y + rBall.gravity.y);
            if (authW.stadium) {
                int tb = 0; double tpc = 1e9;
                resolveBallStaticCollisions(authW, rBall, bN, bvN, tb, tpc);
            }

            double g1K = std::hypot(bN.x - pK.x, bN.y - pK.y) - sumR;
            double g1N = std::hypot(bN.x - pN.x, bN.y - pN.y) - sumR;
            double worstG1 = std::min(g1K, g1N) - pAccH;

            int extMode = std::clamp(fakeShotExtendTicks, 0, 15);
            bool safeToContinue = (extMode >= 10)
                ? true
                : ((extMode >= 6) ? (realGap >= 4.02)
                                  : (realGap >= 4.25 && g1K >= 4.18 && g1N >= 4.14 && worstG1 >= 4.06));

            if (safeToContinue) {
                fakeShotHoldRemainingTicks--;
                isFakeShooting = true;
                activeKickPulseTick = simTick;
                suppressHaloReleaseTicks = 2;
                suppressLocalKickHalo = true;
                outMask |= 16u;
                if (fakeShotHoldRemainingTicks == 0) {
                    fakeShotJustPulsed = true;
                }
                return outMask;
            } else {

                fakeShotHoldRemainingTicks = 0;
                fakeShotJustPulsed = false;
                isFakeShooting = false;
                forcedReleaseTick = simTick;
                outMask &= ~16u;

                if (fastFakeEnabled && holdingFastFakeKey && realDist > 1e-4) {
                    double ux = realDx / realDist, uy = realDy / realDist;
                    static const uint32_t kDirs[8] = { 1u, 2u, 4u, 8u, 5u, 9u, 6u, 10u };
                    uint32_t bestOutDir = 0; double bestS = -1e9;
                    for (uint32_t dM : kDirs) {
                        double ix = ((dM & 8u) ? 1.0 : 0.0) - ((dM & 4u) ? 1.0 : 0.0);
                        double iy = ((dM & 2u) ? 1.0 : 0.0) - ((dM & 1u) ? 1.0 : 0.0);
                        if (ix != 0.0 && iy != 0.0) { ix *= 0.7071067811865475; iy *= 0.7071067811865475; }
                        double s = -(ix * ux + iy * uy);
                        if (s > bestS) { bestS = s; bestOutDir = dM; }
                    }
                    outMask = (outMask & ~15u) | bestOutDir;
                    fastFakeRetreatTicksRemaining = std::max(1, std::clamp(fastFakeSpeedTicks, 1, 10) - 1);
                }
                return outMask;
            }
        }
    }

    if (fakeShotJustPulsed) {
        fakeShotJustPulsed = false;
        isFakeShooting = false;
        forcedReleaseTick = simTick;
        outMask &= ~16u;

        if (fastFakeEnabled && holdingFastFakeKey && realDist > 1e-4) {
            double ux = realDx / realDist;
            double uy = realDy / realDist;
            double tx = -uy;
            double ty = ux;
            double relVx = rMeDisc.speed.x - rBall.speed.x;
            double relVy = rMeDisc.speed.y - rBall.speed.y;
            double tangVel = relVx * tx + relVy * ty;

            static const uint32_t kDirs[8] = { 1u, 2u, 4u, 8u, 5u, 9u, 6u, 10u };
            uint32_t bestOutDir = 0;
            double bestScore = -1e9;
            for (uint32_t dMask : kDirs) {
                double ix = ((dMask & 8u) ? 1.0 : 0.0) - ((dMask & 4u) ? 1.0 : 0.0);
                double iy = ((dMask & 2u) ? 1.0 : 0.0) - ((dMask & 1u) ? 1.0 : 0.0);
                if (ix != 0.0 && iy != 0.0) { ix *= 0.7071067811865475; iy *= 0.7071067811865475; }
                double outProj = -(ix * ux + iy * uy);
                double tangCancel = -(ix * tx + iy * ty) * std::clamp(tangVel, -1.5, 1.5);
                double score = outProj * 4.0 + tangCancel * 1.2;
                if (score > bestScore) {
                    bestScore = score;
                    bestOutDir = dMask;
                }
            }
            outMask = (outMask & ~15u) | bestOutDir;
            fastFakeRetreatTicksRemaining = std::max(1, std::clamp(fastFakeSpeedTicks, 1, 10) - 1);
        }
        return outMask;
    }

    if (fastFakeEnabled && holdingFastFakeKey &&
        (authW.matchState == 0 || authW.matchState == 1) && authW.pauseTickCounter == 0 &&
        realDist > 1e-4) {
        const auto& pp = authW.stadium ? authW.stadium->playerPhysics : PlayerPhysics{};
        double pAccel     = (pp.acceleration > 0.001) ? pp.acceleration : 0.1;
        double pKickAccel = (pp.kickingAcceleration > 0.001) ? pp.kickingAcceleration : 0.07;
        double pDamp      = (pp.damping > 0.5 && pp.damping < 1.0) ? pp.damping : 0.96;
        double pKickDamp  = (pp.kickingDamping > 0.5 && pp.kickingDamping < 1.0) ? pp.kickingDamping : 0.96;
        double bDamp      = (rBall.damping > 0.5 && rBall.damping < 1.0) ? rBall.damping : 0.99;

        double ux = realDx / realDist;
        double uy = realDy / realDist;
        double tx = -uy;
        double ty = ux;
        double relVx = rMeDisc.speed.x - rBall.speed.x;
        double relVy = rMeDisc.speed.y - rBall.speed.y;
        double tangVel = relVx * tx + relVy * ty;

        static const uint32_t kDirs[8] = { 1u, 2u, 4u, 8u, 5u, 9u, 6u, 10u };
        uint32_t bestInDir = 0;
        uint32_t bestOutDir = 0;
        double bestInScore = -1e9;
        double bestOutScore = -1e9;

        for (uint32_t dMask : kDirs) {
            double ix = ((dMask & 8u) ? 1.0 : 0.0) - ((dMask & 4u) ? 1.0 : 0.0);
            double iy = ((dMask & 2u) ? 1.0 : 0.0) - ((dMask & 1u) ? 1.0 : 0.0);
            if (ix != 0.0 && iy != 0.0) { ix *= 0.7071067811865475; iy *= 0.7071067811865475; }

            double inProj = (ix * ux + iy * uy);
            double outProj = -inProj;
            double tangCancel = -(ix * tx + iy * ty) * std::clamp(tangVel, -1.8, 1.8);

            double inScore = inProj * 4.0 + tangCancel * 1.5;
            double outScore = outProj * 4.0 + tangCancel * 1.2;
            if (inScore > bestInScore) { bestInScore = inScore; bestInDir = dMask; }
            if (outScore > bestOutScore) { bestOutScore = outScore; bestOutDir = dMask; }
        }

        double inX = ((bestInDir & 8u) ? 1.0 : 0.0) - ((bestInDir & 4u) ? 1.0 : 0.0);
        double inY = ((bestInDir & 2u) ? 1.0 : 0.0) - ((bestInDir & 1u) ? 1.0 : 0.0);
        if (inX != 0.0 && inY != 0.0) { inX *= 0.7071067811865475; inY *= 0.7071067811865475; }

        auto stepPlayerExact = [](Vec2& pos, Vec2& vel, double ax, double ay, double acc, double damp, const Vec2& grav) {
            if (ax != 0.0 || ay != 0.0) { vel.x += ax * acc; vel.y += ay * acc; }
            pos.x += vel.x;
            pos.y += vel.y;
            vel.x = damp * (vel.x + grav.x);
            vel.y = damp * (vel.y + grav.y);
        };
        auto stepBallExact = [&](Vec2& pos, Vec2& vel) {
            pos.x += vel.x;
            pos.y += vel.y;
            vel.x = bDamp * (vel.x + rBall.gravity.x);
            vel.y = bDamp * (vel.y + rBall.gravity.y);
            if (authW.stadium) {
                int tmpB = 0;
                double tmpPC = 1e9;
                resolveBallStaticCollisions(authW, rBall, pos, vel, tmpB, tmpPC);
            }
        };

        Vec2 nP1 = rMeDisc.pos, nPV1 = rMeDisc.speed;
        Vec2 nB1 = rBall.pos, nBV1 = rBall.speed;
        stepPlayerExact(nP1, nPV1, inX, inY, pAccel, pDamp, rMeDisc.gravity);
        stepBallExact(nB1, nBV1);
        double gap1Normal = std::hypot(nB1.x - nP1.x, nB1.y - nP1.y) - sumR;

        Vec2 kP = rMeDisc.pos, kPV = rMeDisc.speed;
        Vec2 kB = rBall.pos, kBV = rBall.speed;
        double gap1Kick = 999.0, gap2Kick = 999.0;
        int ghostTick = -1;
        int maxLook = fakeShotExtendEnabled ? 7 : 5;
        for (int t = 1; t <= maxLook; ++t) {
            stepPlayerExact(kP, kPV, inX, inY, pKickAccel, pKickDamp, rMeDisc.gravity);
            stepBallExact(kB, kBV);
            double g = std::hypot(kB.x - kP.x, kB.y - kP.y) - sumR;
            if (t == 1) gap1Kick = g;
            if (t == 2) gap2Kick = g;
            if (t >= 2 && g < 3.98 && ghostTick < 0) {
                ghostTick = t;
                break;
            }
        }

        int speedSetting = std::clamp(fastFakeSpeedTicks, 1, 10);
        double outerTargetGap = 4.65 + (speedSetting - 1) * 0.35;

        if (fastFakeRetreatTicksRemaining > 0) {
            if (isNewTick) fastFakeRetreatTicksRemaining--;

            if (realGap < outerTargetGap || realGap < 4.50) {
                outMask = (outMask & ~15u) | bestOutDir;
                outMask &= ~16u;
                return outMask;
            } else {
                fastFakeRetreatTicksRemaining = 0;
            }
        }

        if (realGap < 4.22 || gap1Kick < 4.12 || gap1Normal < 4.08) {
            outMask = (outMask & ~15u) | bestOutDir;
            outMask &= ~16u;
            return outMask;
        }

        outMask = (outMask & ~15u) | bestInDir;

        bool canPulseNow = (realMe->kickCooldown <= 0 && realMe->kickRateCounter >= 0 &&
                            (lastSentMask & 16u) == 0 && !realMe->isKicking);
        bool hitBoundary = (ghostTick == 2) ||
                           (ghostTick == 3 && gap2Kick < 4.75) ||
                           (ghostTick == 4 && realGap <= 5.65 && gap2Kick < 4.85) ||
                           (fakeShotExtendEnabled && ghostTick >= 3 && ghostTick <= 7);

        if (canPulseNow && hitBoundary) {
            isFakeShooting = true;
            activeKickPulseTick = simTick;
            suppressHaloReleaseTicks = 2;
            suppressLocalKickHalo = true;
            int extraHoldTicks = 0;
            if (fakeShotExtendEnabled) {
                if (fakeShotExtendTicks > 0) extraHoldTicks = std::clamp(fakeShotExtendTicks, 1, 15);
                else if (ghostTick >= 3) extraHoldTicks = std::clamp(ghostTick - 2, 0, 5);
            }
            if (extraHoldTicks > 0) {
                fakeShotHoldRemainingTicks = extraHoldTicks;
                fakeShotJustPulsed = false;
            } else {
                fakeShotHoldRemainingTicks = 0;
                fakeShotJustPulsed = true;
            }
            fastFakeRetreatTicksRemaining = speedSetting;
            outMask |= 16u;
            return outMask;
        }

        outMask &= ~16u;
        return outMask;
    }

    if (kickJustPulsed) {
        kickJustPulsed = false;
        forcedReleaseTick = simTick;
        outMask &= ~16u;
        return outMask;
    }

    if (autoGoalHoldTicks > 0 || autoSaveHoldTicks > 0) {
        if (realMe->kickCooldown > 0) {
            autoGoalHoldTicks = 0;
            autoSaveHoldTicks = 0;
            forcedReleaseTick = simTick;
            outMask &= ~16u;
            return outMask;
        }
        if (autoGoalHoldTicks > 0) autoGoalHoldTicks--;
        if (autoSaveHoldTicks > 0) autoSaveHoldTicks--;
        activeKickPulseTick = simTick;
        outMask |= 16u;
        if (autoGoalHoldTicks == 0 && autoSaveHoldTicks == 0) {
            kickJustPulsed = true;
        }
        return outMask;
    }

    if (autoGoalEnabled && (authW.matchState == 0 || authW.matchState == 1) && authW.pauseTickCounter == 0 &&
        realMe->team != 0 && (!rsHelperEnabled || (!rsMacroSlideTriggered && !rsMacroSprintTriggered))) {
        bool kickCooldownReady = (realMe->kickCooldown <= 0 && realMe->kickRateCounter >= 0 && kickCooldownWaitTicks <= 0);
        double relSpeed = std::hypot(rBall.speed.x - rMeDisc.speed.x, rBall.speed.y - rMeDisc.speed.y);
        if (kickCooldownReady && realGap <= 4.0 + relSpeed + 4.0) {
            double reqAcc = static_cast<double>(std::clamp(autoGoalAccuracy, 30, 100));
            int delayTicks = std::max(2, (int)authW.kickMin);

            if (realGap < 3.98) {
                double acc0 = evaluateAutoGoalAccuracy(authW, myPlayerId, realMe->team, rMeDisc.pos, rBall.pos, rBall.speed, 0);
                if (acc0 >= reqAcc) {
                    if ((lastSentMask & 16u) != 0 && !realMe->isKicking) {
                        forcedReleaseTick = simTick;
                        outMask &= ~16u;
                        return outMask;
                    }
                    outMask |= 16u;
                    activeKickPulseTick = simTick;
                    suppressLocalKickHalo = false;
                    kickJustPulsed = true;
                    kickCooldownWaitTicks = delayTicks;
                    fakeShotCooldownRemainingTicks = std::max(fakeShotCooldownRemainingTicks, 6);
                    return outMask;
                }
            } else {

                const auto& pp = authW.stadium ? authW.stadium->playerPhysics : PlayerPhysics{};
                double pKickAccel = (pp.kickingAcceleration > 0.01) ? pp.kickingAcceleration : 0.07;
                double pKickDamp  = (pp.kickingDamping > 0.5 && pp.kickingDamping < 1.0) ? pp.kickingDamping : 0.96;
                double bDamp      = (rBall.damping > 0.5 && rBall.damping < 1.0) ? rBall.damping : 0.99;

                double ax = ((outMask & 8) ? 1.0 : 0.0) - ((outMask & 4) ? 1.0 : 0.0);
                double ay = ((outMask & 2) ? 1.0 : 0.0) - ((outMask & 1) ? 1.0 : 0.0);
                if (ax != 0.0 && ay != 0.0) { ax *= 0.7071067811865475; ay *= 0.7071067811865475; }

                Vec2 pPos1 = rMeDisc.pos;
                Vec2 pVel1 = rMeDisc.speed;
                if (ax != 0.0 || ay != 0.0) {
                    pVel1.x += ax * pKickAccel;
                    pVel1.y += ay * pKickAccel;
                }
                pPos1.x += pVel1.x;
                pPos1.y += pVel1.y;
                pVel1.x = pKickDamp * (pVel1.x + rMeDisc.gravity.x);
                pVel1.y = pKickDamp * (pVel1.y + rMeDisc.gravity.y);

                Vec2 bPos1 = rBall.pos;
                Vec2 bVel1 = rBall.speed;
                bPos1.x += bVel1.x;
                bPos1.y += bVel1.y;
                bVel1.x = bDamp * (bVel1.x + rBall.gravity.x);
                bVel1.y = bDamp * (bVel1.y + rBall.gravity.y);

                if ((rMeDisc.cMask & rBall.cGroup) != 0 && (rMeDisc.cGroup & rBall.cMask) != 0) {
                    double ux = bPos1.x - pPos1.x;
                    double vy = bPos1.y - pPos1.y;
                    double distSq = ux * ux + vy * vy;
                    double minDist = rBall.radius + rMeDisc.radius;
                    if (distSq > 0.0 && distSq <= minDist * minDist && (rBall.invMass + rMeDisc.invMass) > 0.0) {
                        double dist = std::sqrt(distSq);
                        ux /= dist;
                        vy /= dist;
                        double massRatio = rBall.invMass / (rBall.invMass + rMeDisc.invMass);
                        double overlap = minDist - dist;
                        double moveA = overlap * massRatio;
                        bPos1.x += ux * moveA;
                        bPos1.y += vy * moveA;
                        double moveB = overlap - moveA;
                        pPos1.x -= ux * moveB;
                        pPos1.y -= vy * moveB;

                        double relVel = ux * (bVel1.x - pVel1.x) + vy * (bVel1.y - pVel1.y);
                        if (relVel < 0.0) {
                            relVel *= rBall.bCoef * rMeDisc.bCoef + 1.0;
                            double impA = massRatio * relVel;
                            bVel1.x -= ux * impA;
                            bVel1.y -= vy * impA;
                            double impB = relVel - impA;
                            pVel1.x += ux * impB;
                            pVel1.y += vy * impB;
                        }
                    }
                }

                int tmpBounces = 0;
                double tmpPostClear = 1e9;
                if (authW.stadium) {
                    resolveBallStaticCollisions(authW, rBall, bPos1, bVel1, tmpBounces, tmpPostClear);
                }

                double gap1 = std::hypot(bPos1.x - pPos1.x, bPos1.y - pPos1.y) - sumR;
                if (gap1 < 3.98) {
                    double acc1 = evaluateAutoGoalAccuracy(authW, myPlayerId, realMe->team, pPos1, bPos1, bVel1, 1);
                    if (acc1 >= reqAcc) {
                        if ((lastSentMask & 16u) != 0 && !realMe->isKicking) {
                            forcedReleaseTick = simTick;
                            outMask &= ~16u;
                            return outMask;
                        }
                        outMask |= 16u;
                        activeKickPulseTick = simTick;
                        suppressLocalKickHalo = false;
                        autoGoalHoldTicks = 1;
                        kickCooldownWaitTicks = std::max(3, delayTicks);
                        fakeShotCooldownRemainingTicks = std::max(fakeShotCooldownRemainingTicks, 8);
                        return outMask;
                    }
                }
            }
        }
    }

    if (autoSaveEnabled && (authW.matchState == 0 || authW.matchState == 1) && authW.pauseTickCounter == 0 &&
        realMe->team != 0 && authW.stadium && (!rsHelperEnabled || (!rsMacroSlideTriggered && !rsMacroSprintTriggered))) {
        const Goal* ownGoal = nullptr;
        for (const auto& g : authW.stadium->goals) {
            if (g.team == realMe->team) {
                ownGoal = &g;
                break;
            }
        }

        if (ownGoal) {
            double goalX = 0.5 * (ownGoal->p0.x + ownGoal->p1.x);
            double goalMinY = std::min(ownGoal->p0.y, ownGoal->p1.y);
            double goalMaxY = std::max(ownGoal->p0.y, ownGoal->p1.y);
            double goalCenterY = 0.5 * (goalMinY + goalMaxY);
            double goalHalfW = std::max(20.0, 0.5 * (goalMaxY - goalMinY));
            double inwardSign = (goalX > 0.0) ? -1.0 : 1.0;

            double bSpeed = std::hypot(rBall.speed.x, rBall.speed.y);
            double bTowardGoal = rBall.speed.x * (-inwardSign);

            if (bSpeed >= 1.35 && bTowardGoal > 0.15) {
                const auto& pp = authW.stadium->playerPhysics;
                double pAccel     = (pp.acceleration > 0.001) ? pp.acceleration : 0.1;
                double pKickAccel = (pp.kickingAcceleration > 0.001) ? pp.kickingAcceleration : 0.07;
                double pDamp      = (pp.damping > 0.5 && pp.damping < 1.0) ? pp.damping : 0.96;
                double pKickDamp  = (pp.kickingDamping > 0.5 && pp.kickingDamping < 1.0) ? pp.kickingDamping : 0.96;
                double kickStr    = (pp.kickStrength > 0.5) ? pp.kickStrength : 5.0;
                double bDamp      = (rBall.damping > 0.5 && rBall.damping < 1.0) ? rBall.damping : 0.99;
                double bInvM      = (rBall.invMass > 0.01) ? rBall.invMass : 1.0;
                double myR        = (rMeDisc.radius > 1.0) ? rMeDisc.radius : 14.0;
                double bR         = (rBall.radius > 1.0) ? rBall.radius : 7.6;

                double postOffset = (std::abs(goalX) > 1000.0) ? 11.45 : 8.0;
                double postX = goalX + inwardSign * postOffset;
                double pitchHalfX = std::max(200.0, std::abs(postX));
                double boxDepth = pitchHalfX * 0.27;

                bool rawBallScores = false;
                bool passiveBallScores = false;
                double goalCrossY = goalCenterY;
                int tGoalCross = -1;
                bool crossesMyPlane = false;
                double myPlaneCrossY = goalCenterY;
                int tMyPlaneCross = -1;

                {

                    Vec2 sp = rBall.pos, sv = rBall.speed;
                    int tmpB = 0; double tmpPC = 1e9;
                    for (int t = 1; t <= 105; ++t) {
                        Vec2 prev = sp;
                        sp.x += sv.x; sp.y += sv.y;
                        sv.x = bDamp * (sv.x + rBall.gravity.x);
                        sv.y = bDamp * (sv.y + rBall.gravity.y);
                        resolveBallStaticCollisions(authW, rBall, sp, sv, tmpB, tmpPC);

                        if (!crossesMyPlane) {
                            double dPrev = (prev.x - rMeDisc.pos.x) * inwardSign;
                            double dCur  = (sp.x - rMeDisc.pos.x) * inwardSign;
                            if (dPrev >= 0.0 && dCur <= 0.0) {
                                double frac = (dPrev - dCur > 1e-6) ? std::clamp(dPrev / (dPrev - dCur), 0.0, 1.0) : 0.0;
                                myPlaneCrossY = prev.y + (sp.y - prev.y) * frac;
                                tMyPlaneCross = t;
                                crossesMyPlane = true;
                            }
                        }
                        if (ownGoal->check(sp, prev)) {
                            double dPrev = (prev.x - goalX) * inwardSign;
                            double dCur  = (sp.x - goalX) * inwardSign;
                            double frac = (dPrev - dCur > 1e-6) ? std::clamp(dPrev / (dPrev - dCur), 0.0, 1.0) : 0.0;
                            goalCrossY = prev.y + (sp.y - prev.y) * frac;
                            tGoalCross = t;
                            rawBallScores = true;
                            break;
                        }
                        if (std::hypot(sv.x, sv.y) < 0.08) break;
                    }
                }

                if (rawBallScores) {

                    double axM = ((outMask & 8) ? 1.0 : 0.0) - ((outMask & 4) ? 1.0 : 0.0);
                    double ayM = ((outMask & 2) ? 1.0 : 0.0) - ((outMask & 1) ? 1.0 : 0.0);
                    if (axM != 0.0 && ayM != 0.0) { axM *= 0.7071067811865475; ayM *= 0.7071067811865475; }

                    Vec2 bp = rBall.pos, bv = rBall.speed;
                    Vec2 mp = rMeDisc.pos, mv = rMeDisc.speed;
                    int tmpB = 0; double tmpPC = 1e9;
                    int simLimit = std::min(105, tGoalCross + 25);
                    for (int t = 1; t <= simLimit; ++t) {
                        Vec2 prevB = bp;
                        if (axM != 0.0 || ayM != 0.0) { mv.x += axM * pAccel; mv.y += ayM * pAccel; }
                        mp.x += mv.x; mp.y += mv.y;
                        mv.x = pDamp * (mv.x + rMeDisc.gravity.x);
                        mv.y = pDamp * (mv.y + rMeDisc.gravity.y);

                        bp.x += bv.x; bp.y += bv.y;
                        bv.x = bDamp * (bv.x + rBall.gravity.x);
                        bv.y = bDamp * (bv.y + rBall.gravity.y);

                        if ((rMeDisc.cMask & rBall.cGroup) != 0 && (rMeDisc.cGroup & rBall.cMask) != 0) {
                            double ux = bp.x - mp.x, vy = bp.y - mp.y;
                            double dSq = ux * ux + vy * vy;
                            if (dSq > 0.0 && dSq <= sumR * sumR && (rBall.invMass + rMeDisc.invMass) > 0.0) {
                                double d = std::sqrt(dSq);
                                ux /= d; vy /= d;
                                double mr = rBall.invMass / (rBall.invMass + rMeDisc.invMass);
                                double ov = sumR - d;
                                bp.x += ux * ov * mr; bp.y += vy * ov * mr;
                                mp.x -= ux * ov * (1.0 - mr); mp.y -= vy * ov * (1.0 - mr);
                                double rv = ux * (bv.x - mv.x) + vy * (bv.y - mv.y);
                                if (rv < 0.0) {
                                    rv *= rBall.bCoef * rMeDisc.bCoef + 1.0;
                                    bv.x -= ux * mr * rv; bv.y -= vy * mr * rv;
                                    mv.x += ux * (1.0 - mr) * rv; mv.y += vy * (1.0 - mr) * rv;
                                }
                            }
                        }
                        resolveBallStaticCollisions(authW, rBall, bp, bv, tmpB, tmpPC);
                        if (ownGoal->check(bp, prevB)) {
                            passiveBallScores = true;
                            break;
                        }
                        if (std::hypot(bv.x, bv.y) < 0.08) break;
                    }

                    bool oppReboundThreat = false;
                    for (const auto& op : authW.players) {
                        if (op.team == 0 || op.team == realMe->team) continue;
                        if (op.discIndex < 0 || op.discIndex >= (int)authW.discs.size()) continue;
                        const Disc& oD = authW.discs[op.discIndex];
                        if (std::hypot(rBall.pos.x - oD.pos.x, rBall.pos.y - oD.pos.y) < 95.0) {
                            oppReboundThreat = true;
                            break;
                        }
                    }

                    bool needsSaveKick = passiveBallScores || (bSpeed >= 4.0 && oppReboundThreat);

                    auto isSafeSaveKick = [&](const Vec2& pPos, const Vec2& bPos, const Vec2& bVel) -> bool {
                        double dx = bPos.x - pPos.x, dy = bPos.y - pPos.y;
                        double dist = std::hypot(dx, dy);
                        if (dist < 1e-6) return false;
                        double nx = dx / dist, ny = dy / dist;

                        Vec2 sp = bPos;
                        Vec2 sv(bVel.x + nx * kickStr * bInvM, bVel.y + ny * kickStr * bInvM);
                        int tmpB = 0; double tmpPC = 1e9;
                        for (int t = 1; t <= 45; ++t) {
                            Vec2 prev = sp;
                            sp.x += sv.x; sp.y += sv.y;
                            sv.x = bDamp * (sv.x + rBall.gravity.x);
                            sv.y = bDamp * (sv.y + rBall.gravity.y);
                            resolveBallStaticCollisions(authW, rBall, sp, sv, tmpB, tmpPC);
                            if (ownGoal->check(sp, prev)) {
                                return false;
                            }
                            if (std::hypot(sv.x, sv.y) < 0.08) break;
                        }

                        return true;
                    };

                    bool kickCooldownReady = (realMe->kickCooldown <= 0 && realMe->kickRateCounter >= 0 && kickCooldownWaitTicks <= 0);
                    double relSpeed = std::hypot(rBall.speed.x - rMeDisc.speed.x, rBall.speed.y - rMeDisc.speed.y);
                    if (needsSaveKick && kickCooldownReady && realGap <= 4.0 + relSpeed + 4.0) {
                        int delayTicks = std::max(2, (int)authW.kickMin);
                        if (realGap < 3.98) {
                            if (isSafeSaveKick(rMeDisc.pos, rBall.pos, rBall.speed)) {
                                if ((lastSentMask & 16u) != 0 && !realMe->isKicking) {
                                    forcedReleaseTick = simTick;
                                    outMask &= ~16u;
                                    return outMask;
                                }
                                outMask |= 16u;
                                activeKickPulseTick = simTick;
                                suppressLocalKickHalo = false;
                                kickJustPulsed = true;
                                kickCooldownWaitTicks = delayTicks;
                                fakeShotCooldownRemainingTicks = std::max(fakeShotCooldownRemainingTicks, 6);
                                return outMask;
                            }
                        } else {

                            Vec2 pPos1 = rMeDisc.pos, pVel1 = rMeDisc.speed;
                            if (axM != 0.0 || ayM != 0.0) { pVel1.x += axM * pKickAccel; pVel1.y += ayM * pKickAccel; }
                            pPos1.x += pVel1.x; pPos1.y += pVel1.y;
                            pVel1.x = pKickDamp * (pVel1.x + rMeDisc.gravity.x);
                            pVel1.y = pKickDamp * (pVel1.y + rMeDisc.gravity.y);

                            Vec2 bPos1 = rBall.pos, bVel1 = rBall.speed;
                            bPos1.x += bVel1.x; bPos1.y += bVel1.y;
                            bVel1.x = bDamp * (bVel1.x + rBall.gravity.x);
                            bVel1.y = bDamp * (bVel1.y + rBall.gravity.y);
                            int tmpB = 0; double tmpPC = 1e9;
                            resolveBallStaticCollisions(authW, rBall, bPos1, bVel1, tmpB, tmpPC);

                            double gap1 = std::hypot(bPos1.x - pPos1.x, bPos1.y - pPos1.y) - sumR;
                            if (gap1 < 3.98 && isSafeSaveKick(pPos1, bPos1, bVel1)) {
                                if ((lastSentMask & 16u) != 0 && !realMe->isKicking) {
                                    forcedReleaseTick = simTick;
                                    outMask &= ~16u;
                                    return outMask;
                                }
                                outMask |= 16u;
                                activeKickPulseTick = simTick;
                                suppressLocalKickHalo = false;
                                autoSaveHoldTicks = 1;
                                kickCooldownWaitTicks = std::max(3, delayTicks);
                                fakeShotCooldownRemainingTicks = std::max(fakeShotCooldownRemainingTicks, 8);
                                return outMask;
                            }
                        }
                    }

                    double myDepthFromGoal = (rMeDisc.pos.x - postX) * inwardSign;
                    bool ballInFrontOfMe = ((rBall.pos.x - rMeDisc.pos.x) * inwardSign > -8.0);
                    if (passiveBallScores && bSpeed >= 2.2 && tGoalCross >= 3 && tGoalCross <= 55 &&
                        myDepthFromGoal > -12.0 && myDepthFromGoal < boxDepth * 1.15 && ballInFrontOfMe) {

                        double saveY = std::clamp(crossesMyPlane ? myPlaneCrossY : goalCrossY,
                                                  goalMinY + myR + 1.0, goalMaxY - myR - 1.0);
                        int tAvail = (crossesMyPlane && tMyPlaneCross > 0) ? tMyPlaneCross : tGoalCross;

                        double myDistToSave = std::hypot(rMeDisc.pos.x - (crossesMyPlane ? rMeDisc.pos.x : postX),
                                                         rMeDisc.pos.y - saveY);
                        bool isPrimarySaver = true;
                        for (const auto& tm : authW.players) {
                            if (tm.team != realMe->team || tm.id == myPlayerId) continue;
                            if (tm.discIndex < 0 || tm.discIndex >= (int)authW.discs.size()) continue;
                            const Disc& tmD = authW.discs[tm.discIndex];
                            double tmDepth = (tmD.pos.x - postX) * inwardSign;
                            if (tmDepth < myDepthFromGoal - 18.0 &&
                                std::hypot(tmD.pos.x - postX, tmD.pos.y - goalCrossY) + 15.0 < myDistToSave) {
                                isPrimarySaver = false;
                                break;
                            }
                        }

                        if (isPrimarySaver) {

                            double yDiff = saveY - rMeDisc.pos.y;
                            double reachMargin = myR + bR + 3.8;
                            double neededDist = std::max(0.0, std::abs(yDiff) - reachMargin);
                            int dirY = (yDiff > 0.0) ? 1 : -1;
                            double simY = 0.0, simV = rMeDisc.speed.y * dirY;
                            int tReach = 999;
                            if (neededDist <= 0.0) {
                                tReach = 0;
                            } else {
                                for (int t = 1; t <= tAvail + 3; ++t) {
                                    simV += pAccel;
                                    simY += simV;
                                    simV *= pDamp;
                                    if (simY >= neededDist) { tReach = t; break; }
                                }
                            }

                            if (tReach <= tAvail + 2) {

                                double brakeY = rMeDisc.pos.y;
                                double v = rMeDisc.speed.y;
                                int bDir = (v > 0.0) ? -1 : ((v < 0.0) ? 1 : 0);
                                if (bDir != 0) {
                                    for (int s = 0; s < 80; ++s) {
                                        double nv = v + bDir * pAccel;
                                        if ((v > 0.0 && nv <= 0.0) || (v < 0.0 && nv >= 0.0)) break;
                                        v = nv; brakeY += v; v *= pDamp;
                                    }
                                }
                                int needY = 0;
                                if (std::abs(saveY - rMeDisc.pos.y) > 1.8 || std::abs(rMeDisc.speed.y) > pAccel * 1.4) {
                                    if (rMeDisc.speed.y > 0.05) {
                                        if (saveY > brakeY + 0.8) needY = 1;
                                        else if (saveY < brakeY - 0.8) needY = -1;
                                    } else if (rMeDisc.speed.y < -0.05) {
                                        if (saveY < brakeY - 0.8) needY = -1;
                                        else if (saveY > brakeY + 0.8) needY = 1;
                                    } else {
                                        if (saveY - rMeDisc.pos.y > 1.8) needY = 1;
                                        else if (saveY - rMeDisc.pos.y < -1.8) needY = -1;
                                    }
                                }

                                outMask &= ~3u;
                                if (needY < 0) outMask |= 1u;
                                else if (needY > 0) outMask |= 2u;
                                if (std::abs(saveY - rMeDisc.pos.y) > 7.5 && tReach >= tAvail - 6) {
                                    outMask &= ~12u;
                                }
                                return outMask;
                            }
                        }
                    }
                }
            }
        }
    }

    if (fakeShotCooldownRemainingTicks > 0) {
        isFakeShooting = false;

        if ((manualKeyMask & 16u) == 0 && realGap <= 25.0) {
            outMask &= ~16u;
        }
        if ((manualKeyMask & 16u) != 0) {
            return outMask;
        }
        return outMask;
    }

    if (realGap <= 4.0) {
        isFakeShooting = false;
        rsManualKickTicks = 0;
        if (rsCurrentState != RSSuperpowerState::None) {
            rsCurrentState = RSSuperpowerState::None;
            rsChargeProgress = 0.0f;
        }
        if ((manualKeyMask & 16) != 0) {

            int cooldownTicks = std::max(2, static_cast<int>(std::round(std::clamp(fakeShotIntervalMs, 50, 1000) / 16.6667)));
            fakeShotCooldownRemainingTicks = cooldownTicks;
            return outMask;
        }
        return outMask;
    }

    if (rsHelperEnabled && (rsRoomDetected || rsManualKickTicks > 0 || (manualKeyMask & 16u) != 0)) {
        if ((manualKeyMask & 16u) != 0) {
            rsManualKickTicks++;
            if (rsCurrentState == RSSuperpowerState::None) {
                rsCurrentState = RSSuperpowerState::Charging;
            }
        } else if (rsManualKickTicks > 0) {
            if (rsCurrentState == RSSuperpowerState::SlideReady || rsCurrentState == RSSuperpowerState::SprintReady) {
                rsCooldownRemainingMs = 18000.0f;
            }
            rsManualKickTicks = 0;
            rsCurrentState = RSSuperpowerState::None;
        }
    }

    if (!fakeShotEnabled) {
        return outMask;
    }

    if ((authW.matchState != 0 && authW.matchState != 1) || authW.pauseTickCounter > 0) {
        return outMask;
    }
    if ((manualKeyMask & 16u) != 0 || (lastSentMask & 16u) != 0 || realMe->isKicking) {
        return outMask;
    }
    if (realMe->kickCooldown > 0 || realMe->kickRateCounter < 0) {
        return outMask;
    }

    double relSpeedMag = std::hypot(rBall.speed.x - rMeDisc.speed.x, rBall.speed.y - rMeDisc.speed.y);
    double maxEvalGap = std::clamp(18.0 + relSpeedMag * 4.5, 18.0, 65.0);
    if (realGap > maxEvalGap) {
        return outMask;
    }

    const auto& pp = authW.stadium ? authW.stadium->playerPhysics : PlayerPhysics{};
    double pAccel     = (pp.acceleration > 0.001) ? pp.acceleration : 0.1;
    double pKickAccel = (pp.kickingAcceleration > 0.001) ? pp.kickingAcceleration : 0.07;
    double pDamp      = (pp.damping > 0.5 && pp.damping < 1.0) ? pp.damping : 0.96;
    double pKickDamp  = (pp.kickingDamping > 0.5 && pp.kickingDamping < 1.0) ? pp.kickingDamping : 0.96;
    double bDamp      = (rBall.damping > 0.5 && rBall.damping < 1.0) ? rBall.damping : 0.99;

    double ax = ((outMask & 8) ? 1.0 : 0.0) - ((outMask & 4) ? 1.0 : 0.0);
    double ay = ((outMask & 2) ? 1.0 : 0.0) - ((outMask & 1) ? 1.0 : 0.0);
    if (ax != 0.0 && ay != 0.0) { ax *= 0.7071067811865475; ay *= 0.7071067811865475; }

    bool otherPlayerNearBall = false;
    for (const auto& op : authW.players) {
        if (op.id == myPlayerId || op.discIndex < 0 || op.discIndex >= (int)authW.discs.size()) continue;
        const Disc& oD = authW.discs[op.discIndex];
        double oGap = std::hypot(rBall.pos.x - oD.pos.x, rBall.pos.y - oD.pos.y) - (rBall.radius + oD.radius);
        if (oGap < 5.5) {
            otherPlayerNearBall = true;
            break;
        }
    }

    double minGap0 = otherPlayerNearBall ? 5.15 : 4.25;
    if (realGap < minGap0) {
        return outMask;
    }

    auto stepPlayerExact = [](Vec2& pos, Vec2& vel, double inX, double inY, double acc, double damp, const Vec2& grav) {
        if (inX != 0.0 || inY != 0.0) {
            vel.x += inX * acc;
            vel.y += inY * acc;
        }
        pos.x += vel.x;
        pos.y += vel.y;
        vel.x = damp * (vel.x + grav.x);
        vel.y = damp * (vel.y + grav.y);
    };

    auto stepBallExact = [&](Vec2& pos, Vec2& vel) {
        pos.x += vel.x;
        pos.y += vel.y;
        vel.x = bDamp * (vel.x + rBall.gravity.x);
        vel.y = bDamp * (vel.y + rBall.gravity.y);
        if (authW.stadium) {
            int tmpB = 0;
            double tmpPC = 1e9;
            resolveBallStaticCollisions(authW, rBall, pos, vel, tmpB, tmpPC);
        }
    };

    Vec2 normP1 = rMeDisc.pos;
    Vec2 normPV1 = rMeDisc.speed;
    stepPlayerExact(normP1, normPV1, ax, ay, pAccel, pDamp, rMeDisc.gravity);
    Vec2 bPos1Check = rBall.pos;
    Vec2 bVel1Check = rBall.speed;
    stepBallExact(bPos1Check, bVel1Check);
    double gap1Normal = std::hypot(bPos1Check.x - normP1.x, bPos1Check.y - normP1.y) - sumR;

    Vec2 curP = rMeDisc.pos;
    Vec2 curPV = rMeDisc.speed;
    Vec2 curB = rBall.pos;
    Vec2 curBV = rBall.speed;

    double gap1Kick = 999.0;
    double gap2Kick = 999.0;
    double gap3Kick = 999.0;
    int firstGhostKickTick = -1;

    for (int t = 1; t <= 8; ++t) {
        stepPlayerExact(curP, curPV, ax, ay, pKickAccel, pKickDamp, rMeDisc.gravity);
        stepBallExact(curB, curBV);

        double g = std::hypot(curB.x - curP.x, curB.y - curP.y) - sumR;
        if (t == 1) gap1Kick = g;
        if (t == 2) gap2Kick = g;
        if (t == 3) gap3Kick = g;
        if (t >= 2 && g < 3.98 && firstGhostKickTick < 0) {
            firstGhostKickTick = t;
            break;
        }
    }

    double minGap1Kick = otherPlayerNearBall ? 4.68 : 4.18;
    double minGap1Normal = otherPlayerNearBall ? 4.58 : 4.14;
    double worstCaseGap1 = std::min(gap1Kick, gap1Normal) - pAccel;
    if (gap1Kick < minGap1Kick || gap1Normal < minGap1Normal || worstCaseGap1 < 4.06) {
        return outMask;
    }

    bool optimalZeroHaloWindow =
        (firstGhostKickTick == 2) ||
        (firstGhostKickTick == 3 && gap2Kick < (minGap1Kick + 0.45)) ||
        (firstGhostKickTick >= 3 && firstGhostKickTick <= 5 && realGap <= 5.80 && gap2Kick < 4.85) ||
        (fakeShotExtendEnabled && firstGhostKickTick >= 3 && firstGhostKickTick <= 7);

    if (optimalZeroHaloWindow) {

        isFakeShooting = true;
        activeKickPulseTick = simTick;
        suppressHaloReleaseTicks = 2;
        suppressLocalKickHalo = true;

        int extraHoldTicks = 0;
        if (fakeShotExtendEnabled) {
            if (fakeShotExtendTicks > 0) {
                extraHoldTicks = std::clamp(fakeShotExtendTicks, 1, 15);
            } else if (firstGhostKickTick >= 3) {

                extraHoldTicks = std::clamp(firstGhostKickTick - 2, 0, 5);
            }
        }

        if (extraHoldTicks > 0) {
            fakeShotHoldRemainingTicks = extraHoldTicks;
            fakeShotJustPulsed = false;
        } else {
            fakeShotHoldRemainingTicks = 0;
            fakeShotJustPulsed = true;
        }

        int cooldownTicks = std::max(2, static_cast<int>(std::round(std::clamp(fakeShotIntervalMs, 50, 1000) / 16.6667)));
        fakeShotCooldownRemainingTicks = cooldownTicks;
        outMask |= 16u;
        return outMask;
    } else {
        isFakeShooting = false;

        if ((manualKeyMask & 16u) == 0) {
            outMask &= ~16u;
        }
    }

    return outMask;
}

uint32_t TacticalAssistant::computePenaltyGkInput(const World& authW, uint32_t gkPlayerId) const {
    if (!authW.hasMatch || !authW.stadium || authW.discs.empty()) return 0u;
    const Player* gkPl = authW.getPlayer(gkPlayerId);
    if (!gkPl || gkPl->team == 0 || gkPl->discIndex < 0 || gkPl->discIndex >= (int)authW.discs.size()) return 0u;

    const Disc& gkDisc = authW.discs[gkPl->discIndex];
    const Disc& ball = authW.discs[0];

    const Goal* ownGoal = nullptr;
    double bestGoalDist = 1e9;
    for (const auto& g : authW.stadium->goals) {
        double gx = 0.5 * (g.p0.x + g.p1.x);
        double gy = 0.5 * (g.p0.y + g.p1.y);
        double d = std::hypot(gkDisc.pos.x - gx, gkDisc.pos.y - gy);
        if (d < bestGoalDist) {
            bestGoalDist = d;
            ownGoal = &g;
        }
    }
    if (!ownGoal) return 0u;
    const auto& pp = authW.stadium->playerPhysics;
    double pAccel = (pp.acceleration > 0.001) ? pp.acceleration : 0.1;
    double pDamp  = (pp.damping > 0.5 && pp.damping < 1.0) ? pp.damping : 0.96;
    double bDamp  = (ball.damping > 0.5 && ball.damping < 1.0) ? ball.damping : 0.99;
    double gkR    = (gkDisc.radius > 1.0) ? gkDisc.radius : 14.0;
    double bR     = (ball.radius > 1.0) ? ball.radius : 7.6;

    double goalX = 0.5 * (ownGoal->p0.x + ownGoal->p1.x);
    double goalMinY = std::min(ownGoal->p0.y, ownGoal->p1.y);
    double goalMaxY = std::max(ownGoal->p0.y, ownGoal->p1.y);
    double goalCenterY = 0.5 * (goalMinY + goalMaxY);
    double goalHalfW = 0.5 * (goalMaxY - goalMinY);
    double inwardSign = (goalX > 0.0) ? -1.0 : 1.0;

    double standOffset = (std::abs(goalX) > 1000.0) ? 11.45 : (gkR + 2.0);
    double gkLineX = goalX + inwardSign * standOffset;

    double postMargin = gkR + 3.0;
    double clampMinY = goalMinY + postMargin;
    double clampMaxY = goalMaxY - postMargin;
    if (clampMinY > clampMaxY) {
        clampMinY = clampMaxY = goalCenterY;
    }

    auto decideAxis = [&](double pos, double vel, double target, double deadzone) -> int {
        double err = target - pos;
        if (std::abs(err) <= deadzone && std::abs(vel) <= pAccel * 1.4) {
            return 0;
        }

        double brakePos = pos;
        double v = vel;
        int brakeDir = (v > 0.0) ? -1 : ((v < 0.0) ? 1 : 0);
        if (brakeDir != 0) {
            for (int step = 0; step < 80; ++step) {
                double nextV = v + brakeDir * pAccel;
                if ((v > 0.0 && nextV <= 0.0) || (v < 0.0 && nextV >= 0.0)) {
                    break;
                }
                v = nextV;
                brakePos += v;
                v *= pDamp;
            }
        }
        if (vel > 0.05) {
            if (target > brakePos + 0.75) return 1;
            if (target < brakePos - 0.75) return -1;
            return 0;
        } else if (vel < -0.05) {
            if (target < brakePos - 0.75) return -1;
            if (target > brakePos + 0.75) return 1;
            return 0;
        }
        if (err > deadzone) return 1;
        if (err < -deadzone) return -1;
        return 0;
    };

    double ballSpeed = std::hypot(ball.speed.x, ball.speed.y);
    double ballTowardGoalVel = ball.speed.x * (-inwardSign);

    double targetX = gkLineX;
    double targetY = goalCenterY;
    bool urgentDive = false;
    bool rushClear = false;

    if (ballTowardGoalVel > 0.45 && ballSpeed > 0.75) {
        Vec2 simPos = ball.pos;
        Vec2 simVel = ball.speed;
        bool foundCross = false;
        double crossY = ball.pos.y;
        int crossTick = 0;
        int tmpBounces = 0;
        double tmpPostClear = 1e9;

        double planeX = gkDisc.pos.x;

        bool ballAlreadyPastGk = ((simPos.x - planeX) * inwardSign < 0.0);
        if (ballAlreadyPastGk) {
            planeX = goalX;
        }

        for (int t = 1; t <= 120; ++t) {
            Vec2 prevPos = simPos;
            simPos.x += simVel.x;
            simPos.y += simVel.y;
            simVel.x = bDamp * (simVel.x + ball.gravity.x);
            simVel.y = bDamp * (simVel.y + ball.gravity.y);
            resolveBallStaticCollisions(authW, ball, simPos, simVel, tmpBounces, tmpPostClear);

            double dPrev = (prevPos.x - planeX) * inwardSign;
            double dCur  = (simPos.x - planeX) * inwardSign;
            if (dPrev >= 0.0 && dCur <= 0.0) {
                double denom = dPrev - dCur;
                double frac = (denom > 1e-6) ? std::clamp(dPrev / denom, 0.0, 1.0) : 0.0;
                crossY = prevPos.y + (simPos.y - prevPos.y) * frac;
                crossTick = t;
                foundCross = true;
                break;
            }
            if (std::hypot(simVel.x, simVel.y) < 0.08) break;
        }

        if (foundCross) {

            if (std::abs(crossY - goalCenterY) <= goalHalfW + 65.0) {
                targetY = std::clamp(crossY, clampMinY, clampMaxY);
                urgentDive = true;

                double distToBallX = (ball.pos.x - gkDisc.pos.x) * inwardSign;
                if (ballSpeed < 4.2 && distToBallX > 0.0 && distToBallX < 130.0 &&
                    std::abs(targetY - gkDisc.pos.y) < 10.0 && crossTick > 10) {
                    targetX = ball.pos.x;
                    rushClear = true;
                }
            }
        }
    } else {

        const Disc* takerDisc = nullptr;
        double bestTakerDist = 1e9;
        for (const auto& op : authW.players) {
            if (op.team == 0 || op.team == gkPl->team) continue;
            if (op.discIndex < 0 || op.discIndex >= (int)authW.discs.size()) continue;
            const Disc& oD = authW.discs[op.discIndex];
            double d = std::hypot(ball.pos.x - oD.pos.x, ball.pos.y - oD.pos.y);
            if (d < bestTakerDist) {
                bestTakerDist = d;
                takerDisc = &oD;
            }
        }

        if (takerDisc) {
            double tR = (takerDisc->radius > 1.0) ? takerDisc->radius : gkR;
            double sumR = tR + bR;
            double takerGap = bestTakerDist - sumR;

            Vec2 projP = takerDisc->pos;
            Vec2 projV = takerDisc->speed;
            int tContact = 20;
            if (takerGap <= 4.0) {
                tContact = 0;
            } else {
                for (int t = 1; t <= 20; ++t) {
                    projP.x += projV.x;
                    projP.y += projV.y;
                    projV.x *= pDamp;
                    projV.y *= pDamp;
                    double g = std::hypot(ball.pos.x - projP.x, ball.pos.y - projP.y) - sumR;
                    if (g <= 4.0) {
                        tContact = t;
                        break;
                    }
                }
            }

            auto projectRayToGkLine = [&](const Vec2& fromPos, double& outY) -> bool {
                Vec2 dir = safeNormalize(Vec2(ball.pos.x - fromPos.x, ball.pos.y - fromPos.y));
                if (dir.x * (-inwardSign) <= 0.08) return false;
                double s = (gkLineX - ball.pos.x) / dir.x;
                if (s < 0.0) return false;
                outY = std::clamp(ball.pos.y + dir.y * s, clampMinY, clampMaxY);
                return true;
            };

            if (tContact <= 10 && takerGap <= 42.0) {
                int effTicks = tContact + 2;

                auto simCounterfactual = [&](double steerY) -> Vec2 {
                    Vec2 p = takerDisc->pos;
                    Vec2 v = takerDisc->speed;
                    for (int t = 0; t < effTicks; ++t) {

                        v.y += steerY * pAccel;
                        p.x += v.x;
                        p.y += v.y;
                        v.x *= pDamp;
                        v.y *= pDamp;
                        double g = std::hypot(ball.pos.x - p.x, ball.pos.y - p.y) - sumR;
                        if (g <= 3.95 && t >= tContact - 1) break;
                    }
                    return p;
                };

                Vec2 pExtremeUp   = simCounterfactual(-1.0);
                Vec2 pExtremeDown = simCounterfactual(+1.0);

                double aimFromUp = clampMaxY;
                double aimFromDown = clampMinY;
                bool okUp   = projectRayToGkLine(pExtremeUp, aimFromUp);
                bool okDown = projectRayToGkLine(pExtremeDown, aimFromDown);

                if (okUp && okDown) {
                    double coneMinY = std::min(aimFromUp, aimFromDown);
                    double coneMaxY = std::max(aimFromUp, aimFromDown);
                    targetY = std::clamp(0.5 * (coneMinY + coneMaxY), clampMinY, clampMaxY);

                    double coneWidth = coneMaxY - coneMinY;
                    if (coneWidth < goalHalfW * 1.15 && tContact <= 4) {
                        urgentDive = true;
                    }
                } else if (okUp || okDown) {
                    double validAim = okUp ? aimFromUp : aimFromDown;
                    targetY = std::clamp(0.5 * (goalCenterY + validAim), clampMinY, clampMaxY);
                } else {
                    targetY = goalCenterY;
                }
            } else {

                targetY = goalCenterY;
            }
        }
    }

    int iy = decideAxis(gkDisc.pos.y, gkDisc.speed.y, targetY, urgentDive ? 1.5 : 2.2);

    int ix = 0;
    double yErrAbs = std::abs(targetY - gkDisc.pos.y);
    if (!urgentDive || yErrAbs <= 7.5 || rushClear) {
        ix = decideAxis(gkDisc.pos.x, gkDisc.speed.x, targetX, rushClear ? 2.0 : 3.5);
    }

    uint32_t mask = 0u;
    if (iy < 0) mask |= 1u;
    else if (iy > 0) mask |= 2u;
    if (ix < 0) mask |= 4u;
    else if (ix > 0) mask |= 8u;

    double curDist = std::hypot(ball.pos.x - gkDisc.pos.x, ball.pos.y - gkDisc.pos.y);
    double curGap = curDist - (gkR + bR);
    if (curGap < 3.95 && gkPl->kickCooldown <= 0 && gkPl->kickRateCounter >= 0 && !gkPl->isKicking) {
        double clearDirX = (ball.pos.x - gkDisc.pos.x) / std::max(curDist, 1e-4);
        if (clearDirX * inwardSign > 0.15) {
            mask |= 16u;
        }
    }

    return mask;
}

uint32_t TacticalAssistant::computeSmartGkInput(const World& authW, uint32_t gkPlayerId) const {
    if (!authW.hasMatch || !authW.stadium || authW.discs.empty()) return 0u;
    const Player* gkPl = authW.getPlayer(gkPlayerId);
    if (!gkPl || gkPl->team == 0 || gkPl->discIndex < 0 || gkPl->discIndex >= (int)authW.discs.size()) return 0u;

    const Disc& gkDisc = authW.discs[gkPl->discIndex];
    const Disc& ball = authW.discs[0];

    const Goal* ownGoal = nullptr;
    for (const auto& g : authW.stadium->goals) {
        if (g.team == gkPl->team) {
            ownGoal = &g;
            break;
        }
    }
    if (!ownGoal) {
        double bestD = 1e9;
        for (const auto& g : authW.stadium->goals) {
            double gx = 0.5 * (g.p0.x + g.p1.x);
            double gy = 0.5 * (g.p0.y + g.p1.y);
            double d = std::hypot(gkDisc.pos.x - gx, gkDisc.pos.y - gy);
            if (d < bestD) { bestD = d; ownGoal = &g; }
        }
    }
    if (!ownGoal) return 0u;

    const auto& pp = authW.stadium->playerPhysics;
    double pAccel  = (pp.acceleration > 0.001) ? pp.acceleration : 0.1;
    double pDamp   = (pp.damping > 0.5 && pp.damping < 1.0) ? pp.damping : 0.96;
    double kickStr = (pp.kickStrength > 0.5) ? pp.kickStrength : 5.0;
    double bDamp   = (ball.damping > 0.5 && ball.damping < 1.0) ? ball.damping : 0.99;
    double bInvM   = (ball.invMass > 0.01) ? ball.invMass : 1.0;
    double gkR     = (gkDisc.radius > 1.0) ? gkDisc.radius : 14.0;
    double bR      = (ball.radius > 1.0) ? ball.radius : 7.6;
    double vMax    = pAccel / std::max(1.0 - pDamp, 1e-4);

    double goalX       = 0.5 * (ownGoal->p0.x + ownGoal->p1.x);
    double goalMinY    = std::min(ownGoal->p0.y, ownGoal->p1.y);
    double goalMaxY    = std::max(ownGoal->p0.y, ownGoal->p1.y);
    double goalCenterY = 0.5 * (goalMinY + goalMaxY);
    double goalHalfW   = std::max(20.0, 0.5 * (goalMaxY - goalMinY));
    double inwardSign  = (goalX > 0.0) ? -1.0 : 1.0;

    double postOffset = (std::abs(goalX) > 1000.0) ? 11.45 : 8.0;
    double postX = goalX + inwardSign * postOffset;
    double pitchHalfX = std::max(200.0, std::abs(postX));
    double boxDepth = pitchHalfX * 0.27;
    double sixYardDepth = pitchHalfX * 0.105;

    double minStandX = postX + inwardSign * (gkR + 4.0);
    double maxStandX = postX + inwardSign * (pitchHalfX * 0.78);
    double clampMinY = goalMinY + gkR + 2.0;
    double clampMaxY = goalMaxY - gkR - 2.0;
    if (clampMinY > clampMaxY) clampMinY = clampMaxY = goalCenterY;

    auto decideAxis = [&](double pos, double vel, double target, double deadzone) -> int {
        double err = target - pos;
        if (std::abs(err) <= deadzone && std::abs(vel) <= pAccel * 1.45) {
            return 0;
        }
        double brakePos = pos;
        double v = vel;
        int brakeDir = (v > 0.0) ? -1 : ((v < 0.0) ? 1 : 0);
        if (brakeDir != 0) {
            for (int step = 0; step < 80; ++step) {
                double nextV = v + brakeDir * pAccel;
                if ((v > 0.0 && nextV <= 0.0) || (v < 0.0 && nextV >= 0.0)) break;
                v = nextV;
                brakePos += v;
                v *= pDamp;
            }
        }
        if (vel > 0.05) {
            if (target > brakePos + 0.8) return 1;
            if (target < brakePos - 0.8) return -1;
            return 0;
        } else if (vel < -0.05) {
            if (target < brakePos - 0.8) return -1;
            if (target > brakePos + 0.8) return 1;
            return 0;
        }
        if (err > deadzone) return 1;
        if (err < -deadzone) return -1;
        return 0;
    };

    auto estimateTicksToReach = [&](const Vec2& pPos, const Vec2& pVel, const Vec2& tgt, double margin) -> double {
        double dist = std::hypot(tgt.x - pPos.x, tgt.y - pPos.y) - margin;
        if (dist <= 0.0) return 0.0;
        double nx = (tgt.x - pPos.x) / std::max(dist + margin, 1e-4);
        double ny = (tgt.y - pPos.y) / std::max(dist + margin, 1e-4);
        double vAlong = pVel.x * nx + pVel.y * ny;
        double pos = 0.0;
        double v = vAlong;
        for (int t = 1; t <= 120; ++t) {
            v += pAccel;
            pos += v;
            v *= pDamp;
            if (pos >= dist) return static_cast<double>(t);
        }
        return 120.0 + (dist - pos) / std::max(vMax, 1.0);
    };

    struct ThreatInfo {
        const Disc* disc = nullptr;
        uint32_t id = 0;
        double distToBall = 1e9;
        double gapToBall = 1e9;
        double ticksToBall = 1e9;
        double depthFromGoal = 1e9;
    };

    ThreatInfo primaryOpp{};
    double bestOppTicks = 1e9;
    double deepestOppDepth = pitchHalfX * 2.0;
    double bestTmTicks = 1e9;
    double bestTmDistToBall = 1e9;

    std::vector<const Disc*> boxOpponents;
    std::vector<const Disc*> allTeammates;

    for (const auto& p : authW.players) {
        if (p.team == 0 || p.id == gkPlayerId) continue;
        if (p.discIndex < 0 || p.discIndex >= (int)authW.discs.size()) continue;
        const Disc& d = authW.discs[p.discIndex];
        double r = (d.radius > 1.0) ? d.radius : gkR;
        double distB = std::hypot(ball.pos.x - d.pos.x, ball.pos.y - d.pos.y);
        double tReach = estimateTicksToReach(d.pos, d.speed, ball.pos, r + bR + 3.5);
        double depthG = (d.pos.x - postX) * inwardSign;

        if (p.team != gkPl->team) {
            if (depthG < deepestOppDepth) {
                deepestOppDepth = depthG;
            }
            if (tReach < bestOppTicks) {
                bestOppTicks = tReach;
                primaryOpp.disc = &d;
                primaryOpp.id = p.id;
                primaryOpp.distToBall = distB;
                primaryOpp.gapToBall = distB - (r + bR);
                primaryOpp.ticksToBall = tReach;
                primaryOpp.depthFromGoal = depthG;
            }
            if (depthG > -10.0 && depthG < boxDepth * 1.15 && std::abs(d.pos.y - goalCenterY) < goalHalfW * 2.1) {
                boxOpponents.push_back(&d);
            }
        } else {
            allTeammates.push_back(&d);
            if (tReach < bestTmTicks) bestTmTicks = tReach;
            if (distB < bestTmDistToBall) bestTmDistToBall = distB;
        }
    }

    bool hasOpenSecondaryRunner = false;
    double secondaryRunnerY = goalCenterY;
    double bestRunnerScore = -1e9;
    for (const Disc* oD : boxOpponents) {
        if (primaryOpp.disc && oD == primaryOpp.disc) continue;

        double nearestTm = 1e9;
        for (const Disc* tmD : allTeammates) {
            double td = std::hypot(oD->pos.x - tmD->pos.x, oD->pos.y - tmD->pos.y);
            if (td < nearestTm) nearestTm = td;
        }
        double depthG = (oD->pos.x - postX) * inwardSign;
        if (nearestTm > 35.0 && depthG < boxDepth * 1.05 && std::abs(oD->pos.y - goalCenterY) <= goalHalfW * 1.35) {
            double score = (boxDepth - depthG) + std::min(nearestTm, 120.0);
            if (score > bestRunnerScore) {
                bestRunnerScore = score;
                hasOpenSecondaryRunner = true;
                secondaryRunnerY = std::clamp(oD->pos.y + oD->speed.y * 10.0, clampMinY, clampMaxY);
            }
        }
    }

    constexpr int kRollTicks = 140;
    Vec2 rollPos[kRollTicks + 1];
    Vec2 rollVel[kRollTicks + 1];
    rollPos[0] = ball.pos;
    rollVel[0] = ball.speed;

    bool shotOnTarget = false;
    bool crossesGkPlane = false;
    double goalCrossY = goalCenterY;
    int tGoalCross = -1;
    double gkPlaneCrossY = goalCenterY;
    int tGkPlaneCross = -1;

    double ballSpeed = std::hypot(ball.speed.x, ball.speed.y);
    double ballTowardGoalVel = ball.speed.x * (-inwardSign);
    double gkActualX = gkDisc.pos.x;
    bool ballPastGk = ((ball.pos.x - gkActualX) * inwardSign < 0.0);

    {
        Vec2 sp = ball.pos;
        Vec2 sv = ball.speed;
        int tmpB = 0;
        double tmpPC = 1e9;
        for (int t = 1; t <= kRollTicks; ++t) {
            Vec2 prev = sp;
            sp.x += sv.x;
            sp.y += sv.y;
            sv.x = bDamp * (sv.x + ball.gravity.x);
            sv.y = bDamp * (sv.y + ball.gravity.y);
            resolveBallStaticCollisions(authW, ball, sp, sv, tmpB, tmpPC);
            rollPos[t] = sp;
            rollVel[t] = sv;

            if (!crossesGkPlane && !ballPastGk) {
                double dPrev = (prev.x - gkActualX) * inwardSign;
                double dCur  = (sp.x - gkActualX) * inwardSign;
                if (dPrev >= 0.0 && dCur <= 0.0) {
                    double frac = (dPrev - dCur > 1e-6) ? std::clamp(dPrev / (dPrev - dCur), 0.0, 1.0) : 0.0;
                    gkPlaneCrossY = prev.y + (sp.y - prev.y) * frac;
                    tGkPlaneCross = t;
                    crossesGkPlane = true;
                }
            }

            if (!shotOnTarget) {
                if (ownGoal->check(sp, prev)) {
                    double dPrev = (prev.x - goalX) * inwardSign;
                    double dCur  = (sp.x - goalX) * inwardSign;
                    double frac = (dPrev - dCur > 1e-6) ? std::clamp(dPrev / (dPrev - dCur), 0.0, 1.0) : 0.0;
                    goalCrossY = prev.y + (sp.y - prev.y) * frac;
                    tGoalCross = t;
                    shotOnTarget = true;
                } else {
                    double dPrev = (prev.x - postX) * inwardSign;
                    double dCur  = (sp.x - postX) * inwardSign;
                    if (dPrev >= 0.0 && dCur <= 0.0) {
                        double frac = (dPrev - dCur > 1e-6) ? std::clamp(dPrev / (dPrev - dCur), 0.0, 1.0) : 0.0;
                        double cy = prev.y + (sp.y - prev.y) * frac;
                        if (std::abs(cy - goalCenterY) <= goalHalfW + 24.0) {
                            goalCrossY = cy;
                            tGoalCross = t;
                            shotOnTarget = true;
                        }
                    }
                }
            }
            if (std::hypot(sv.x, sv.y) < 0.06) {
                for (int k = t + 1; k <= kRollTicks; ++k) {
                    rollPos[k] = sp;
                    rollVel[k] = Vec2(0.0, 0.0);
                }
                break;
            }
        }
    }

    double targetX = minStandX;
    double targetY = goalCenterY;
    bool urgentDive = false;
    bool pureVerticalDive = false;
    bool sweepingBall = false;

    bool hasOpenPassTarget = false;
    Vec2 bestPassTarget(0.0, 0.0);
    Vec2 bestPassNormal(inwardSign, 0.0);
    double bestPassScore = -1e9;
    {
        double kickImp = kickStr * bInvM;
        for (const Disc* tmD : allTeammates) {
            double upfieldDelta = (tmD->pos.x - ball.pos.x) * inwardSign;
            if (upfieldDelta < 45.0) continue;
            double rawDist = std::hypot(tmD->pos.x - ball.pos.x, tmD->pos.y - ball.pos.y);
            if (rawDist < 65.0 || rawDist > pitchHalfX * 0.80) continue;

            double estTicks = std::clamp(rawDist / std::max(kickImp * 0.92, 2.0), 3.0, 22.0);
            Vec2 leadPos(tmD->pos.x + tmD->speed.x * estTicks * 0.85,
                         tmD->pos.y + tmD->speed.y * estTicks * 0.85);
            Vec2 passVec(leadPos.x - ball.pos.x, leadPos.y - ball.pos.y);
            double passLen = std::hypot(passVec.x, passVec.y);
            if (passLen < 50.0) continue;
            Vec2 passDir(passVec.x / passLen, passVec.y / passLen);
            if (passDir.x * inwardSign < 0.25) continue;

            double minLaneClearance = 1e9;
            bool laneBlocked = false;
            for (const auto& op : authW.players) {
                if (op.team == 0 || op.team == gkPl->team) continue;
                if (op.discIndex < 0 || op.discIndex >= (int)authW.discs.size()) continue;
                const Disc& oD = authW.discs[op.discIndex];
                double proj = (oD.pos.x - ball.pos.x) * passDir.x + (oD.pos.y - ball.pos.y) * passDir.y;
                if (proj < 18.0 || proj > passLen * 0.96) continue;
                double tAtOpp = std::clamp(proj / std::max(kickImp, 2.0), 0.0, 25.0);
                Vec2 oFut(oD.pos.x + oD.speed.x * tAtOpp * 0.65, oD.pos.y + oD.speed.y * tAtOpp * 0.65);
                double perp = std::abs((oFut.x - ball.pos.x) * (-passDir.y) + (oFut.y - ball.pos.y) * passDir.x);
                if (perp < minLaneClearance) minLaneClearance = perp;
                if (perp < (gkR + bR + 18.0)) {
                    laneBlocked = true;
                    break;
                }
            }
            if (laneBlocked) continue;

            double distScore = 100.0 - std::abs(passLen - 280.0) * 0.18;
            double flankBonus = std::min(std::abs(leadPos.y - goalCenterY), 220.0) * 0.12;
            double openScore = std::min(minLaneClearance, 160.0) * 0.65 + distScore + flankBonus;
            if (openScore > bestPassScore) {
                bestPassScore = openScore;
                hasOpenPassTarget = true;
                bestPassTarget = leadPos;
                bestPassNormal = computeVelocityCompensatedKickNormal(ball.speed, kickImp, passDir);
            }
        }
    }

    bool activeShotThreat = shotOnTarget ||
        (crossesGkPlane && ballTowardGoalVel > 0.75 && std::abs(gkPlaneCrossY - goalCenterY) <= goalHalfW + 45.0 && tGkPlaneCross <= 65);

    double tGkToBallNow = estimateTicksToReach(gkDisc.pos, gkDisc.speed, ball.pos, gkR + bR + 1.0);
    bool underPassPressure = activeShotThreat ||
                             (primaryOpp.disc && (primaryOpp.distToBall < 90.0 || primaryOpp.ticksToBall < tGkToBallNow + 10.0));

    if (activeShotThreat) {
        urgentDive = true;

        if (ballPastGk && shotOnTarget) {
            targetX = postX;
            targetY = std::clamp(goalCrossY, clampMinY, clampMaxY);
        } else {

            int maxT = (tGoalCross > 0) ? tGoalCross : ((tGkPlaneCross > 0) ? tGkPlaneCross : 60);
            double saveY = crossesGkPlane ? std::clamp(gkPlaneCrossY, clampMinY, clampMaxY)
                                          : std::clamp(goalCrossY, clampMinY, clampMaxY);

            bool foundEarlyIntercept = false;
            Vec2 bestIntercept(gkActualX, saveY);
            if (ballSpeed < 5.2 || maxT > 32) {
                for (int t = 4; t <= std::min(maxT, 85); t += 2) {
                    const Vec2& bp = rollPos[t];
                    double bDepth = (bp.x - postX) * inwardSign;
                    if (bDepth < gkR + 2.0 || bDepth > pitchHalfX * 0.85) continue;
                    if (std::abs(bp.y - goalCenterY) > goalHalfW * 3.2) continue;
                    double tReach = estimateTicksToReach(gkDisc.pos, gkDisc.speed, bp, gkR + bR - 2.0);
                    if (tReach <= static_cast<double>(t) - 3.0) {

                        if (!primaryOpp.disc || tReach + 2.0 < primaryOpp.ticksToBall) {
                            bestIntercept = bp;
                            foundEarlyIntercept = true;
                            break;
                        }
                    }
                }
            }

            if (foundEarlyIntercept) {
                targetX = bestIntercept.x;
                targetY = bestIntercept.y;
                sweepingBall = true;
            } else {

                targetX = std::clamp(gkActualX, std::min(minStandX, maxStandX), std::max(minStandX, maxStandX));
                targetY = saveY;
                if (std::abs(targetY - gkDisc.pos.y) > 7.5) {
                    pureVerticalDive = true;
                }
            }
        }
    } else {

        Vec2 leadBall = rollPos[std::min(8, kRollTicks)];
        double ballDepth = (leadBall.x - postX) * inwardSign;
        double tGkToBall = estimateTicksToReach(gkDisc.pos, gkDisc.speed, leadBall, gkR + bR + 1.0);

        double maxSweepDepth = (deepestOppDepth > boxDepth * 1.15)
            ? std::min(pitchHalfX * 0.88, deepestOppDepth - 40.0)
            : (boxDepth * 1.05);
        maxSweepDepth = std::max(maxSweepDepth, boxDepth * 1.05);
        bool ballInSweepZone = (ballDepth > gkR && ballDepth < maxSweepDepth &&
                                std::abs(leadBall.y - goalCenterY) < std::max(goalHalfW * 2.6, pitchHalfX * 0.48));
        bool gkClearlyWinsRace = (tGkToBall + 4.0 < bestOppTicks) && (tGkToBall + 3.0 < bestTmTicks);
        bool teammateAlreadyControlling = (bestTmDistToBall < 42.0 && bestTmTicks + 2.0 < bestOppTicks);

        bool safeToSweep = !hasOpenSecondaryRunner || (ballDepth < boxDepth * 0.62);

        if (ballInSweepZone && ballSpeed < 4.5 && gkClearlyWinsRace && !teammateAlreadyControlling && safeToSweep) {
            if (hasOpenPassTarget && !underPassPressure && bestPassNormal.x * inwardSign > 0.22) {

                double stageDist = gkR + bR + 1.4;
                Vec2 aimBall = (tGkToBall <= 5.0) ? ball.pos : leadBall;
                targetX = aimBall.x - bestPassNormal.x * stageDist;
                targetY = aimBall.y - bestPassNormal.y * stageDist;
            } else {
                targetX = leadBall.x;
                targetY = leadBall.y;
            }
            sweepingBall = true;
        } else {

            int leadT = (ballSpeed > 1.5) ? 8 : 3;
            Vec2 B = rollPos[leadT];
            double bDepth = (B.x - postX) * inwardSign;

            Vec2 pTop(postX, goalMinY);
            Vec2 pBot(postX, goalMaxY);
            Vec2 uTop = safeNormalize(Vec2(pTop.x - B.x, pTop.y - B.y));
            Vec2 uBot = safeNormalize(Vec2(pBot.x - B.x, pBot.y - B.y));
            Vec2 uBisect = safeNormalize(Vec2(uTop.x + uBot.x, uTop.y + uBot.y), Vec2(-inwardSign, 0.0));

            double bisectorGoalY = goalCenterY;
            if (std::abs(uBisect.x) > 1e-4) {
                double s = (postX - B.x) / uBisect.x;
                if (s > 0.0) {
                    bisectorGoalY = std::clamp(B.y + uBisect.y * s, clampMinY, clampMaxY);
                }
            }

            double distBallToGoal = std::hypot(B.x - postX, B.y - bisectorGoalY);
            double desiredDepth = gkR + 14.0;

            if (bDepth > pitchHalfX * 1.02) {

                double oppHalfProgress = std::clamp((bDepth - pitchHalfX) / std::max(pitchHalfX, 1.0), 0.0, 1.0);
                desiredDepth = pitchHalfX * (0.40 + 0.34 * oppHalfProgress);
            } else if (bDepth > boxDepth * 1.35) {

                double halfFrac = std::clamp((bDepth - boxDepth * 1.35) / std::max(pitchHalfX * 1.02 - boxDepth * 1.35, 1.0), 0.0, 1.0);
                desiredDepth = boxDepth * 0.72 + (pitchHalfX * 0.40 - boxDepth * 0.72) * halfFrac;
            } else if (bDepth > boxDepth * 0.65) {

                if (hasOpenSecondaryRunner || boxOpponents.size() >= 2) {
                    desiredDepth = std::clamp(distBallToGoal * 0.28, sixYardDepth * 0.80, boxDepth * 0.48);
                } else {
                    desiredDepth = std::clamp(distBallToGoal * 0.36, sixYardDepth * 0.90, boxDepth * 0.64);
                }
            } else if (bDepth > 0.0) {

                if (!hasOpenSecondaryRunner && boxOpponents.size() <= 1 && primaryOpp.disc && primaryOpp.gapToBall < 85.0) {

                    desiredDepth = std::clamp(distBallToGoal * 0.56, gkR + 18.0, boxDepth * 0.72);
                } else {

                    desiredDepth = std::clamp(distBallToGoal * 0.30, gkR + 12.0, sixYardDepth * 0.95);
                }
            } else {

                desiredDepth = gkR + 8.0;
            }

            if (desiredDepth > boxDepth * 0.85 && deepestOppDepth < pitchHalfX * 1.8) {
                double maxSafeVsCherryPicker = std::max(boxDepth * 0.75, deepestOppDepth - 115.0);
                desiredDepth = std::min(desiredDepth, maxSafeVsCherryPicker);
            }

            if (std::abs(B.y - goalCenterY) > goalHalfW * 1.15 && bDepth < boxDepth * 0.75) {
                desiredDepth = std::min(desiredDepth, sixYardDepth * 0.75);
            }

            if (teammateAlreadyControlling && bDepth < boxDepth) {
                desiredDepth = std::min(desiredDepth, sixYardDepth * 0.55);
            }

            Vec2 outDir = safeNormalize(Vec2(B.x - postX, B.y - bisectorGoalY), Vec2(inwardSign, 0.0));
            if (outDir.x * inwardSign < 0.20) {
                outDir = safeNormalize(Vec2(inwardSign * 0.35, outDir.y));
            }
            double rayScale = desiredDepth / std::max(outDir.x * inwardSign, 0.20);
            rayScale = std::min(rayScale, std::max(0.0, distBallToGoal - (gkR + bR + 16.0)));

            targetX = postX + outDir.x * rayScale;
            targetY = bisectorGoalY + outDir.y * rayScale;

            double loX = std::min(minStandX, maxStandX);
            double hiX = std::max(minStandX, maxStandX);
            targetX = std::clamp(targetX, loX, hiX);

            double standDepthFrac = std::clamp(std::abs(targetX - postX) / std::max(pitchHalfX * 0.75, 1.0), 0.0, 1.0);
            double yLimit = (bDepth < boxDepth * 1.15)
                ? (goalHalfW * (0.95 + 0.90 * standDepthFrac))
                : (goalHalfW * (0.85 + 1.45 * standDepthFrac));
            targetY = std::clamp(targetY, goalCenterY - yLimit, goalCenterY + yLimit);

            if (primaryOpp.disc && primaryOpp.gapToBall <= 28.0 && primaryOpp.ticksToBall <= 7.0 && bDepth > 0.0 && bDepth < boxDepth * 1.25) {
                int effT = static_cast<int>(std::ceil(primaryOpp.ticksToBall)) + 2;
                double tR = (primaryOpp.disc->radius > 1.0) ? primaryOpp.disc->radius : gkR;
                double sumR = tR + bR;

                auto simCut = [&](double steerY) -> Vec2 {
                    Vec2 p = primaryOpp.disc->pos;
                    Vec2 v = primaryOpp.disc->speed;
                    for (int t = 0; t < effT; ++t) {
                        v.y += steerY * pAccel;
                        p.x += v.x;
                        p.y += v.y;
                        v.x *= pDamp;
                        v.y *= pDamp;
                        if (std::hypot(ball.pos.x - p.x, ball.pos.y - p.y) - sumR <= 3.95 && t >= effT - 3) break;
                    }
                    return p;
                };
                auto rayToGkX = [&](const Vec2& fromP, double& outY) -> bool {
                    Vec2 d = safeNormalize(Vec2(ball.pos.x - fromP.x, ball.pos.y - fromP.y));
                    if (d.x * (-inwardSign) <= 0.12) return false;
                    double s = (targetX - ball.pos.x) / d.x;
                    if (s < 0.0) return false;
                    outY = std::clamp(ball.pos.y + d.y * s, clampMinY, clampMaxY);
                    return true;
                };

                double aUp = clampMaxY, aDn = clampMinY;
                if (rayToGkX(simCut(-1.0), aUp) && rayToGkX(simCut(+1.0), aDn)) {
                    double cMin = std::min(aUp, aDn);
                    double cMax = std::max(aUp, aDn);
                    if ((cMax - cMin) < goalHalfW * 1.15) {
                        double minimaxY = 0.5 * (cMin + cMax);
                        targetY = std::clamp(0.40 * targetY + 0.60 * minimaxY, clampMinY, clampMaxY);
                        if (primaryOpp.ticksToBall <= 3.5) urgentDive = true;
                    }
                }
            }

            if (hasOpenSecondaryRunner && std::abs(B.y - goalCenterY) > goalHalfW * 0.75) {
                targetY = std::clamp(targetY * 0.74 + secondaryRunnerY * 0.26, clampMinY, clampMaxY);
            }
        }
    }

    if ((gkDisc.pos.x - postX) * inwardSign < 0.0) {
        targetX = minStandX;
        targetY = std::clamp(targetY, goalMinY + gkR + 8.0, goalMaxY - gkR - 8.0);
        pureVerticalDive = false;
    }

    int iy = decideAxis(gkDisc.pos.y, gkDisc.speed.y, targetY, urgentDive ? 1.5 : 2.5);
    int ix = 0;
    if (!pureVerticalDive) {
        ix = decideAxis(gkDisc.pos.x, gkDisc.speed.x, targetX, sweepingBall ? 1.8 : 3.5);
    }

    uint32_t mask = 0u;
    if (iy < 0) mask |= 1u;
    else if (iy > 0) mask |= 2u;
    if (ix < 0) mask |= 4u;
    else if (ix > 0) mask |= 8u;

    double curDist = std::hypot(ball.pos.x - gkDisc.pos.x, ball.pos.y - gkDisc.pos.y);
    double curGap = curDist - (gkR + bR);
    if (curGap < 3.95 && gkPl->kickCooldown <= 0 && gkPl->kickRateCounter >= 0 && !gkPl->isKicking) {
        Vec2 kn = safeNormalize(Vec2(ball.pos.x - gkDisc.pos.x, ball.pos.y - gkDisc.pos.y), Vec2(inwardSign, 0.0));

        Vec2 simPos = ball.pos;
        Vec2 simVel(ball.speed.x + kn.x * kickStr * bInvM, ball.speed.y + kn.y * kickStr * bInvM);
        bool risksOwnGoal = false;
        int tmpB = 0;
        double tmpPC = 1e9;
        for (int t = 1; t <= 35; ++t) {
            Vec2 prev = simPos;
            simPos.x += simVel.x;
            simPos.y += simVel.y;
            simVel.x = bDamp * (simVel.x + ball.gravity.x);
            simVel.y = bDamp * (simVel.y + ball.gravity.y);
            resolveBallStaticCollisions(authW, ball, simPos, simVel, tmpB, tmpPC);
            if (ownGoal->check(simPos, prev)) {
                risksOwnGoal = true;
                break;
            }
        }

        bool goodClearAngle = (kn.x * inwardSign > 0.22) ||
                              (kn.x * inwardSign > -0.05 && std::abs(kn.y) > 0.72 && std::abs(ball.pos.y - goalCenterY) > goalHalfW * 0.45);
        if (!risksOwnGoal && goodClearAngle) {
            if (hasOpenPassTarget && !underPassPressure && bestPassNormal.x * inwardSign > 0.22) {
                double passAlign = kn.x * bestPassNormal.x + kn.y * bestPassNormal.y;
                if (passAlign >= 0.94) {
                    mask |= 16u;
                }
            } else {
                mask |= 16u;
            }
        }
    }

    return mask;
}

}
