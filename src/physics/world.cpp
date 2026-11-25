#include "world.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <unordered_set>

namespace haxball {

World::World() : World(Stadium::createClassic()) {}

World::World(std::shared_ptr<Stadium> st) : stadium(std::move(st)) {
    teamColors[1].angle = 0;
    teamColors[1].textColor = 0xFFFFFF;
    teamColors[1].colors = { 0xE56E56 };

    teamColors[2].angle = 0;
    teamColors[2].textColor = 0xFFFFFF;
    teamColors[2].colors = { 0x5689E5 };

    hasMatch = false;
    startGame();
}

World World::cloneState() const {
    World copy = *this;
    copy.onKick = nullptr;
    copy.onGoal = nullptr;
    copy.onPlayerBallKick = nullptr;
    return copy;
}

void World::deserialize(::BufferReader& sr) {
    auto optRoomName = sr.readOptStr();
    roomName = optRoomName.value_or("Haxball Room");
    teamsLocked = sr.readU8() != 0;
    scoreLimit = sr.readI32BE();
    timeLimit = sr.readI32BE();
    kickBurst = sr.readI16BE();
    kickRate = sr.readU8();
    kickMin = sr.readU8();

    stadium = Stadium::deserialize(sr);

    hasMatch = sr.readU8() != 0;
    discs.clear();
    if (hasMatch) {
        uint8_t discCount = sr.readU8();
        discs.reserve(discCount);
        for (uint8_t i = 0; i < discCount; ++i) {
            Disc d;
            d.deserialize(sr);
            discs.push_back(d);
        }

        goalTickCounter = sr.readI32BE();
        matchState = sr.readI32BE();
        redScore = sr.readI32BE();
        blueScore = sr.readI32BE();
        timeElapsed = sr.readF64BE();
        pauseTickCounter = sr.readI32BE();
        goalConcedingTeam = sr.readI8();
        if (goalConcedingTeam != 1 && goalConcedingTeam != 2) goalConcedingTeam = 0;
    } else {
        goalTickCounter = 0;
        matchState = 0;
        redScore = 0;
        blueScore = 0;
        timeElapsed = 0.0;
        pauseTickCounter = 0;
        goalConcedingTeam = 1;
    }

    uint8_t playerCount = sr.readU8();
    players.clear();
    players.reserve(playerCount);
    for (uint8_t i = 0; i < playerCount; ++i) {
        bool admin = sr.readU8() != 0;
        int32_t teamOrder = sr.readI32BE();
        auto avatar = sr.readOptStr();
        auto headAvatar = sr.readOptStr();
        bool desynced = sr.readU8() != 0;
        auto flag = sr.readOptStr();
        int32_t pPing = sr.readI32BE();
        auto pName = sr.readOptStr();
        int32_t pInput = sr.readI32BE();
        uint32_t pId = sr.readVarInt();
        bool isKicking = sr.readU8() != 0;
        int16_t kickRateCnt = sr.readI16BE();
        uint8_t kickCd = sr.readU8();
        int8_t team = sr.readI8();
        int16_t discIdx = sr.readI16BE();

        Player pl;
        pl.id = pId;
        pl.name = pName.value_or("Player");
        pl.flag = flag.value_or("nl");
        pl.avatar = avatar.value_or("");
        pl.headlessAvatar = headAvatar;
        pl.team = (team == 1 || team == 2) ? team : 0;
        pl.teamOrder = teamOrder;
        pl.admin = admin;
        pl.desynced = desynced;
        pl.ping = pPing;
        pl.input = (uint32_t)pInput;
        pl.isKicking = isKicking;
        pl.kickRateCounter = kickRateCnt;
        pl.kickCooldown = kickCd;
        pl.discIndex = (hasMatch && discIdx >= 0 && discIdx < (int)discs.size()) ? (int)discIdx : -1;
        if (pl.discIndex >= 0) {
            discs[pl.discIndex].playerId = (int32_t)pl.id;
        }
        players.push_back(pl);
    }

    teamColors[1].deserialize(sr);
    teamColors[2].deserialize(sr);
}

void World::removePlayerDisc(Player& p) {
    if (p.discIndex >= 0 && p.discIndex < (int)discs.size()) {
        int removedIdx = p.discIndex;
        discs.erase(discs.begin() + removedIdx);
        p.discIndex = -1;
        for (auto& other : players) {
            if (other.discIndex > removedIdx) {
                other.discIndex--;
            }
        }
    } else {
        p.discIndex = -1;
    }
}

void World::spawnOrResetPlayerDisc(Player& p) {
    if (!stadium) return;
    p.input = 0;
    if (p.discIndex < 0 || p.discIndex >= (int)discs.size()) {
        Disc d;
        d.playerId = (int32_t)p.id;
        p.discIndex = (int)discs.size();
        discs.push_back(d);
    }
    auto& r = discs[p.discIndex];
    const auto& c = stadium->playerPhysics;
    r.playerId = (int32_t)p.id;
    r.color = 0;
    r.radius = c.radius;
    r.invMass = c.invMass;
    r.damping = c.damping;
    r.bCoef = c.bCoef;
    r.cMask = 39;
    int32_t teamGroup = (p.team == 1) ? 2 : (p.team == 2) ? 4 : 0;
    r.cGroup = teamGroup | c.cGroup;

    const auto& spawns = (p.team == 1) ? stadium->redSpawnPoints : stadium->blueSpawnPoints;
    double dir = (p.team == 1) ? -1.0 : (p.team == 2) ? 1.0 : 0.0;
    if (spawns.empty()) {
        r.pos = Vec2(dir * stadium->width, 0.0);
    } else {
        r.pos = spawns.back();
    }
    r.speed = Vec2(0.0, 0.0);
    r.gravity = c.gravity;
}

void World::updatePlayerDisc(Player& p) {
    if (p.team == 0) {
        removePlayerDisc(p);
    } else {
        spawnOrResetPlayerDisc(p);
    }
}

void World::startGame() {
    if (hasMatch || !stadium) return;
    hasMatch = true;
    redScore = 0;
    blueScore = 0;
    timeElapsed = 0.0;
    matchState = 0;
    goalTickCounter = 0;
    pauseTickCounter = 0;
    goalConcedingTeam = 1;

    for (auto& p : players) {
        p.discIndex = -1;
        p.teamOrder = 0;
    }

    discs.clear();
    for (const auto& d : stadium->discs) {
        Disc copy = d;
        copy.playerId = -1;
        discs.push_back(copy);
    }

    resetKickOffPositions();
}

void World::stopGame() {
    if (!hasMatch) return;
    hasMatch = false;
    discs.clear();
    for (auto& p : players) {
        p.discIndex = -1;
        p.teamOrder = 0;
    }
}

void World::pauseResumeGame(bool pause) {
    if (!hasMatch) return;
    bool wasPaused = (pauseTickCounter == 120);
    if (pause) {
        pauseTickCounter = 120;
    } else if (wasPaused) {
        pauseTickCounter = 119;
    }
}

void World::resetKickOffPositions() {
    if (!hasMatch || !stadium) return;
    matchState = 0;

    int resetCount = stadium->kickOffResetFull ? (int)stadium->discs.size() : 1;
    for (int g = 0; g < resetCount && g < (int)discs.size() && g < (int)stadium->discs.size(); ++g) {
        int32_t pId = discs[g].playerId;
        discs[g] = stadium->discs[g];
        discs[g].playerId = pId;
    }

    int U[3] = {0, 0, 0};
    for (auto& p : players) {
        updatePlayerDisc(p);
        int u = p.team;
        if (u == 1 || u == 2) {
            double dir = (u == 1) ? -1.0 : 1.0;
            auto& C = discs[p.discIndex].pos;
            int h = U[u];
            const auto& O = (u == 1) ? stadium->redSpawnPoints : stadium->blueSpawnPoints;
            if (O.empty()) {
                int g = (h + 1) >> 1;
                if ((h & 1) == 0) g = -g;
                C.x = stadium->spawnDistance * dir;
                C.y = 55.0 * g;
            } else {
                int idx = std::min(h, (int)O.size() - 1);
                C = O[idx];
            }
            p.teamOrder = ++U[u];
        }
    }
}

Player* World::getPlayer(uint32_t id) {
    for (auto& p : players) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

const Player* World::getPlayer(uint32_t id) const {
    for (const auto& p : players) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

Player* World::addPlayer(uint32_t id, const std::string& name, const std::string& flag, const std::string& avatar) {
    if (auto* existing = getPlayer(id)) {
        existing->name = name;
        existing->flag = flag;
        existing->avatar = avatar;
        return existing;
    }
    Player p;
    p.id = id;
    p.name = name;
    p.flag = flag;
    p.avatar = avatar;
    p.team = 0;
    players.push_back(p);
    return &players.back();
}

void World::removePlayer(uint32_t id) {
    for (size_t i = 0; i < players.size(); ++i) {
        if (players[i].id == id) {
            if (hasMatch) {
                removePlayerDisc(players[i]);
            }
            players.erase(players.begin() + i);
            return;
        }
    }
}

void World::setPlayerTeam(uint32_t id, int team) {
    if (team < 0 || team > 2) team = 0;
    int foundIdx = -1;
    for (size_t i = 0; i < players.size(); ++i) {
        if (players[i].id == id) {
            foundIdx = (int)i;
            break;
        }
    }
    if (foundIdx < 0) return;
    if (players[foundIdx].team == team) return;

    Player p = players[foundIdx];
    players.erase(players.begin() + foundIdx);
    p.team = team;
    players.push_back(p);

    auto& moved = players.back();
    if (!hasMatch) return;

    removePlayerDisc(moved);
    updatePlayerDisc(moved);

    int32_t n = 0;
    bool used = false;
    do {
        n++;
        used = false;
        for (const auto& other : players) {
            if (other.id != moved.id && other.team == moved.team && other.teamOrder == n) {
                used = true;
                break;
            }
        }
    } while (used);
    moved.teamOrder = n;
}

void World::setPlayerInput(uint32_t id, uint32_t inputMask) {
    if (auto* p = getPlayer(id)) {
        if (!(p->input & 16) && (inputMask & 16)) {
            p->isKicking = true;
        }
        p->input = inputMask;
    }
}

void World::autoTeams() {
    std::vector<uint32_t> specs;
    int redCount = 0;
    int blueCount = 0;
    for (const auto& p : players) {
        if (p.team == 0) specs.push_back(p.id);
        else if (p.team == 1) redCount++;
        else if (p.team == 2) blueCount++;
    }
    if (specs.empty()) return;
    if (blueCount == redCount) {
        if (specs.size() >= 2) {
            setPlayerTeam(specs[0], 1);
            setPlayerTeam(specs[1], 2);
        }
    } else {
        setPlayerTeam(specs[0], (blueCount > redCount) ? 1 : 2);
    }
}

void World::reorderPlayers(const std::vector<int32_t>& idList, bool moveToTop) {
    std::vector<Player> selected;
    std::vector<Player> remaining;
    std::unordered_set<uint32_t> movedIds;

    for (int32_t targetId : idList) {
        for (const auto& p : players) {
            if ((int32_t)p.id == targetId && movedIds.find(p.id) == movedIds.end()) {
                selected.push_back(p);
                movedIds.insert(p.id);
                break;
            }
        }
    }
    for (const auto& p : players) {
        if (movedIds.find(p.id) == movedIds.end()) {
            remaining.push_back(p);
        }
    }

    players.clear();
    if (moveToTop) {
        players.insert(players.end(), selected.begin(), selected.end());
        players.insert(players.end(), remaining.begin(), remaining.end());
    } else {
        players.insert(players.end(), remaining.begin(), remaining.end());
        players.insert(players.end(), selected.begin(), selected.end());
    }
}

void World::setKickRateLimit(int32_t min, int32_t rate, int32_t burst) {
    kickMin = (uint8_t)std::clamp(min, 0, 255);
    kickRate = (uint8_t)std::clamp(rate, 0, 255);
    int32_t clampedBurst = std::clamp(burst, 0, 100);
    kickBurst = (int16_t)(kickRate * clampedBurst);
}

void World::setDiscProperties(int32_t id, bool isPlayer, const std::optional<float> fProps[10], const std::optional<int32_t> iProps[3]) {
    if (!hasMatch) return;
    Disc* n = nullptr;
    if (isPlayer) {
        if (auto* p = getPlayer((uint32_t)id)) {
            if (p->discIndex >= 0 && p->discIndex < (int)discs.size()) {
                n = &discs[p->discIndex];
            }
        }
    } else {
        if (id >= 0 && id < (int)discs.size()) {
            n = &discs[id];
        }
    }
    if (!n) return;

    if (fProps[0].has_value()) n->pos.x = *fProps[0];
    if (fProps[1].has_value()) n->pos.y = *fProps[1];
    if (fProps[2].has_value()) n->speed.x = *fProps[2];
    if (fProps[3].has_value()) n->speed.y = *fProps[3];
    if (fProps[4].has_value()) n->gravity.x = *fProps[4];
    if (fProps[5].has_value()) n->gravity.y = *fProps[5];
    if (fProps[6].has_value()) n->radius = *fProps[6];
    if (fProps[7].has_value()) n->bCoef = *fProps[7];
    if (fProps[8].has_value()) n->invMass = *fProps[8];
    if (fProps[9].has_value()) n->damping = *fProps[9];

    if (iProps[0].has_value()) n->color = (uint32_t)(*iProps[0]);
    if (iProps[1].has_value()) n->cMask = *iProps[1];
    if (iProps[2].has_value()) n->cGroup = *iProps[2];
}

void World::step(double dt) {
    currentTick++;
    if (!hasMatch || !stadium) return;

    if (pauseTickCounter > 0) {
        if (pauseTickCounter < 120) pauseTickCounter--;
        return;
    }

    const auto& D = stadium->playerPhysics;
    for (auto& g : players) {
        if (g.discIndex < 0 || g.discIndex >= (int)discs.size()) continue;
        auto& U = discs[g.discIndex];

        if ((g.input & 16) == 0) g.isKicking = false;
        if (g.kickCooldown > 0) g.kickCooldown--;
        if (g.kickRateCounter < kickBurst) g.kickRateCounter++;

        if (g.isKicking && g.kickCooldown <= 0 && g.kickRateCounter >= 0) {
            bool A = false;
            bool blockedFake = false;
            for (size_t i = 0; i < discs.size(); ++i) {
                if ((int)i == g.discIndex) continue;
                auto& u = discs[i];
                if ((u.cGroup & 64) != 0) {
                    double rx = u.pos.x - U.pos.x;
                    double ry = u.pos.y - U.pos.y;
                    double f = std::sqrt(rx * rx + ry * ry);
                    if (f > 1e-9 && !(f - u.radius - U.radius >= 4.0)) {
                        if (!g.allowSpeculativeKick) {
                            blockedFake = true;
                        } else {
                            double a = rx / f;
                            double L = ry / f;
                            Vec2 preVel = u.speed;
                            u.applyImpulse(D.kickStrength, a, L);
                            U.applyImpulse(-D.kickback, a, L);
                            A = true;
                            if (i == 0 && onPlayerBallKick) {
                                BallKickEvent ev;
                                ev.tick = currentTick;
                                ev.playerId = g.id;
                                ev.team = g.team;
                                ev.preBallVel = preVel;
                                ev.postBallVel = u.speed;
                                ev.kickNormal = Vec2(a, L);
                                ev.baseDeltaV = D.kickStrength * u.invMass;
                                ev.ballDamping = (u.damping > 0.5 && u.damping < 1.0) ? u.damping : 0.99;
                                onPlayerBallKick(ev);
                            }
                        }
                    }
                }
            }
            if (blockedFake) {
                g.fakeShotDetected = true;
            }
            if (A) {
                if (onKick) onKick();
                g.isKicking = false;
                g.kickCooldown = kickMin;
                g.kickRateCounter -= kickRate;
            }
        }

        uint32_t inp = g.input;
        double r = ((inp & 8) > 0) - ((inp & 4) > 0);
        double f = ((inp & 2) > 0) - ((inp & 1) > 0);
        if (r != 0.0 && f != 0.0) {
            r *= 0.7071067811865475;
            f *= 0.7071067811865475;
        }
        if (r != 0.0 || f != 0.0) {
            double a = g.isKicking ? D.kickingAcceleration : D.acceleration;
            U.speed.x += r * a;
            U.speed.y += f * a;
        }
        U.damping = g.isKicking ? D.kickingDamping : D.damping;
    }

    struct ScoreDiscPrev {
        size_t idx;
        Vec2 prevPos;
    };
    ScoreDiscPrev stackScoreDiscs[16];
    size_t stackScoreCount = 0;
    std::vector<ScoreDiscPrev> overflowScoreDiscs;
    for (size_t i = 0; i < discs.size(); ++i) {
        if ((discs[i].cGroup & 128) != 0) {
            if (stackScoreCount < 16) {
                stackScoreDiscs[stackScoreCount++] = {i, discs[i].pos};
            } else {
                overflowScoreDiscs.push_back({i, discs[i].pos});
            }
        }
    }

    stepPhysics(dt);

    switch (matchState) {
        case 0: {
            int32_t koMask = (goalConcedingTeam == 1) ? 8 : (goalConcedingTeam == 2) ? 16 : 0;
            for (auto& g : players) {
                if (g.discIndex >= 0 && g.discIndex < (int)discs.size()) {
                    discs[g.discIndex].cMask = 39 | koMask;
                }
            }
            if (!discs.empty()) {
                const auto& bSpeed = discs[0].speed;
                if (bSpeed.x * bSpeed.x + bSpeed.y * bSpeed.y > 0.0) {
                    matchState = 1;
                }
            }
            break;
        }
        case 1: {
            timeElapsed += 0.016666666666666666;
            for (auto& g : players) {
                if (g.discIndex >= 0 && g.discIndex < (int)discs.size()) {
                    discs[g.discIndex].cMask = 39;
                }
            }
            int scoredGoalIdx = -1;
            const auto& goals = stadium->goals;
            auto checkScoreDisc = [&](const ScoreDiscPrev& sd) {
                if (sd.idx >= discs.size()) return;
                for (size_t gi = 0; gi < goals.size(); ++gi) {
                    if (goals[gi].check(discs[sd.idx].pos, sd.prevPos)) {
                        scoredGoalIdx = (int)gi;
                        return;
                    }
                }
            };
            for (size_t si = 0; si < stackScoreCount && scoredGoalIdx < 0; ++si) {
                checkScoreDisc(stackScoreDiscs[si]);
            }
            for (size_t si = 0; si < overflowScoreDiscs.size() && scoredGoalIdx < 0; ++si) {
                checkScoreDisc(overflowScoreDiscs[si]);
            }
            if (scoredGoalIdx >= 0) {
                int teamConceded = stadium->goals[scoredGoalIdx].team;
                matchState = 2;
                goalTickCounter = 150;
                goalConcedingTeam = teamConceded;
                if (teamConceded == 1) blueScore++;
                else redScore++;
                if (onGoal) onGoal(teamConceded == 1 ? 2 : 1);
            } else if (timeLimit > 0 && timeElapsed >= 60.0 * timeLimit && redScore != blueScore) {
                goalTickCounter = 300;
                matchState = 3;
            }
            break;
        }
        case 2: {
            goalTickCounter--;
            if (goalTickCounter <= 0) {
                if ((scoreLimit > 0 && (redScore >= scoreLimit || blueScore >= scoreLimit)) ||
                    (timeLimit > 0 && timeElapsed >= 60.0 * timeLimit && redScore != blueScore)) {
                    goalTickCounter = 300;
                    matchState = 3;
                } else {
                    resetKickOffPositions();
                }
            }
            break;
        }
        case 3: {
            goalTickCounter--;
            if (goalTickCounter <= 0) {
                stopGame();
            }
            break;
        }
    }
}

void World::stepPhysics(double dt) {
    if (discs.empty() || !stadium) return;

    for (auto& d : discs) {
        double vx = d.speed.x;
        double vy = d.speed.y;
        d.pos.x += dt * vx;
        d.pos.y += dt * vy;
        d.speed.x = d.damping * (vx + d.gravity.x);
        d.speed.y = d.damping * (vy + d.gravity.y);
    }

    const auto& planes = stadium->planes;
    const auto& segments = stadium->segments;
    const auto& vertices = stadium->vertices;
    const auto& joints = stadium->joints;
    const int numVertices = static_cast<int>(vertices.size());

    for (size_t V = 0; V < discs.size(); ++V) {
        auto& i = discs[V];
        double o = i.bCoef;
        double t = i.radius;
        int32_t D_mask = i.cMask;
        int32_t X_group = i.cGroup;
        double J_invMass = i.invMass;

        for (size_t O = V + 1; O < discs.size(); ++O) {
            auto& B = discs[O];
            if ((B.cMask & X_group) != 0 && (B.cGroup & D_mask) != 0) {
                double ux = i.pos.x - B.pos.x;
                double vy = i.pos.y - B.pos.y;
                double distSq = ux * ux + vy * vy;
                double minDist = B.radius + t;
                if (distSq > 0.0 && distSq <= minDist * minDist && (J_invMass + B.invMass) > 0.0) {
                    double dist = std::sqrt(distSq);
                    ux /= dist;
                    vy /= dist;
                    double massRatio = J_invMass / (J_invMass + B.invMass);
                    double overlap = minDist - dist;
                    double moveA = overlap * massRatio;
                    i.pos.x += ux * moveA;
                    i.pos.y += vy * moveA;
                    double moveB = overlap - moveA;
                    B.pos.x -= ux * moveB;
                    B.pos.y -= vy * moveB;

                    double relVel = ux * (i.speed.x - B.speed.x) + vy * (i.speed.y - B.speed.y);
                    if (relVel < 0.0) {
                        relVel *= o * B.bCoef + 1.0;
                        massRatio *= relVel;
                        i.speed.x -= ux * massRatio;
                        i.speed.y -= vy * massRatio;
                        double impB = relVel - massRatio;
                        B.speed.x += ux * impB;
                        B.speed.y += vy * impB;
                    }
                }
            }
        }

        if (J_invMass != 0.0) {

            for (const auto& q : planes) {
                if ((q.cMask & X_group) != 0 && (q.cGroup & D_mask) != 0) {
                    double H = q.normal.x;
                    double W = q.normal.y;
                    double s = q.dist - (H * i.pos.x + W * i.pos.y) + t;
                    if (s > 0.0) {
                        i.pos.x += H * s;
                        i.pos.y += W * s;
                        double velDot = i.speed.x * H + i.speed.y * W;
                        if (velDot < 0.0) {
                            velDot *= o * q.bCoef + 1.0;
                            i.speed.x -= velDot * H;
                            i.speed.y -= velDot * W;
                        }
                    }
                }
            }

            for (const auto& q : segments) {
                if ((q.cMask & X_group) != 0 && (q.cGroup & D_mask) != 0) {
                    double H = 0.0, W = 0.0, T = 0.0;
                    double gx = i.pos.x;
                    double by = i.pos.y;

                    if (q.isArc && std::isfinite(q.curveF)) {
                        H = gx - q.center.x;
                        W = by - q.center.y;
                        const Vec2& Tn = q.v0Normal;
                        const Vec2& n2 = q.v1Normal;
                        if (((Tn.x * H + Tn.y * W > 0.0 && n2.x * H + n2.y * W > 0.0) == (q.curveF <= 0.0))) {
                            continue;
                        }
                        double dist = std::sqrt(H * H + W * W);
                        if (dist == 0.0) continue;
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
                        if (T < 0.0) {
                            T = -T;
                            H = -H;
                            W = -W;
                        }
                    } else {
                        if (bias < 0.0) {
                            bias = -bias;
                            T = -T;
                            H = -H;
                            W = -W;
                        }
                        if (T < -bias) continue;
                    }

                    if (T < t) {
                        T = t - T;
                        i.pos.x += H * T;
                        i.pos.y += W * T;
                        T = H * i.speed.x + W * i.speed.y;
                        if (T < 0.0) {
                            T *= o * q.bCoef + 1.0;
                            i.speed.x -= H * T;
                            i.speed.y -= W * T;
                        }
                    }
                }
            }

            double radiusSq = t * t;
            for (const auto& q : vertices) {
                if ((q.cMask & X_group) != 0 && (q.cGroup & D_mask) != 0) {
                    double s = i.pos.x - q.pos.x;
                    double F = i.pos.y - q.pos.y;
                    double distSq = s * s + F * F;
                    if (distSq > 0.0 && distSq <= radiusSq) {
                        double dist = std::sqrt(distSq);
                        s /= dist;
                        F /= dist;
                        double overlap = t - dist;
                        i.pos.x += s * overlap;
                        i.pos.y += F * overlap;
                        double velDot = s * i.speed.x + F * i.speed.y;
                        if (velDot < 0.0) {
                            velDot *= o * q.bCoef + 1.0;
                            i.speed.x -= s * velDot;
                            i.speed.y -= F * velDot;
                        }
                    }
                }
            }
        }
    }

    if (!joints.empty()) {
        for (int iter = 0; iter < 2; ++iter) {
            for (const auto& j : joints) {
                if (j.d0 >= discs.size() || j.d1 >= discs.size()) continue;
                auto& dA = discs[j.d0];
                auto& dB = discs[j.d1];
                double p = dA.pos.x - dB.pos.x;
                double V = dA.pos.y - dB.pos.y;
                double B = std::sqrt(p * p + V * V);
                if (B <= 0.0) continue;
                p /= B;
                V /= B;
                double O = dA.invMass / (dA.invMass + dB.invMass);
                if (std::isnan(O)) O = 0.5;

                double s = 0.0;
                int F = 0;
                if (j.minLength >= j.maxLength) {
                    s = j.minLength;
                    F = 0;
                } else if (B <= j.minLength) {
                    s = j.minLength;
                    F = 1;
                } else if (B >= j.maxLength) {
                    s = j.maxLength;
                    F = -1;
                } else {
                    continue;
                }

                B = s - B;
                if (std::isfinite(j.strength)) {
                    O = j.strength * B * 0.5;
                    p *= O;
                    V *= O;
                    dA.speed.x += p * dA.invMass;
                    dA.speed.y += V * dA.invMass;
                    dB.speed.x -= p * dB.invMass;
                    dB.speed.y -= V * dB.invMass;
                } else {
                    s = B * O;
                    dA.pos.x += p * s * 0.5;
                    dA.pos.y += V * s * 0.5;
                    B -= s;
                    dB.pos.x -= p * B * 0.5;
                    dB.pos.y -= V * B * 0.5;
                    B = p * (dA.speed.x - dB.speed.x) + V * (dA.speed.y - dB.speed.y);
                    if (F * B <= 0.0) {
                        O *= B;
                        dA.speed.x -= p * O;
                        dA.speed.y -= V * O;
                        O = B - O;
                        dB.speed.x += p * O;
                        dB.speed.y += V * O;
                    }
                }
            }
        }
    }
}

}
