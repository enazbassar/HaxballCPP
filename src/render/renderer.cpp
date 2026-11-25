#include "renderer.hpp"
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace haxball {

static SDL_Color u32ToColor(uint32_t c, uint8_t alpha = 255) {
    return SDL_Color{
        static_cast<uint8_t>((c >> 16) & 0xFF),
        static_cast<uint8_t>((c >> 8) & 0xFF),
        static_cast<uint8_t>(c & 0xFF),
        alpha
    };
}

static const char* kQuickEmojis[14] = {
    "⚽", "🔥", "👑", "🐐", "💎", "⚡", "💀", "🥶", "😎", "🎯", "🏆", "🤡", "😂", "❤️"
};

GameRenderer::GameRenderer() {}

GameRenderer::~GameRenderer() {
    if (grassTileTex) SDL_DestroyTexture(grassTileTex);
    if (hockeyTileTex) SDL_DestroyTexture(hockeyTileTex);
}

bool GameRenderer::init(SDL_Renderer* renderer, const std::string& fontPath) {
    if (!fontSmall.init(renderer, fontPath, 14.0f)) return false;
    if (!font.init(renderer, fontPath, 17.0f)) return false;
    if (!fontLarge.init(renderer, fontPath, 24.0f)) return false;
    if (!fontBanner.init(renderer, fontPath, 46.0f)) return false;
    createPitchTextures(renderer);
    return true;
}

void GameRenderer::createPitchTextures(SDL_Renderer* renderer) {
    if (grassTileTex) SDL_DestroyTexture(grassTileTex);
    if (hockeyTileTex) SDL_DestroyTexture(hockeyTileTex);

    const int tw = 128;
    const int th = 128;
    std::vector<uint32_t> grassPixels(tw * th);
    for (int y = 0; y < th; ++y) {
        for (int x = 0; x < tw; ++x) {
            bool stripe = ((x / 64) ^ (y / 64)) & 1;
            int n = ((x * 37 + y * 17) & 3) - 1;
            int r = (stripe ? 115 : 108) + n;
            int g = (stripe ? 142 : 134) + n;
            int b = (stripe ? 92 : 86) + n;
            grassPixels[y * tw + x] = (0xFFu << 24) | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(r);
        }
    }
    grassTileTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, tw, th);
    if (grassTileTex) {
        SDL_UpdateTexture(grassTileTex, nullptr, grassPixels.data(), tw * sizeof(uint32_t));
    }

    std::vector<uint32_t> hockeyPixels(tw * th);
    for (int y = 0; y < th; ++y) {
        for (int x = 0; x < tw; ++x) {
            bool stripe = ((x / 64) ^ (y / 64)) & 1;
            int v = stripe ? 88 : 84;
            hockeyPixels[y * tw + x] = (0xFFu << 24) | (static_cast<uint32_t>(v) << 16) | (static_cast<uint32_t>(v) << 8) | static_cast<uint32_t>(v);
        }
    }
    hockeyTileTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, tw, th);
    if (hockeyTileTex) {
        SDL_UpdateTexture(hockeyTileTex, nullptr, hockeyPixels.data(), tw * sizeof(uint32_t));
    }
}

void GameRenderer::applyZoomPreset(int presetIdx) {
    static const float presets[] = { 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.25f, 0.75f };
    if (presetIdx >= 1 && presetIdx <= 7) {
        setZoom(presets[presetIdx - 1]);
    }
}

int GameRenderer::getEffectiveFieldRotation(int localPlayerTeam) const {
    if (settings.fieldRotation == 4) {

        return (localPlayerTeam == 2) ? 3 : 1;
    }
    return ((settings.fieldRotation % 4) + 4) % 4;
}

float GameRenderer::getRotationRadians() const {
    switch (activeRotation) {
        case 1: return -static_cast<float>(M_PI) * 0.5f;
        case 2: return  static_cast<float>(M_PI);
        case 3: return  static_cast<float>(M_PI) * 0.5f;
        default: return 0.0f;
    }
}

uint32_t GameRenderer::remapInputForRotation(uint32_t screenKeyMask, int localPlayerTeam) const {
    int rot = getEffectiveFieldRotation(localPlayerTeam);
    if (rot == 0) return screenKeyMask;

    uint32_t out = screenKeyMask & ~15u;
    bool up    = (screenKeyMask & 1) != 0;
    bool down  = (screenKeyMask & 2) != 0;
    bool left  = (screenKeyMask & 4) != 0;
    bool right = (screenKeyMask & 8) != 0;

    if (rot == 1) {

        if (up)    out |= 8;
        if (down)  out |= 4;
        if (left)  out |= 1;
        if (right) out |= 2;
    } else if (rot == 2) {

        if (up)    out |= 2;
        if (down)  out |= 1;
        if (left)  out |= 8;
        if (right) out |= 4;
    } else if (rot == 3) {

        if (up)    out |= 4;
        if (down)  out |= 8;
        if (left)  out |= 2;
        if (right) out |= 1;
    }
    return out;
}

void GameRenderer::addChatMessage(const std::string& sender, const std::string& text, uint32_t color, int team, bool isAdmin) {
    std::lock_guard<std::mutex> lock(chatMutex);
    ChatMessage msg;
    msg.sender = sender;
    msg.text = text;
    msg.color = color;
    msg.senderTeam = team;
    msg.isAdmin = isAdmin;
    msg.isAnnouncement = false;
    msg.style = 0;
    msg.age = 0.0f;
    chatMessages.push_back(std::move(msg));
    if (chatMessages.size() > 120) chatMessages.pop_front();
    chatScrollOffset = 0;
}

void GameRenderer::addAnnouncement(const std::string& text, uint32_t color, uint8_t style) {
    std::lock_guard<std::mutex> lock(chatMutex);
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        std::string line = (nl == std::string::npos) ? text.substr(start) : text.substr(start, nl - start);
        if (!line.empty()) {
            ChatMessage msg;
            msg.sender = "";
            msg.text = std::move(line);
            msg.color = (color == 0xFFFFFFFFu || (int32_t)color < 0) ? 0xE2F1D5 : (color & 0xFFFFFFu);
            msg.isAnnouncement = true;
            msg.style = style;
            msg.age = 0.0f;
            chatMessages.push_back(std::move(msg));
            if (chatMessages.size() > 120) chatMessages.pop_front();
        }
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    chatScrollOffset = 0;
}

void GameRenderer::spawnKickRipple(const Vec2& pos, SDL_Color col) {
    KickRippleEffect rip;
    rip.worldPos = pos;
    rip.age = 0.0f;
    rip.maxAge = 0.42f;
    rip.color = col;
    kickRipples.push_back(rip);
    if (kickRipples.size() > 32) kickRipples.erase(kickRipples.begin());
}

void GameRenderer::update(double dt, int currentPingMs) {
    {
        std::lock_guard<std::mutex> lock(chatMutex);
        for (auto& m : chatMessages) {
            m.age += static_cast<float>(dt);
        }
    }
    if (bannerAnimTimer > 0.0) {
        bannerAnimTimer = std::max(0.0, bannerAnimTimer - dt);
    }
    pingSampleTimer += dt;
    if (pingSampleTimer >= 0.25) {
        pingSampleTimer = 0.0;
        pingHistory.push_back(currentPingMs);
        if (pingHistory.size() > 60) pingHistory.pop_front();
    }

    for (auto it = kickRipples.begin(); it != kickRipples.end();) {
        it->age += static_cast<float>(dt);
        if (it->age >= it->maxAge) it = kickRipples.erase(it);
        else ++it;
    }
}

Vec2 GameRenderer::worldToScreen(const Vec2& worldPos, int screenW, int screenH) const {
    double dx = (worldPos.x - cameraPos.x) * zoom;
    double dy = (worldPos.y - cameraPos.y) * zoom;
    double rx = dx;
    double ry = dy;
    switch (activeRotation) {
        case 1: rx =  dy; ry = -dx; break;
        case 2: rx = -dx; ry = -dy; break;
        case 3: rx = -dy; ry =  dx; break;
        default: break;
    }
    return Vec2(screenW * 0.5 + rx, screenH * 0.5 + ry);
}

Vec2 GameRenderer::screenToWorld(const Vec2& screenPos, int screenW, int screenH) const {
    double rx = (screenPos.x - screenW * 0.5) / zoom;
    double ry = (screenPos.y - screenH * 0.5) / zoom;
    double dx = rx;
    double dy = ry;
    switch (activeRotation) {
        case 1: dx = -ry; dy =  rx; break;
        case 2: dx = -rx; dy = -ry; break;
        case 3: dx =  ry; dy = -rx; break;
        default: break;
    }
    return Vec2(cameraPos.x + dx, cameraPos.y + dy);
}

static const std::vector<SDL_FPoint>& getUnitCircleLUT(int segments) {
    segments = std::clamp(segments, 12, 128);
    static std::vector<SDL_FPoint> table[129];
    auto& lut = table[segments];
    if (lut.empty()) {
        lut.resize(static_cast<size_t>(segments));
        const float step = (2.0f * static_cast<float>(M_PI)) / static_cast<float>(segments);
        for (int i = 0; i < segments; ++i) {
            float a = static_cast<float>(i) * step;
            lut[static_cast<size_t>(i)] = {std::cos(a), std::sin(a)};
        }
    }
    return lut;
}

void GameRenderer::drawAACircleFilled(SDL_Renderer* renderer, float cx, float cy, float radius, SDL_Color color, int segments) {
    if (radius <= 0.2f || !std::isfinite(radius)) return;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    segments = std::clamp(segments, 16, 128);
    const auto& lut = getUnitCircleLUT(segments);
    const float feather = 1.15f;
    float innerR = std::max(0.0f, radius - feather * 0.5f);
    float outerR = radius + feather * 0.5f;

    thread_local std::vector<SDL_Vertex> verts;
    thread_local std::vector<int> indices;
    verts.clear();
    indices.clear();
    verts.reserve(1 + segments * 2);
    indices.reserve(segments * 9);

    SDL_Color transColor = {color.r, color.g, color.b, 0};
    verts.push_back({{cx, cy}, color, {0.0f, 0.0f}});

    for (int i = 0; i < segments; ++i) {
        float cs = lut[static_cast<size_t>(i)].x;
        float sn = lut[static_cast<size_t>(i)].y;
        verts.push_back({{cx + cs * innerR, cy + sn * innerR}, color, {0.0f, 0.0f}});
        verts.push_back({{cx + cs * outerR, cy + sn * outerR}, transColor, {0.0f, 0.0f}});
    }

    for (int i = 0; i < segments; ++i) {
        int next = (i + 1 == segments) ? 0 : (i + 1);
        int in0 = 1 + i * 2;
        int out0 = 2 + i * 2;
        int in1 = 1 + next * 2;
        int out1 = 2 + next * 2;

        indices.push_back(0);
        indices.push_back(in0);
        indices.push_back(in1);

        indices.push_back(in0);
        indices.push_back(out0);
        indices.push_back(in1);

        indices.push_back(in1);
        indices.push_back(out0);
        indices.push_back(out1);
    }

    SDL_RenderGeometry(renderer, nullptr, verts.data(), (int)verts.size(), indices.data(), (int)indices.size());
}

void GameRenderer::drawAARing(SDL_Renderer* renderer, float cx, float cy, float radius, float thickness, SDL_Color color, int segments) {
    if (radius <= 0.2f || thickness <= 0.1f || !std::isfinite(radius)) return;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    segments = std::clamp(segments, 20, 128);
    const auto& lut = getUnitCircleLUT(segments);
    const float feather = 1.0f;
    float halfT = std::max(0.5f, thickness * 0.5f);
    float r0 = std::max(0.0f, radius - halfT - feather);
    float r1 = std::max(0.0f, radius - halfT);
    float r2 = radius + halfT;
    float r3 = radius + halfT + feather;

    SDL_Color transColor = {color.r, color.g, color.b, 0};
    thread_local std::vector<SDL_Vertex> verts;
    thread_local std::vector<int> indices;
    verts.clear();
    indices.clear();
    verts.reserve(segments * 4);
    indices.reserve(segments * 18);

    for (int i = 0; i < segments; ++i) {
        float cs = lut[static_cast<size_t>(i)].x;
        float sn = lut[static_cast<size_t>(i)].y;
        verts.push_back({{cx + cs * r0, cy + sn * r0}, transColor, {0.0f, 0.0f}});
        verts.push_back({{cx + cs * r1, cy + sn * r1}, color, {0.0f, 0.0f}});
        verts.push_back({{cx + cs * r2, cy + sn * r2}, color, {0.0f, 0.0f}});
        verts.push_back({{cx + cs * r3, cy + sn * r3}, transColor, {0.0f, 0.0f}});
    }

    auto addQuad = [&](int a0, int b0, int a1, int b1) {
        indices.push_back(a0); indices.push_back(b0); indices.push_back(a1);
        indices.push_back(a1); indices.push_back(b0); indices.push_back(b1);
    };

    for (int i = 0; i < segments; ++i) {
        int next = (i + 1 == segments) ? 0 : (i + 1);
        int b0 = i * 4;
        int b1 = next * 4;
        addQuad(b0 + 0, b0 + 1, b1 + 0, b1 + 1);
        addQuad(b0 + 1, b0 + 2, b1 + 1, b1 + 2);
        addQuad(b0 + 2, b0 + 3, b1 + 2, b1 + 3);
    }

    SDL_RenderGeometry(renderer, nullptr, verts.data(), (int)verts.size(), indices.data(), (int)indices.size());
}

void GameRenderer::drawAALine(SDL_Renderer* renderer, float x0, float y0, float x1, float y1, float thickness, SDL_Color color) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.1f) return;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    float nx = -dy / len;
    float ny = dx / len;
    float halfT = std::max(0.5f, thickness * 0.5f);
    float feather = 1.0f;

    SDL_Color trans = {color.r, color.g, color.b, 0};

    SDL_Vertex verts[8] = {
        {{x0 + nx * (halfT + feather), y0 + ny * (halfT + feather)}, trans, {0, 0}},
        {{x0 + nx * halfT,             y0 + ny * halfT},             color, {0, 0}},
        {{x0 - nx * halfT,             y0 - ny * halfT},             color, {0, 0}},
        {{x0 - nx * (halfT + feather), y0 - ny * (halfT + feather)}, trans, {0, 0}},

        {{x1 + nx * (halfT + feather), y1 + ny * (halfT + feather)}, trans, {0, 0}},
        {{x1 + nx * halfT,             y1 + ny * halfT},             color, {0, 0}},
        {{x1 - nx * halfT,             y1 - ny * halfT},             color, {0, 0}},
        {{x1 - nx * (halfT + feather), y1 - ny * (halfT + feather)}, trans, {0, 0}},
    };

    int indices[18] = {
        0, 1, 4,  4, 1, 5,
        1, 2, 5,  5, 2, 6,
        2, 3, 6,  6, 3, 7
    };

    SDL_RenderGeometry(renderer, nullptr, verts, 8, indices, 18);
}

void GameRenderer::drawAAArc(SDL_Renderer* renderer, float cx, float cy, float radius, float startAngle, float endAngle, bool increasingAngle, float thickness, SDL_Color color) {
    if (radius <= 0.5f || !std::isfinite(radius) || !std::isfinite(startAngle) || !std::isfinite(endAngle)) return;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    constexpr float kTwoPi = 2.0f * static_cast<float>(M_PI);
    float a0 = startAngle;
    float a1 = endAngle;
    if (increasingAngle) {
        if (a1 < a0) a1 += kTwoPi * std::ceil((a0 - a1) / kTwoPi);
    } else {
        if (a1 > a0) a1 -= kTwoPi * std::ceil((a1 - a0) / kTwoPi);
    }

    float sweep = std::abs(a1 - a0);
    int steps = std::clamp(static_cast<int>(sweep * std::sqrt(radius) * 1.8f), 12, 96);

    float halfT = std::max(0.5f, thickness * 0.5f);
    float feather = 1.0f;
    float r0 = std::max(0.0f, radius - halfT - feather);
    float r1 = std::max(0.0f, radius - halfT);
    float r2 = radius + halfT;
    float r3 = radius + halfT + feather;

    SDL_Color trans = {color.r, color.g, color.b, 0};
    thread_local std::vector<SDL_Vertex> verts;
    thread_local std::vector<int> indices;
    verts.clear();
    indices.clear();
    verts.reserve((steps + 1) * 4);
    indices.reserve(steps * 18);

    const float da = (a1 - a0) / static_cast<float>(steps);
    for (int i = 0; i <= steps; ++i) {
        float a = a0 + da * static_cast<float>(i);
        float cs = std::cos(a);
        float sn = std::sin(a);
        verts.push_back({{cx + cs * r0, cy + sn * r0}, trans, {0, 0}});
        verts.push_back({{cx + cs * r1, cy + sn * r1}, color, {0, 0}});
        verts.push_back({{cx + cs * r2, cy + sn * r2}, color, {0, 0}});
        verts.push_back({{cx + cs * r3, cy + sn * r3}, trans, {0, 0}});
    }

    auto addQuad = [&](int vA0, int vB0, int vA1, int vB1) {
        indices.push_back(vA0); indices.push_back(vB0); indices.push_back(vA1);
        indices.push_back(vA1); indices.push_back(vB0); indices.push_back(vB1);
    };

    for (int i = 0; i < steps; ++i) {
        int b0 = i * 4;
        int b1 = (i + 1) * 4;
        addQuad(b0 + 0, b0 + 1, b1 + 0, b1 + 1);
        addQuad(b0 + 1, b0 + 2, b1 + 1, b1 + 2);
        addQuad(b0 + 2, b0 + 3, b1 + 2, b1 + 3);
    }

    SDL_RenderGeometry(renderer, nullptr, verts.data(), (int)verts.size(), indices.data(), (int)indices.size());
}

void GameRenderer::drawRoundedRect(SDL_Renderer* renderer, float x, float y, float w, float h, float r, SDL_Color fillCol, SDL_Color borderCol, float borderWidth) {
    if (w <= 0.0f || h <= 0.0f) return;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    r = std::clamp(r, 0.0f, std::min(w, h) * 0.5f);

    if (r <= 0.5f && (borderCol.a == 0 || borderWidth <= 0.1f)) {
        if (fillCol.a > 0) {
            SDL_Vertex v[4] = {
                {{x,     y},     fillCol, {0, 0}},
                {{x + w, y},     fillCol, {0, 0}},
                {{x + w, y + h}, fillCol, {0, 0}},
                {{x,     y + h}, fillCol, {0, 0}}
            };
            int idx[6] = {0, 1, 2, 0, 2, 3};
            SDL_RenderGeometry(renderer, nullptr, v, 4, idx, 6);
        }
        return;
    }

    const int cornerSegs = (r > 1.0f) ? 8 : 1;
    thread_local std::vector<SDL_FPoint> perimeter;
    perimeter.clear();
    perimeter.reserve(cornerSegs * 4 + 4);

    static const float qCos[9] = {
        1.0000000f, 0.9807853f, 0.9238795f, 0.8314696f, 0.7071068f,
        0.5555702f, 0.3826834f, 0.1950903f, 0.0000000f
    };
    static const float qSin[9] = {
        0.0000000f, 0.1950903f, 0.3826834f, 0.5555702f, 0.7071068f,
        0.8314696f, 0.9238795f, 0.9807853f, 1.0000000f
    };

    if (r > 0.5f) {
        if (cornerSegs == 8) {

            for (int i = 0; i <= 8; ++i) perimeter.push_back({x + w - r + qSin[i] * r, y + r - qCos[i] * r});

            for (int i = 0; i <= 8; ++i) perimeter.push_back({x + w - r + qCos[i] * r, y + h - r + qSin[i] * r});

            for (int i = 0; i <= 8; ++i) perimeter.push_back({x + r - qSin[i] * r, y + h - r + qCos[i] * r});

            for (int i = 0; i <= 8; ++i) perimeter.push_back({x + r - qCos[i] * r, y + r - qSin[i] * r});
        } else {
            perimeter.push_back({x + w - r, y});
            perimeter.push_back({x + w, y + r});
            perimeter.push_back({x + w, y + h - r});
            perimeter.push_back({x + w - r, y + h});
            perimeter.push_back({x + r, y + h});
            perimeter.push_back({x, y + h - r});
            perimeter.push_back({x, y + r});
            perimeter.push_back({x + r, y});
        }
    } else {
        perimeter.push_back({x, y});
        perimeter.push_back({x + w, y});
        perimeter.push_back({x + w, y + h});
        perimeter.push_back({x, y + h});
    }

    int n = (int)perimeter.size();
    thread_local std::vector<SDL_Vertex> verts;
    thread_local std::vector<int> indices;

    if (fillCol.a > 0) {
        verts.clear();
        indices.clear();
        verts.reserve(n + 1);
        indices.reserve(n * 3);
        verts.push_back({{x + w * 0.5f, y + h * 0.5f}, fillCol, {0, 0}});
        for (int i = 0; i < n; ++i) {
            verts.push_back({perimeter[i], fillCol, {0, 0}});
        }
        for (int i = 0; i < n; ++i) {
            indices.push_back(0);
            indices.push_back(1 + i);
            indices.push_back(1 + ((i + 1 == n) ? 0 : (i + 1)));
        }
        SDL_RenderGeometry(renderer, nullptr, verts.data(), (int)verts.size(), indices.data(), (int)indices.size());
    }

    if (borderCol.a > 0 && borderWidth > 0.1f) {
        verts.clear();
        indices.clear();
        verts.reserve(n * 8);
        indices.reserve(n * 18);
        const float halfT = std::max(0.5f, borderWidth * 0.5f);
        const float feather = 1.0f;
        const SDL_Color trans = {borderCol.r, borderCol.g, borderCol.b, 0};

        for (int i = 0; i < n; ++i) {
            const auto& p0 = perimeter[i];
            const auto& p1 = perimeter[(i + 1 == n) ? 0 : (i + 1)];
            float dx = p1.x - p0.x;
            float dy = p1.y - p0.y;
            float len = std::sqrt(dx * dx + dy * dy);
            if (len < 0.1f) continue;

            float nx = -dy / len;
            float ny = dx / len;
            int b = static_cast<int>(verts.size());

            verts.push_back({{p0.x + nx * (halfT + feather), p0.y + ny * (halfT + feather)}, trans,     {0, 0}});
            verts.push_back({{p0.x + nx * halfT,             p0.y + ny * halfT},             borderCol, {0, 0}});
            verts.push_back({{p0.x - nx * halfT,             p0.y - ny * halfT},             borderCol, {0, 0}});
            verts.push_back({{p0.x - nx * (halfT + feather), p0.y - ny * (halfT + feather)}, trans,     {0, 0}});

            verts.push_back({{p1.x + nx * (halfT + feather), p1.y + ny * (halfT + feather)}, trans,     {0, 0}});
            verts.push_back({{p1.x + nx * halfT,             p1.y + ny * halfT},             borderCol, {0, 0}});
            verts.push_back({{p1.x - nx * halfT,             p1.y - ny * halfT},             borderCol, {0, 0}});
            verts.push_back({{p1.x - nx * (halfT + feather), p1.y - ny * (halfT + feather)}, trans,     {0, 0}});

            indices.push_back(b + 0); indices.push_back(b + 1); indices.push_back(b + 4);
            indices.push_back(b + 4); indices.push_back(b + 1); indices.push_back(b + 5);
            indices.push_back(b + 1); indices.push_back(b + 2); indices.push_back(b + 5);
            indices.push_back(b + 5); indices.push_back(b + 2); indices.push_back(b + 6);
            indices.push_back(b + 2); indices.push_back(b + 3); indices.push_back(b + 6);
            indices.push_back(b + 6); indices.push_back(b + 3); indices.push_back(b + 7);
        }
        if (!verts.empty()) {
            SDL_RenderGeometry(renderer, nullptr, verts.data(), (int)verts.size(), indices.data(), (int)indices.size());
        }
    }
}

void GameRenderer::drawStripedPlayerDisc(SDL_Renderer* renderer, float cx, float cy, float radius, const TeamColors& tc, uint32_t fallbackColor) {
    if (tc.colors.empty()) {
        drawAACircleFilled(renderer, cx, cy, radius, u32ToColor(fallbackColor));
        return;
    }
    if (tc.colors.size() == 1) {
        drawAACircleFilled(renderer, cx, cy, radius, u32ToColor((uint32_t)tc.colors[0]));
        return;
    }

    int numStripes = (int)tc.colors.size();
    float angleRad = static_cast<float>(M_PI) * static_cast<float>(tc.angle) / 128.0f + getRotationRadians();
    float cs = std::cos(angleRad);
    float sn = std::sin(angleRad);

    auto toScreen = [&](float u, float v) -> SDL_FPoint {
        return {
            cx + u * cs - v * sn,
            cy + u * sn + v * cs
        };
    };

    constexpr int kSlices = 12;
    SDL_Vertex verts[(kSlices + 1) * 2];
    int indices[kSlices * 6];
    for (int i = 0; i < kSlices; ++i) {
        int b0 = i * 2;
        int b1 = (i + 1) * 2;
        indices[i * 6 + 0] = b0; indices[i * 6 + 1] = b0 + 1; indices[i * 6 + 2] = b1;
        indices[i * 6 + 3] = b1; indices[i * 6 + 4] = b0 + 1; indices[i * 6 + 5] = b1 + 1;
    }

    const float rSq = radius * radius;
    for (int s = 0; s < numStripes; ++s) {
        SDL_Color col = u32ToColor((uint32_t)tc.colors[s]);
        float uStart = -radius + (2.0f * radius * s) / numStripes;
        float uEnd   = -radius + (2.0f * radius * (s + 1)) / numStripes;

        for (int i = 0; i <= kSlices; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(kSlices);
            float u = std::clamp(uStart + (uEnd - uStart) * t, -radius, radius);
            float vMax = std::sqrt(std::max(0.0f, rSq - u * u));
            verts[i * 2 + 0] = {toScreen(u, -vMax), col, {0, 0}};
            verts[i * 2 + 1] = {toScreen(u,  vMax), col, {0, 0}};
        }

        SDL_RenderGeometry(renderer, nullptr, verts, (kSlices + 1) * 2, indices, kSlices * 6);
    }
}

void GameRenderer::updateCamera(const World& world, uint32_t myPlayerId, int screenW, int screenH, double dt) {
    if (!world.stadium || !world.hasMatch) {
        cameraPos = Vec2(0.0, 0.0);
        return;
    }

    const auto& st = *world.stadium;

    bool isVert = (activeRotation == 1 || activeRotation == 3);
    double viewW = (isVert ? screenH : screenW) / zoom;
    double viewH = (isVert ? screenW : screenH) / zoom;
    double topPad = 50.0 / zoom;

    const Disc* followDisc = nullptr;
    for (const auto& p : world.players) {
        if (p.id == myPlayerId && p.discIndex >= 0 && p.discIndex < (int)world.discs.size()) {
            followDisc = &world.discs[p.discIndex];
            break;
        }
    }
    const Disc* ballDisc = (!world.discs.empty()) ? &world.discs[0] : nullptr;

    if (settings.cameraMode == 3) {
        cameraPos = Vec2(0.0, isVert ? 0.0 : -topPad * 0.5);
        return;
    }

    Vec2 target{0.0, 0.0};
    if (settings.cameraMode == 1 && followDisc) {
        target = followDisc->pos;
    } else if (settings.cameraMode == 2 && ballDisc) {
        target = ballDisc->pos;
    } else {
        if (followDisc && st.cameraFollow == 1) {
            target = followDisc->pos;
        } else if (ballDisc) {
            target = ballDisc->pos;
            if (followDisc) {
                Vec2 pPos = followDisc->pos;
                target.x = 0.5 * (target.x + pPos.x);
                target.y = 0.5 * (target.y + pPos.y);
                double halfW = 0.5 * viewW;
                double halfH = 0.5 * viewH;
                double minX = pPos.x - halfW + 50.0;
                double maxX = pPos.x + halfW - 50.0;
                double minY = pPos.y - halfH + 50.0;
                double maxY = pPos.y + halfH - 50.0;
                if (minX <= maxX) target.x = std::clamp(target.x, minX, maxX);
                if (minY <= maxY) target.y = std::clamp(target.y, minY, maxY);
            }
        }
    }

    double smoothFactor = std::clamp(static_cast<double>(settings.cameraSmoothness), 0.05, 1.0);
    if (smoothFactor >= 0.98) {
        cameraPos = target;
    } else {
        double t = std::min(1.0, 60.0 * dt * smoothFactor);
        cameraPos.x += (target.x - cameraPos.x) * t;
        cameraPos.y += (target.y - cameraPos.y) * t;
    }

    if (viewW > 2.0 * st.width) {
        cameraPos.x = 0.0;
    } else if (cameraPos.x + 0.5 * viewW > st.width) {
        cameraPos.x = st.width - 0.5 * viewW;
    } else if (cameraPos.x - 0.5 * viewW < -st.width) {
        cameraPos.x = -st.width + 0.5 * viewW;
    }

    if (viewH > 2.0 * st.height) {
        cameraPos.y = 0.0;
    } else if (cameraPos.y + 0.5 * viewH > st.height) {
        cameraPos.y = st.height - 0.5 * viewH;
    } else if (cameraPos.y - 0.5 * viewH < -st.height) {
        cameraPos.y = -st.height + 0.5 * viewH;
    }

    if (std::isnan(cameraPos.x)) cameraPos.x = 0.0;
    if (std::isnan(cameraPos.y)) cameraPos.y = 0.0;
}

void GameRenderer::renderWorld(SDL_Renderer* renderer, const World& world, uint32_t myPlayerId, int pingMs, int maxPingMs, int fps, double dt, bool isSandbox, const World* realWorld) {
    int screenW = 1280;
    int screenH = 720;
    SDL_GetRendererOutputSize(renderer, &screenW, &screenH);

    int myTeam = 1;
    if (const auto* me = world.getPlayer(myPlayerId)) {
        if (me->team == 1 || me->team == 2) myTeam = me->team;
    }
    activeRotation = getEffectiveFieldRotation(myTeam);

    if (world.hasMatch && !world.discs.empty()) {
        const auto& ball = world.discs[0];

        for (const auto& pl : world.players) {
            if (pl.discIndex >= 0 && pl.discIndex < (int)world.discs.size()) {
                const auto& pDisc = world.discs[pl.discIndex];
                double dx = pDisc.pos.x - ball.pos.x;
                double dy = pDisc.pos.y - ball.pos.y;
                double thresh = pDisc.radius + ball.radius + 4.5;
                if (dx * dx + dy * dy <= thresh * thresh) {
                    if (pl.team == 1 || pl.team == 2) {
                        lastBallTouchTeam = pl.team;
                    }
                }
            }
        }

        ballTrailTimer += dt;
        if (ballTrailTimer >= 0.012) {
            ballTrailTimer = 0.0;
            ballTrailPositions.push_back(world.discs[0].pos);
            if (ballTrailPositions.size() > 36) ballTrailPositions.pop_front();
        }
    } else {
        ballTrailPositions.clear();
        lastBallTouchTeam = 0;
    }

    if (world.matchState != prevMatchState) {
        if (world.matchState == 2) {
            bannerAnimTimer = 2.3;
            int scoringTeam = (world.goalConcedingTeam == 1) ? 2 : 1;
            if (scoringTeam == 2) {
                bannerLine1 = "BLUE";
                bannerLine2 = "SCORES! ⚽";
                bannerColor = {86, 137, 229, 255};
            } else {
                bannerLine1 = "RED";
                bannerLine2 = "SCORES! ⚽";
                bannerColor = {229, 110, 86, 255};
            }
        } else if (world.matchState == 3) {
            bannerAnimTimer = 3.5;
            if (world.redScore > world.blueScore) {
                bannerLine1 = "RED IS";
                bannerLine2 = "VICTORIOUS! 🏆";
                bannerColor = {229, 110, 86, 255};
            } else {
                bannerLine1 = "BLUE IS";
                bannerLine2 = "VICTORIOUS! 🏆";
                bannerColor = {86, 137, 229, 255};
            }
        }
        prevMatchState = world.matchState;
    }

    updateCamera(world, myPlayerId, screenW, screenH, dt);

    uint32_t bgCol = world.stadium ? world.stadium->bgColor : 0x718C5A;
    if ((int32_t)bgCol < 0) bgCol = 0x718C5A;
    if (settings.pitchTheme == 1) bgCol = 0x678252;
    else if (settings.pitchTheme == 2) bgCol = 0x4A5462;
    else if (settings.pitchTheme == 3) bgCol = 0x0C101C;
    else if (settings.pitchTheme == 4) bgCol = 0x262B34;
    else if (settings.pitchTheme == 5) bgCol = 0x241E14;

    SDL_Color clearCol = u32ToColor(bgCol);
    SDL_SetRenderDrawColor(renderer, clearCol.r, clearCol.g, clearCol.b, 255);
    SDL_RenderClear(renderer);

    if (world.stadium && world.hasMatch) {
        drawPitch(renderer, *world.stadium, screenW, screenH);
        drawFunEffectsUnder(renderer, world, screenW, screenH);
        drawJoints(renderer, world, screenW, screenH);
        drawTacticalOverlay(renderer, world, myPlayerId, screenW, screenH);
        drawDiscs(renderer, world, myPlayerId, screenW, screenH);

        if (settings.showDesyncGhost && realWorld && !realWorld->discs.empty()) {
            if (const auto* me = realWorld->getPlayer(myPlayerId)) {
                if (me->discIndex >= 0 && me->discIndex < (int)realWorld->discs.size()) {
                    const auto& realDisc = realWorld->discs[me->discIndex];
                    Vec2 gsp = worldToScreen(realDisc.pos, screenW, screenH);
                    float gRadius = static_cast<float>(realDisc.radius * zoom);

                    SDL_Color ghostFill = {245, 158, 11, 45};
                    SDL_Color ghostBorder = {245, 158, 11, 220};
                    drawAACircleFilled(renderer, (float)gsp.x, (float)gsp.y, gRadius, ghostFill, 32);
                    drawAARing(renderer, (float)gsp.x, (float)gsp.y, gRadius, 1.8f * zoom, ghostBorder, 32);
                }
            }
        }

        drawLocationIndicators(renderer, world, myPlayerId, screenW, screenH);
    }

    drawHUD(renderer, world, myPlayerId, pingMs, maxPingMs, fps, screenW, screenH);
    drawChat(renderer, screenW, screenH);

    if (!world.hasMatch || showRoomMenu) {
        drawRoomLobbyModal(renderer, world, myPlayerId, screenW, screenH, isSandbox);
    }
    if (showSettingsPanel && !useImGuiDashboard) {
        drawUnifiedDashboardModal(renderer, world, screenW, screenH);
    }
}

void GameRenderer::drawPitch(SDL_Renderer* renderer, const Stadium& stadium, int screenW, int screenH) {
    float lineW = std::max(1.8f, 3.0f * zoom);
    float rotRad = getRotationRadians();

    double bgW = stadium.bgWidth > 10.0 ? stadium.bgWidth : stadium.width;
    double bgH = stadium.bgHeight > 10.0 ? stadium.bgHeight : stadium.height;

    int effTheme = settings.pitchTheme;
    if (effTheme == 0) {
        effTheme = (stadium.bgType == 2) ? 2 : 1;
    }

    if ((stadium.bgType >= 1 || settings.pitchTheme > 0) && bgW > 0.0 && bgH > 0.0) {
        Vec2 c0 = worldToScreen(Vec2(-bgW, -bgH), screenW, screenH);
        Vec2 c1 = worldToScreen(Vec2( bgW,  bgH), screenW, screenH);
        float minX = static_cast<float>(std::min(c0.x, c1.x));
        float maxX = static_cast<float>(std::max(c0.x, c1.x));
        float minY = static_cast<float>(std::min(c0.y, c1.y));
        float maxY = static_cast<float>(std::max(c0.y, c1.y));
        float pw = maxX - minX;
        float ph = maxY - minY;
        float cr = static_cast<float>(stadium.bgCornerRadius * zoom);

        SDL_Color turfFill = u32ToColor(0x718C5A);
        SDL_Color stripeAlt = u32ToColor(0x698253);
        SDL_Color lineCol = u32ToColor(0xC7E6BD);

        if (effTheme == 2) {
            turfFill = {205, 222, 238, 255};
            stripeAlt = {196, 215, 232, 255};
            lineCol = u32ToColor(0xE9CC6E);
        } else if (effTheme == 3) {
            turfFill = {16, 22, 38, 255};
            stripeAlt = {20, 28, 48, 255};
            lineCol = {0, 235, 255, 215};
        } else if (effTheme == 4) {
            turfFill = {46, 52, 64, 255};
            stripeAlt = {52, 58, 72, 255};
            lineCol = {245, 195, 75, 230};
        } else if (effTheme == 5) {
            turfFill = {44, 62, 46, 255};
            stripeAlt = {38, 55, 40, 255};
            lineCol = {255, 215, 95, 240};
        }

        drawRoundedRect(renderer, minX, minY, pw, ph, cr, turfFill);

        constexpr int kNumStripes = 12;
        for (int s = 0; s < kNumStripes; s += 2) {
            double xA = -bgW + (2.0 * bgW * s) / kNumStripes;
            double xB = -bgW + (2.0 * bgW * (s + 1)) / kNumStripes;
            Vec2 s0 = worldToScreen(Vec2(xA, -bgH), screenW, screenH);
            Vec2 s1 = worldToScreen(Vec2(xB,  bgH), screenW, screenH);
            float sx = static_cast<float>(std::min(s0.x, s1.x));
            float sy = static_cast<float>(std::min(s0.y, s1.y));
            float sw = static_cast<float>(std::abs(s1.x - s0.x));
            float sh = static_cast<float>(std::abs(s1.y - s0.y));
            drawRoundedRect(renderer, sx, sy, sw, sh, 0.0f, stripeAlt);
        }

        drawRoundedRect(renderer, minX, minY, pw, ph, cr, {0, 0, 0, 0}, lineCol, lineW);

        Vec2 cTop = worldToScreen(Vec2(0.0, -bgH), screenW, screenH);
        Vec2 cBot = worldToScreen(Vec2(0.0,  bgH), screenW, screenH);
        drawAALine(renderer, (float)cTop.x, (float)cTop.y, (float)cBot.x, (float)cBot.y, lineW, lineCol);

        double koRad = stadium.bgKickOffRadius > 1.0 ? stadium.bgKickOffRadius : 75.0;
        Vec2 center = worldToScreen(Vec2(0.0, 0.0), screenW, screenH);
        drawAARing(renderer, (float)center.x, (float)center.y, (float)(koRad * zoom), lineW, lineCol, 48);
        drawAACircleFilled(renderer, (float)center.x, (float)center.y, std::max(3.0f, 4.5f * zoom), lineCol, 20);
    }

    for (const auto& seg : stadium.segments) {
        if (!seg.visible && !settings.showInvisibleSegments) continue;
        if (seg.color == -1) continue;
        if (seg.v0 < 0 || seg.v0 >= (int)stadium.vertices.size() ||
            seg.v1 < 0 || seg.v1 >= (int)stadium.vertices.size()) continue;

        const auto& p0 = stadium.vertices[seg.v0].pos;
        const auto& p1 = stadium.vertices[seg.v1].pos;
        SDL_Color col = u32ToColor((uint32_t)seg.color, seg.visible ? 255 : 110);
        if (settings.pitchTheme == 3 && seg.visible && seg.color == 0xC7E6BD) {
            col = {0, 235, 255, 230};
        }

        if (seg.isArc && std::isfinite(seg.curveF) && seg.arcRadius > 0.5) {
            Vec2 sc = worldToScreen(seg.center, screenW, screenH);
            float sr = static_cast<float>(seg.arcRadius * zoom);
            float a0 = static_cast<float>(std::atan2(p0.y - seg.center.y, p0.x - seg.center.x)) + rotRad;
            float a1 = static_cast<float>(std::atan2(p1.y - seg.center.y, p1.x - seg.center.x)) + rotRad;
            drawAAArc(renderer, (float)sc.x, (float)sc.y, sr, a0, a1, seg.curveF > 0.0, lineW, col);
        } else {
            Vec2 sp0 = worldToScreen(p0, screenW, screenH);
            Vec2 sp1 = worldToScreen(p1, screenW, screenH);
            drawAALine(renderer, (float)sp0.x, (float)sp0.y, (float)sp1.x, (float)sp1.y, lineW, col);
        }
    }
}

void GameRenderer::drawFunEffectsUnder(SDL_Renderer* renderer, const World& world, int screenW, int screenH) {

    if (settings.ballTrailStyle > 0 && ballTrailPositions.size() >= 2 && !world.discs.empty()) {
        float baseRadius = static_cast<float>(world.discs[0].radius * zoom);
        size_t n = ballTrailPositions.size();
        for (size_t i = 0; i < n; ++i) {
            float frac = static_cast<float>(i + 1) / static_cast<float>(n);
            uint8_t alpha = static_cast<uint8_t>(frac * frac * settings.ballTrailOpacity * 175.0f);
            if (alpha < 4) continue;

            Vec2 sp = worldToScreen(ballTrailPositions[i], screenW, screenH);
            float r = std::max(1.5f, baseRadius * (0.25f + 0.68f * frac));

            SDL_Color c{0, 235, 255, alpha};
            if (settings.ballTrailStyle == 1) {
                if (lastBallTouchTeam == 1) {
                    c = {248, 113, 113, alpha};
                } else if (lastBallTouchTeam == 2) {
                    c = {96, 165, 250, alpha};
                } else {
                    c = {250, 204, 21, alpha};
                }
            } else if (settings.ballTrailStyle == 2) {
                if (lastBallTouchTeam == 1) {

                    c = {255, static_cast<uint8_t>(50 + frac * 45), static_cast<uint8_t>(75 + frac * 35), alpha};
                } else if (lastBallTouchTeam == 2) {

                    c = {static_cast<uint8_t>(40 + frac * 40), static_cast<uint8_t>(160 + frac * 85), 255, alpha};
                } else {
                    c = {0, 235, 255, alpha};
                }
            } else if (settings.ballTrailStyle == 3) {
                if (lastBallTouchTeam == 1) {

                    c = {255, static_cast<uint8_t>(20 + frac * 80), static_cast<uint8_t>(20 + frac * 40), alpha};
                } else if (lastBallTouchTeam == 2) {

                    c = {static_cast<uint8_t>(30 + frac * 70), static_cast<uint8_t>(130 + frac * 115), 255, alpha};
                } else {

                    c = {255, static_cast<uint8_t>(60 + frac * 175), static_cast<uint8_t>(20 + frac * 40), alpha};
                }
            } else if (settings.ballTrailStyle == 4) {
                c = {255, 255, 255, alpha};
            } else if (settings.ballTrailStyle == 5) {
                float hue = static_cast<float>((i * 24) % 360);
                uint8_t rr = static_cast<uint8_t>(127 + 120 * std::sin(hue * 0.01745f));
                uint8_t gg = static_cast<uint8_t>(127 + 120 * std::sin((hue + 120.0f) * 0.01745f));
                uint8_t bb = static_cast<uint8_t>(127 + 120 * std::sin((hue + 240.0f) * 0.01745f));
                c = {rr, gg, bb, alpha};
            }
            drawAACircleFilled(renderer, (float)sp.x, (float)sp.y, r, c, 20);
        }
    }

    for (const auto& rip : kickRipples) {
        float t = std::clamp(rip.age / rip.maxAge, 0.0f, 1.0f);
        float radius = (14.0f + t * 38.0f) * zoom;
        uint8_t alpha = static_cast<uint8_t>((1.0f - t) * rip.color.a);
        Vec2 sp = worldToScreen(rip.worldPos, screenW, screenH);
        SDL_Color col = rip.color;
        col.a = alpha;
        drawAARing(renderer, (float)sp.x, (float)sp.y, radius, std::max(1.5f, (1.0f - t * 0.5f) * 3.2f * zoom), col, 32);
    }
}

void GameRenderer::drawJoints(SDL_Renderer* renderer, const World& world, int screenW, int screenH) {
    if (!world.stadium) return;
    float lineW = std::max(1.8f, 3.0f * zoom);
    for (const auto& j : world.stadium->joints) {
        if (j.color == -1) continue;
        if (j.d0 >= world.discs.size() || j.d1 >= world.discs.size()) continue;
        Vec2 p0 = worldToScreen(world.discs[j.d0].pos, screenW, screenH);
        Vec2 p1 = worldToScreen(world.discs[j.d1].pos, screenW, screenH);
        drawAALine(renderer, (float)p0.x, (float)p0.y, (float)p1.x, (float)p1.y, lineW, u32ToColor((uint32_t)j.color));
    }
}

void GameRenderer::drawTacticalOverlay(SDL_Renderer* renderer, const World& world, uint32_t myPlayerId, int screenW, int screenH) {
    if (!world.hasMatch || world.discs.empty()) return;

    float opacity = std::clamp(assistant.lineOpacity, 0.10f, 1.0f);
    float baseWidth = std::clamp(assistant.lineThickness, 1.0f, 5.0f) * zoom;

    auto drawPolyline = [&](const std::vector<Vec2>& pts, float width, SDL_Color col, bool dashed = false) {
        if (pts.size() < 2) return;
        size_t n = pts.size();
        for (size_t i = 1; i < n; ++i) {
            if (dashed && (i & 1)) continue;
            float fade = 1.0f - 0.55f * (static_cast<float>(i - 1) / static_cast<float>(n));
            SDL_Color segCol = col;
            segCol.a = static_cast<uint8_t>(col.a * fade);
            Vec2 a = worldToScreen(pts[i - 1], screenW, screenH);
            Vec2 b = worldToScreen(pts[i], screenW, screenH);
            drawAALine(renderer, (float)a.x, (float)a.y, (float)b.x, (float)b.y, width, segCol);
        }
    };

    if ((assistant.showGkDangerCone || assistant.showOtherGoalsCone) &&
        world.stadium && !world.stadium->goals.empty()) {
        const auto& goals = world.stadium->goals;
        const Vec2 ballPos = world.discs[0].pos;
        const Vec2 ballScr = worldToScreen(ballPos, screenW, screenH);

        bool isMyGoalStack[64] = {};
        std::vector<uint8_t> isMyGoalHeap;
        auto getIsMyGoal = [&](size_t idx) -> bool {
            return (goals.size() <= 64) ? isMyGoalStack[idx] : (isMyGoalHeap[idx] != 0);
        };
        auto setIsMyGoal = [&](size_t idx) {
            if (goals.size() <= 64) isMyGoalStack[idx] = true;
            else isMyGoalHeap[idx] = 1;
        };
        if (goals.size() > 64) isMyGoalHeap.assign(goals.size(), 0);
        bool anyMyGoal = false;
        bool usingExcludedColor = false;

        if (assistant.autoGoalExcludeColor >= 0) {
            uint32_t excl = static_cast<uint32_t>(assistant.autoGoalExcludeColor);
            for (size_t i = 0; i < goals.size(); ++i) {
                if (TacticalAssistant::getGoalColor(*world.stadium, goals[i]) == excl) {
                    setIsMyGoal(i);
                    anyMyGoal = true;
                    usingExcludedColor = true;
                }
            }
        }

        const Player* localP = world.getPlayer(myPlayerId);
        int myTeam = localP ? localP->team : 0;
        if (!anyMyGoal && (myTeam == 1 || myTeam == 2)) {
            int teamMatches = 0;
            for (const auto& g : goals) {
                if (g.team == myTeam) ++teamMatches;
            }
            if (teamMatches == 1 || goals.size() <= 2) {
                for (size_t i = 0; i < goals.size(); ++i) {
                    if (goals[i].team == myTeam) {
                        setIsMyGoal(i);
                        anyMyGoal = true;
                    }
                }
                if (!anyMyGoal && goals.size() <= 2) {
                    for (size_t i = 0; i < goals.size(); ++i) {
                        double gx = (goals[i].p0.x + goals[i].p1.x) * 0.5;
                        if ((myTeam == 1 && gx < 0.0) || (myTeam == 2 && gx > 0.0)) {
                            setIsMyGoal(i);
                            anyMyGoal = true;
                        }
                    }
                }
            }
        }

        auto drawDashedWorldLine = [&](const Vec2& aW, const Vec2& bW, float width, SDL_Color col, double dashLen, double gapLen) {
            double dx = bW.x - aW.x, dy = bW.y - aW.y;
            double len = std::sqrt(dx * dx + dy * dy);
            if (len < 1e-3) return;
            double ux = dx / len, uy = dy / len;
            double step = dashLen + gapLen;
            for (double d = 0.0; d < len; d += step) {
                double dEnd = std::min(len, d + dashLen);
                Vec2 s0 = worldToScreen(Vec2(aW.x + ux * d, aW.y + uy * d), screenW, screenH);
                Vec2 s1 = worldToScreen(Vec2(aW.x + ux * dEnd, aW.y + uy * dEnd), screenW, screenH);
                drawAALine(renderer, (float)s0.x, (float)s0.y, (float)s1.x, (float)s1.y, width, col);
            }
        };

        for (size_t i = 0; i < goals.size(); ++i) {
            const Goal& g = goals[i];
            double d0x = g.p0.x - ballPos.x, d0y = g.p0.y - ballPos.y;
            double d1x = g.p1.x - ballPos.x, d1y = g.p1.y - ballPos.y;
            double glx = g.p1.x - g.p0.x, gly = g.p1.y - g.p0.y;
            double d0 = std::sqrt(d0x * d0x + d0y * d0y);
            double d1 = std::sqrt(d1x * d1x + d1y * d1y);
            double goalLen = std::sqrt(glx * glx + gly * gly);
            if (d0 + d1 < goalLen + 2.0) continue;

            Vec2 mid((g.p0.x + g.p1.x) * 0.5, (g.p0.y + g.p1.y) * 0.5);
            double mdx = mid.x - ballPos.x, mdy = mid.y - ballPos.y;
            double distMid = std::sqrt(mdx * mdx + mdy * mdy);

            if (getIsMyGoal(i)) {
                if (!assistant.showGkDangerCone) continue;

                float nearBoost = static_cast<float>(std::clamp(1.0 - distMid / 420.0, 0.0, 1.0));
                uint8_t edgeAlpha = static_cast<uint8_t>(std::clamp(opacity * (55.0f + 65.0f * nearBoost), 18.0f, 140.0f));
                uint8_t bisAlpha = static_cast<uint8_t>(edgeAlpha * 0.55f);

                SDL_Color edgeCol{248, 130, 130, edgeAlpha};
                if (usingExcludedColor) {
                    uint32_t c = static_cast<uint32_t>(assistant.autoGoalExcludeColor);
                    uint8_t r = static_cast<uint8_t>((((c >> 16) & 0xFF) + 255) / 2);
                    uint8_t gr = static_cast<uint8_t>((((c >> 8) & 0xFF) + 255) / 2);
                    uint8_t b = static_cast<uint8_t>(((c & 0xFF) + 255) / 2);
                    edgeCol = {r, gr, b, edgeAlpha};
                }
                SDL_Color bisCol{edgeCol.r, edgeCol.g, edgeCol.b, bisAlpha};

                Vec2 p0Scr = worldToScreen(g.p0, screenW, screenH);
                Vec2 p1Scr = worldToScreen(g.p1, screenW, screenH);
                float edgeW = std::max(1.1f, 1.35f * zoom);
                drawAALine(renderer, (float)ballScr.x, (float)ballScr.y, (float)p0Scr.x, (float)p0Scr.y, edgeW, edgeCol);
                drawAALine(renderer, (float)ballScr.x, (float)ballScr.y, (float)p1Scr.x, (float)p1Scr.y, edgeW, edgeCol);

                double sumD = d0 + d1;
                if (sumD > 1e-3) {
                    Vec2 bisPt((d1 * g.p0.x + d0 * g.p1.x) / sumD, (d1 * g.p0.y + d0 * g.p1.y) / sumD);
                    drawDashedWorldLine(ballPos, bisPt, std::max(0.9f, 1.0f * zoom), bisCol, 9.0, 11.0);
                }
            } else {
                if (!assistant.showOtherGoalsCone) continue;

                constexpr double kOtherGoalRange = 340.0;
                if (distMid >= kOtherGoalRange) continue;
                float prox = static_cast<float>(std::clamp(1.0 - distMid / kOtherGoalRange, 0.0, 1.0));
                float fade = prox * prox;
                if (fade < 0.04f) continue;

                uint32_t c = TacticalAssistant::getGoalColor(*world.stadium, g);
                uint8_t r = static_cast<uint8_t>((((c >> 16) & 0xFF) * 6 + 255 * 4) / 10);
                uint8_t gr = static_cast<uint8_t>((((c >> 8) & 0xFF) * 6 + 255 * 4) / 10);
                uint8_t b = static_cast<uint8_t>(((c & 0xFF) * 6 + 255 * 4) / 10);
                uint8_t alpha = static_cast<uint8_t>(std::clamp(opacity * 75.0f * fade, 0.0f, 80.0f));
                if (alpha < 6) continue;

                Vec2 p0Scr = worldToScreen(g.p0, screenW, screenH);
                Vec2 p1Scr = worldToScreen(g.p1, screenW, screenH);
                float lineW = std::max(1.0f, 1.05f * zoom);
                drawAALine(renderer, (float)ballScr.x, (float)ballScr.y, (float)p0Scr.x, (float)p0Scr.y, lineW, {r, gr, b, alpha});
                drawAALine(renderer, (float)ballScr.x, (float)ballScr.y, (float)p1Scr.x, (float)p1Scr.y, lineW, {r, gr, b, alpha});
            }
        }
    }

    if (assistant.showBallRollTrajectory && assistant.ballRollPath.size() >= 2) {
        uint8_t rollAlpha = static_cast<uint8_t>(opacity * 150.0f);
        drawPolyline(assistant.ballRollPath, std::max(1.4f, baseWidth * 0.75f), {210, 235, 255, rollAlpha}, true);
        Vec2 endScreen = worldToScreen(assistant.ballRollPath.back(), screenW, screenH);
        drawAARing(renderer, (float)endScreen.x, (float)endScreen.y, 6.0f * zoom, 1.5f * zoom, {210, 235, 255, rollAlpha}, 20);
    }

    const auto* me = world.getPlayer(myPlayerId);
    if (!me || me->discIndex < 0 || me->discIndex >= (int)world.discs.size()) return;

    Vec2 ballScreen = worldToScreen(world.discs[0].pos, screenW, screenH);
    Vec2 meScreen = worldToScreen(world.discs[me->discIndex].pos, screenW, screenH);

    if (assistant.showLiveAimTrajectory && assistant.manualAimIsNearBall && assistant.manualAimPath.size() >= 2) {
        uint8_t aimAlpha = assistant.manualAimCanKick
            ? static_cast<uint8_t>(std::min(255.0f, opacity * 255.0f))
            : static_cast<uint8_t>(opacity * 220.0f);
        SDL_Color aimCol = assistant.manualAimCanKick
            ? SDL_Color{34, 235, 115, aimAlpha}
            : SDL_Color{255, 240, 120, aimAlpha};

        float aimWidth = assistant.manualAimCanKick ? (baseWidth * 1.15f) : baseWidth;
        drawPolyline(assistant.manualAimPath, aimWidth, aimCol, false);

        Vec2 endPt = worldToScreen(assistant.manualAimPath.back(), screenW, screenH);
        drawAACircleFilled(renderer, (float)endPt.x, (float)endPt.y, (assistant.manualAimCanKick ? 4.6f : 4.0f) * zoom, aimCol, 16);
    }
}

void GameRenderer::drawDiscs(SDL_Renderer* renderer, const World& world, uint32_t myPlayerId, int screenW, int screenH) {
    float outlineW = std::max(1.2f, settings.discOutlineWidth * zoom);

    if (settings.showLocalHalo) {
        for (const auto& p : world.players) {
            if (p.id == myPlayerId && p.discIndex >= 0 && p.discIndex < (int)world.discs.size()) {
                Vec2 sp = worldToScreen(world.discs[p.discIndex].pos, screenW, screenH);
                uint8_t hAlpha = static_cast<uint8_t>(std::clamp(settings.localHaloOpacity, 0.1f, 1.0f) * 255.0f);
                SDL_Color haloCol = assistant.isRealShooting
                    ? SDL_Color{255, 95, 95, hAlpha}
                    : (assistant.isFakeShooting
                        ? SDL_Color{255, 185, 50, hAlpha}
                        : SDL_Color{255, 255, 255, hAlpha});
                drawAARing(renderer, (float)sp.x, (float)sp.y, 25.0f * zoom, std::max(2.0f, 3.0f * zoom), haloCol, 40);
                break;
            }
        }
    }

    for (const auto& p : world.players) {
        if (p.discIndex < 0 || p.discIndex >= (int)world.discs.size()) continue;
        if (settings.showLocalHalo && p.id == myPlayerId) continue;

        const auto& d = world.discs[p.discIndex];
        Vec2 sp = worldToScreen(d.pos, screenW, screenH);
        float radius = static_cast<float>(d.radius * zoom);

        std::string label = settings.showPlayerIds ? ("#" + std::to_string(p.id) + " " + p.name) : p.name;
        fontSmall.drawTextShadow(renderer, label, (float)sp.x, (float)sp.y + radius + 5.0f * zoom, {245, 248, 255, 240}, TextAlign::Center, std::clamp(zoom, 0.85f, 1.25f));
    }

    for (size_t i = 0; i < world.discs.size(); ++i) {
        const auto& d = world.discs[i];
        if (d.radius <= 0.1) continue;

        Vec2 sp = worldToScreen(d.pos, screenW, screenH);
        float radius = static_cast<float>(d.radius * zoom);
        if (radius < 1.5f) radius = 1.5f;

        if (sp.x + radius < -20 || sp.x - radius > screenW + 20 ||
            sp.y + radius < -20 || sp.y - radius > screenH + 20) {
            continue;
        }

        if (d.playerId < 0) {
            if ((int32_t)d.color == -1) continue;
            drawAACircleFilled(renderer, (float)sp.x, (float)sp.y, radius, u32ToColor(d.color));
            drawAARing(renderer, (float)sp.x, (float)sp.y, radius, outlineW, {0, 0, 0, 255});
        } else {
            const Player* pl = world.getPlayer((uint32_t)d.playerId);
            if (!pl) continue;

            uint32_t fallbackCol = (pl->team == 1) ? 0xE56E56 : (pl->team == 2) ? 0x5689E5 : 0xCCCCCC;
            static const TeamColors kDefaultRedTc  = {0, 0xFFFFFF, {0xE56E56}};
            static const TeamColors kDefaultBlueTc = {0, 0xFFFFFF, {0x5689E5}};
            static const TeamColors kDefaultSpecTc = {0, 0xFFFFFF, {0xCCCCCC}};
            const TeamColors& defaultTc = (pl->team == 1) ? kDefaultRedTc : (pl->team == 2 ? kDefaultBlueTc : kDefaultSpecTc);

            const TeamColors& tc = (settings.showTeamColors && (pl->team == 1 || pl->team == 2) && !world.teamColors[pl->team].colors.empty())
                ? world.teamColors[pl->team]
                : defaultTc;

            drawStripedPlayerDisc(renderer, (float)sp.x, (float)sp.y, radius, tc, fallbackCol);

            if (pl->id == myPlayerId && assistant.rsCurrentState != TacticalAssistant::RSSuperpowerState::None) {
                if (assistant.rsCurrentState == TacticalAssistant::RSSuperpowerState::SlideReady) {

                    drawAARing(renderer, (float)sp.x, (float)sp.y, radius + 3.5f * zoom, 2.2f * zoom, {52, 211, 153, 230}, 32);
                    float fw = fontSmall.measureText("SLIDE", 0.82f) + 12.0f;
                    float fh = 15.0f;
                    float fx = (float)sp.x - fw * 0.5f;
                    float fy = (float)sp.y - radius - 18.0f * zoom;
                    drawRoundedRect(renderer, fx, fy, fw, fh, 4.0f, {6, 44, 30, 220}, {52, 211, 153, 220}, 1.0f);
                    fontSmall.drawTextShadow(renderer, "SLIDE", (float)sp.x, fy + 1.5f, {167, 243, 208, 255}, TextAlign::Center, 0.82f);
                } else if (assistant.rsCurrentState == TacticalAssistant::RSSuperpowerState::SprintReady) {

                    drawAARing(renderer, (float)sp.x, (float)sp.y, radius + 3.5f * zoom, 2.2f * zoom, {250, 204, 21, 230}, 32);
                    float fw = fontSmall.measureText("SPRINT", 0.82f) + 12.0f;
                    float fh = 15.0f;
                    float fx = (float)sp.x - fw * 0.5f;
                    float fy = (float)sp.y - radius - 18.0f * zoom;
                    drawRoundedRect(renderer, fx, fy, fw, fh, 4.0f, {45, 33, 6, 220}, {250, 204, 21, 220}, 1.0f);
                    fontSmall.drawTextShadow(renderer, "SPRINT", (float)sp.x, fy + 1.5f, {254, 240, 138, 255}, TextAlign::Center, 0.82f);
                } else if (assistant.rsCurrentState == TacticalAssistant::RSSuperpowerState::Charging) {

                    drawAARing(renderer, (float)sp.x, (float)sp.y, radius + 2.5f * zoom, 1.4f * zoom, {147, 197, 253, 160}, 24);
                }
            } else if (pl->id == myPlayerId && assistant.isRealShooting) {
                const char* tagText = "SHOT";
                SDL_Color bgCol = {42, 12, 16, 225};
                SDL_Color borderCol = {255, 95, 95, 245};
                SDL_Color txtCol = {255, 165, 165, 255};
                float fw = fontSmall.measureText(tagText, 0.90f) + 14.0f;
                float fh = 18.0f;
                float fx = (float)sp.x - fw * 0.5f;
                float fy = (float)sp.y - radius - 20.0f * zoom;
                drawRoundedRect(renderer, fx, fy, fw, fh, 4.0f, bgCol, borderCol, 1.2f);
                fontSmall.drawTextShadow(renderer, tagText, (float)sp.x, fy + 2.5f, txtCol, TextAlign::Center, 0.90f);
            }

            bool showKickHalo = pl->isKicking;

            if (pl->id == myPlayerId && assistant.suppressLocalKickHalo) {
                showKickHalo = false;
            }

            if (pl->id != myPlayerId && assistant.antiFakeShotFilter && pl->fakeShotDetected) {
                showKickHalo = false;
            }
            SDL_Color strokeCol = showKickHalo ? SDL_Color{255, 255, 255, 255} : SDL_Color{0, 0, 0, 255};
            drawAARing(renderer, (float)sp.x, (float)sp.y, radius, outlineW, strokeCol);

            std::string av = pl->headlessAvatar.has_value()
                ? *pl->headlessAvatar
                : (settings.showAvatars && !pl->avatar.empty() ? pl->avatar : std::to_string(pl->teamOrder));
            if (!av.empty()) {
                SDL_Color txtCol = u32ToColor((uint32_t)tc.textColor);
                float avScale = static_cast<float>(d.radius / 15.0) * zoom * 0.96f;
                float avY = (float)sp.y - font.getLineHeight(avScale) * 0.50f;
                font.drawText(renderer, av, (float)sp.x, avY, txtCol, TextAlign::Center, avScale);
            }

            if (pl->chatIndicator && settings.showChatIndicators) {
                float bx = (float)sp.x;
                float by = (float)sp.y - radius - 14.0f * zoom;
                drawRoundedRect(renderer, bx - 11.0f * zoom, by - 7.0f * zoom, 22.0f * zoom, 14.0f * zoom, 5.0f * zoom, {255, 255, 255, 235}, {0, 0, 0, 220}, 1.5f);
                drawAACircleFilled(renderer, bx - 5.0f * zoom, by, 1.8f * zoom, {40, 40, 40, 255}, 12);
                drawAACircleFilled(renderer, bx,               by, 1.8f * zoom, {40, 40, 40, 255}, 12);
                drawAACircleFilled(renderer, bx + 5.0f * zoom, by, 1.8f * zoom, {40, 40, 40, 255}, 12);
            }
        }
    }
}

void GameRenderer::drawLocationIndicators(SDL_Renderer* renderer, const World& world, uint32_t myPlayerId, int screenW, int screenH) {
    if (world.discs.empty()) return;

    auto drawIndicatorForPos = [&](const Vec2& pos, SDL_Color col) {
        Vec2 sp = worldToScreen(pos, screenW, screenH);
        float marginX = 32.0f;
        float marginTop = 74.0f;
        float marginBot = 36.0f;

        float clampedX = std::clamp((float)sp.x, marginX, (float)screenW - marginX);
        float clampedY = std::clamp((float)sp.y, marginTop, (float)screenH - marginBot);
        float dx = (float)sp.x - clampedX;
        float dy = (float)sp.y - clampedY;
        if (dx * dx + dy * dy <= 900.0f) return;

        float angle = std::atan2(dy, dx);
        float cs = std::cos(angle);
        float sn = std::sin(angle);

        auto rotPt = [&](float lx, float ly, float ox, float oy) -> SDL_FPoint {
            return {
                clampedX + ox + lx * cs - ly * sn,
                clampedY + oy + lx * sn + ly * cs
            };
        };

        SDL_Color shadowCol{0, 0, 0, 140};
        SDL_Vertex sVerts[3] = {
            {rotPt(15.0f,  0.0f, 2.0f, 2.0f), shadowCol, {0, 0}},
            {rotPt( 0.0f,  7.5f, 2.0f, 2.0f), shadowCol, {0, 0}},
            {rotPt( 0.0f, -7.5f, 2.0f, 2.0f), shadowCol, {0, 0}}
        };
        int idx[3] = {0, 1, 2};
        SDL_RenderGeometry(renderer, nullptr, sVerts, 3, idx, 3);

        SDL_Vertex cVerts[3] = {
            {rotPt(15.0f,  0.0f, 0.0f, 0.0f), col, {0, 0}},
            {rotPt( 0.0f,  7.5f, 0.0f, 0.0f), col, {0, 0}},
            {rotPt( 0.0f, -7.5f, 0.0f, 0.0f), col, {0, 0}}
        };
        SDL_RenderGeometry(renderer, nullptr, cVerts, 3, idx, 3);
    };

    if ((int32_t)world.discs[0].color != -1) {
        drawIndicatorForPos(world.discs[0].pos, u32ToColor(world.discs[0].color));
    }

    for (const auto& p : world.players) {
        if (p.discIndex >= 0 && p.discIndex < (int)world.discs.size()) {
            SDL_Color tCol = (p.team == 1) ? u32ToColor(0xE56E56) : u32ToColor(0x5689E5);
            drawIndicatorForPos(world.discs[p.discIndex].pos, tCol);
        }
    }
}

void GameRenderer::drawHUD(SDL_Renderer* renderer, const World& world, uint32_t myPlayerId, int pingMs, int maxPingMs, int fps, int screenW, int screenH) {
    float midX = screenW * 0.5f;
    float topY = 12.0f;

    float barW = 290.0f;
    float barH = 42.0f;
    drawRoundedRect(renderer, midX - barW * 0.5f, topY, barW, barH, 8.0f, {18, 22, 30, 230}, {65, 78, 102, 200}, 1.5f);

    uint32_t rBadgeCol = (!world.teamColors[1].colors.empty()) ? (uint32_t)world.teamColors[1].colors[0] : 0xE56E56;
    uint32_t bBadgeCol = (!world.teamColors[2].colors.empty()) ? (uint32_t)world.teamColors[2].colors[0] : 0x5689E5;
    drawRoundedRect(renderer, midX - barW * 0.5f + 8.0f, topY + 7.0f, 34.0f, 28.0f, 5.0f, u32ToColor(rBadgeCol), {255, 255, 255, 60}, 1.0f);
    fontLarge.drawTextShadow(renderer, std::to_string(world.redScore), midX - 68.0f, topY + 9.0f, {255, 255, 255, 255}, TextAlign::Center);
    fontLarge.drawText(renderer, "-", midX - 42.0f, topY + 8.0f, {140, 155, 175, 255}, TextAlign::Center);
    fontLarge.drawTextShadow(renderer, std::to_string(world.blueScore), midX - 16.0f, topY + 9.0f, {255, 255, 255, 255}, TextAlign::Center);
    drawRoundedRect(renderer, midX + 6.0f, topY + 7.0f, 34.0f, 28.0f, 5.0f, u32ToColor(bBadgeCol), {255, 255, 255, 60}, 1.0f);

    int totalSec = static_cast<int>(world.timeElapsed);
    int mins = totalSec / 60;
    int secs = totalSec % 60;
    char timeBuf[32];
    std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", mins, secs);

    bool isOvertime = (world.timeLimit > 0 && world.timeElapsed >= world.timeLimit * 60.0);
    SDL_Color clockCol = isOvertime ? SDL_Color{255, 110, 110, 255} : SDL_Color{235, 242, 255, 255};
    fontLarge.drawTextShadow(renderer, timeBuf, midX + 94.0f, topY + 9.0f, clockCol, TextAlign::Center, 0.95f);

    drawRoundedRect(renderer, 14.0f, 12.0f, 335.0f, 32.0f, 6.0f, {18, 22, 30, 215}, {65, 80, 105, 185}, 1.2f);
    fontSmall.drawText(renderer, "[Tab] Lobby   •   [F1-F3 / Insert] Client Menu", 24.0f, 20.0f, {195, 215, 240, 250});

    float teleW = 285.0f;
    float teleH = 0.0f;
    float teleX = screenW - teleW - 14.0f;

    if (assistant.rsHelperEnabled && (assistant.rsRoomDetected || !world.hasMatch)) {
        float rsW = teleW;
        float rsH = 22.0f;
        float rsX = teleX;
        float rsY = 12.0f + teleH + 5.0f;
        drawRoundedRect(renderer, rsX, rsY, rsW, rsH, 5.0f, {15, 20, 28, 200}, {40, 52, 72, 160}, 1.0f);

        char sBuf[32];
        SDL_Color sBorder, sTxt;
        if (assistant.rsIsSlideAvailable()) {
            std::snprintf(sBuf, sizeof(sBuf), "👟 Slide: Ready [C]");
            sBorder = {52, 211, 153, 190};
            sTxt = {167, 243, 208, 240};
        } else {
            int sSec = static_cast<int>(std::ceil(assistant.rsCooldownRemainingMs / 1000.0f));
            std::snprintf(sBuf, sizeof(sBuf), "👟 Slide: %ds", sSec);
            sBorder = {239, 68, 68, 160};
            sTxt = {252, 165, 165, 240};
        }
        drawRoundedRect(renderer, rsX + 4.0f, rsY + 2.5f, 135.0f, 17.0f, 3.5f, {18, 26, 36, 190}, sBorder, 0.9f);
        fontSmall.drawText(renderer, sBuf, rsX + 71.5f, rsY + 4.0f, sTxt, TextAlign::Center, 0.82f);

        char spBuf[32];
        SDL_Color spBorder, spTxt;
        if (assistant.rsIsSprintAvailable()) {
            std::snprintf(spBuf, sizeof(spBuf), "⚡ Sprint: Ready [V]");
            spBorder = {250, 204, 21, 190};
            spTxt = {254, 240, 138, 240};
        } else {
            int spSec = static_cast<int>(std::ceil(assistant.rsCooldownRemainingMs / 1000.0f));
            std::snprintf(spBuf, sizeof(spBuf), "⚡ Sprint: %ds", spSec);
            spBorder = {239, 68, 68, 160};
            spTxt = {252, 165, 165, 240};
        }
        drawRoundedRect(renderer, rsX + 144.0f, rsY + 2.5f, 137.0f, 17.0f, 3.5f, {18, 26, 36, 190}, spBorder, 0.9f);
        fontSmall.drawText(renderer, spBuf, rsX + 212.5f, rsY + 4.0f, spTxt, TextAlign::Center, 0.82f);
    }

    if (world.hasMatch && world.pauseTickCounter > 0) {
        fontBanner.drawTextShadow(renderer, "GAME", midX, screenH * 0.5f - 52.0f, {255, 255, 255, 245}, TextAlign::Center, 1.0f, 3.0f);
        fontBanner.drawTextShadow(renderer, "PAUSED", midX, screenH * 0.5f - 4.0f, {255, 255, 255, 245}, TextAlign::Center, 1.0f, 3.0f);

        if (world.pauseTickCounter < 120) {
            float barProgressW = (static_cast<float>(world.pauseTickCounter) / 120.0f) * 220.0f;
            drawRoundedRect(renderer, midX - barProgressW * 0.5f, screenH * 0.5f + 56.0f, barProgressW, 14.0f, 4.0f, {255, 255, 255, 240});
        }
    } else if (bannerAnimTimer > 0.0) {
        float alphaF = std::clamp(static_cast<float>(bannerAnimTimer * 2.0), 0.0f, 1.0f);
        SDL_Color c = bannerColor;
        c.a = static_cast<uint8_t>(alphaF * 255);
        fontBanner.drawTextShadow(renderer, bannerLine1, midX, screenH * 0.5f - 55.0f, c, TextAlign::Center, 1.05f, 3.5f);
        fontBanner.drawTextShadow(renderer, bannerLine2, midX, screenH * 0.5f - 4.0f, c, TextAlign::Center, 1.05f, 3.5f);
    }
}

void GameRenderer::scrollChat(int deltaLines) {
    std::lock_guard<std::mutex> lock(chatMutex);
    int maxVisibleLines = isChatOpen ? 10 : 6;
    int maxScroll = std::max(0, static_cast<int>(chatMessages.size()) - maxVisibleLines);
    chatScrollOffset = std::clamp(chatScrollOffset + deltaLines, 0, maxScroll);
}

void GameRenderer::drawChat(SDL_Renderer* renderer, int screenW, int screenH) {
    std::lock_guard<std::mutex> lock(chatMutex);
    float boxX = 14.0f;
    float boxW = std::min(560.0f, screenW * 0.48f);
    int maxVisibleLines = isChatOpen ? 10 : 6;
    float lineH = 20.0f;

    int totalMsgs = (int)chatMessages.size();
    int maxScroll = std::max(0, totalMsgs - maxVisibleLines);
    chatScrollOffset = std::clamp(chatScrollOffset, 0, maxScroll);

    std::vector<const ChatMessage*> visibleMsgs;
    int startIdx = totalMsgs - 1 - chatScrollOffset;
    for (int i = startIdx; i >= 0 && (int)visibleMsgs.size() < maxVisibleLines; --i) {
        if (!isChatOpen && chatMessages[i].age > 14.0f) continue;
        visibleMsgs.push_back(&chatMessages[i]);
    }

    float emojiBarH = isChatOpen ? 30.0f : 0.0f;
    float inputH = isChatOpen ? 34.0f : 0.0f;
    float historyH = visibleMsgs.empty() ? 0.0f : (visibleMsgs.size() * lineH + 12.0f);
    float totalH = historyH + emojiBarH + inputH;
    float baseBotY = screenH - 14.0f;

    if (isChatOpen || !visibleMsgs.empty()) {
        uint8_t bgAlpha = isChatOpen ? 225 : 120;
        drawRoundedRect(renderer, boxX, baseBotY - totalH, boxW, totalH, 7.0f,
                        {14, 18, 25, bgAlpha},
                        isChatOpen ? SDL_Color{75, 130, 210, 220} : SDL_Color{40, 50, 65, 90},
                        isChatOpen ? 1.5f : 1.0f);
    }

    if (isChatOpen && maxScroll > 0) {
        float scrollTrackH = std::max(20.0f, historyH - 12.0f);
        float scrollTrackY = baseBotY - totalH + 6.0f;
        float scrollTrackX = boxX + boxW - 8.0f;
        drawRoundedRect(renderer, scrollTrackX, scrollTrackY, 4.0f, scrollTrackH, 2.0f, {30, 40, 58, 160});

        float thumbRatio = std::max(0.18f, static_cast<float>(maxVisibleLines) / static_cast<float>(totalMsgs));
        float thumbH = std::max(16.0f, scrollTrackH * thumbRatio);
        float scrollPct = static_cast<float>(chatScrollOffset) / static_cast<float>(maxScroll);
        float thumbY = scrollTrackY + (scrollTrackH - thumbH) * (1.0f - scrollPct);
        drawRoundedRect(renderer, scrollTrackX, thumbY, 4.0f, thumbH, 2.0f, {80, 145, 235, 240});

        if (chatScrollOffset > 0) {
            std::string scrollTag = "▲ +" + std::to_string(chatScrollOffset) + " (Scroll down to live)";
            float tagW = scrollTag.size() * 7.2f + 14.0f;
            drawRoundedRect(renderer, boxX + boxW - tagW - 14.0f, baseBotY - totalH + 6.0f, tagW, 18.0f, 4.0f, {20, 28, 42, 230}, {70, 120, 200, 200}, 1.0f);
            fontSmall.drawText(renderer, scrollTag, boxX + boxW - tagW * 0.5f - 14.0f, baseBotY - totalH + 8.5f, {120, 195, 255, 255}, TextAlign::Center, 0.78f);
        }
    }

    if (isChatOpen) {

        float ebY = baseBotY - inputH - emojiBarH + 3.0f;
        fontSmall.drawText(renderer, "Emojis:", boxX + 10.0f, ebY + 6.0f, {150, 175, 205, 240}, TextAlign::Left, 0.88f);
        float startEx = boxX + 66.0f;
        for (int i = 0; i < 14; ++i) {
            float ex = startEx + i * 28.0f;
            if (ex + 25.0f > boxX + boxW - 6.0f) break;
            drawRoundedRect(renderer, ex, ebY + 1.0f, 25.0f, 24.0f, 4.0f, {24, 32, 46, 230}, {65, 88, 120, 180}, 1.0f);
            font.drawText(renderer, kQuickEmojis[i], ex + 12.5f, ebY + 4.5f, {255, 255, 255, 255}, TextAlign::Center, 0.90f);
        }

        float iy = baseBotY - inputH;
        drawAALine(renderer, boxX + 6.0f, iy, boxX + boxW - 6.0f, iy, 1.0f, {60, 75, 100, 180});
        std::string prompt = "Chat (/extrap, /avatar 👑): " + chatInputBuffer + "_";
        fontSmall.drawText(renderer, prompt, boxX + 12.0f, iy + 9.0f, {255, 255, 255, 255});
    }

    float msgY = baseBotY - inputH - emojiBarH - lineH - 4.0f;
    for (const auto* m : visibleMsgs) {
        uint8_t alpha = 255;
        if (!isChatOpen && m->age > 10.0f) {
            alpha = static_cast<uint8_t>(std::clamp((14.0f - m->age) / 4.0f, 0.0f, 1.0f) * 255.0f);
        }
        SDL_Color col = u32ToColor(m->color, alpha);
        std::string line = m->isAnnouncement ? m->text : (m->sender + ": " + m->text);
        float scale = (m->style >= 3) ? 0.9f : 1.0f;
        fontSmall.drawTextShadow(renderer, line, boxX + 10.0f, msgY, col, TextAlign::Left, scale, 1.2f);
        msgY -= lineH;
    }
}

void GameRenderer::drawRoomLobbyModal(SDL_Renderer* renderer, const World& world, uint32_t myPlayerId, int screenW, int screenH, bool isSandbox) {
    float modalW = std::min(780.0f, screenW - 40.0f);
    float modalH = std::min(490.0f, screenH - 80.0f);
    float mx = (screenW - modalW) * 0.5f;
    float my = (screenH - modalH) * 0.5f;

    const Player* me = world.getPlayer(myPlayerId);
    bool isAdmin = isSandbox || (me && me->admin);
    bool canChangeTeam = isAdmin || (!world.teamsLocked && !world.hasMatch);

    drawRoundedRect(renderer, mx + 4.0f, my + 6.0f, modalW, modalH, 12.0f, {0, 0, 0, 140});
    drawRoundedRect(renderer, mx, my, modalW, modalH, 12.0f, {13, 17, 26, 248}, {71, 85, 105, 220}, 1.8f);

    fontLarge.drawTextShadow(renderer, world.roomName, mx + 24.0f, my + 16.0f, {245, 248, 255, 255}, TextAlign::Left, 0.90f);
    drawRoundedRect(renderer, mx + modalW - 124.0f, my + 13.0f, 108.0f, 30.0f, 6.0f, {185, 28, 28, 220}, {248, 113, 113, 200}, 1.2f);
    fontSmall.drawTextShadow(renderer, "Leave Room", mx + modalW - 70.0f, my + 21.0f, {255, 255, 255, 255}, TextAlign::Center, 0.92f);

    drawAALine(renderer, mx + 16.0f, my + 52.0f, mx + modalW - 16.0f, my + 52.0f, 1.2f, {45, 58, 78, 200});

    float colW = (modalW - 64.0f) / 3.0f;
    float colY = my + 64.0f;
    float colH = modalH - 180.0f;

    auto drawTeamCol = [&](int teamId, float cx, const std::string& title, SDL_Color headerCol, SDL_Color borderCol) {
        int count = 0;
        for (const auto& p : world.players) if (p.team == teamId) count++;

        std::string label = canChangeTeam ? (title + " [" + std::to_string(count) + "]") : (title + " (Locked)");
        SDL_Color btnBg = canChangeTeam ? headerCol : SDL_Color{30, 41, 59, 200};

        drawRoundedRect(renderer, cx, colY, colW, 34.0f, 7.0f, btnBg, borderCol, 1.2f);
        font.drawTextShadow(renderer, label, cx + colW * 0.5f, colY + 9.0f, {255, 255, 255, 255}, TextAlign::Center, 0.88f);

        drawRoundedRect(renderer, cx, colY + 40.0f, colW, colH - 40.0f, 7.0f, {10, 14, 22, 235}, {38, 48, 68, 200}, 1.0f);

        float py = colY + 48.0f;
        for (const auto& p : world.players) {
            if (p.team != teamId) continue;
            if (py + 26.0f > colY + colH) break;

            if (p.id == myPlayerId) {
                drawRoundedRect(renderer, cx + 4.0f, py - 2.0f, colW - 8.0f, 24.0f, 5.0f, {30, 58, 95, 200}, {56, 189, 248, 180}, 1.0f);
            } else {
                drawRoundedRect(renderer, cx + 4.0f, py - 2.0f, colW - 8.0f, 24.0f, 5.0f, {17, 24, 38, 160});
            }

            std::string flagStr = "[" + p.flag + "]";
            fontSmall.drawText(renderer, flagStr, cx + 10.0f, py + 3.0f, {148, 163, 184, 255}, TextAlign::Left, 0.86f);

            std::string pName = p.admin ? ("👑 " + p.name) : p.name;
            SDL_Color nameCol = p.admin ? SDL_Color{250, 204, 21, 255} : (p.id == myPlayerId ? SDL_Color{255, 255, 255, 255} : SDL_Color{226, 232, 240, 255});
            std::string pLabel = FontRenderer::clipUTF8Codepoints(pName, 15);
            fontSmall.drawText(renderer, pLabel, cx + 46.0f, py + 3.0f, nameCol, TextAlign::Left, 0.90f);

            SDL_Color pingCol = (p.ping < 50) ? SDL_Color{34, 197, 94, 255} : ((p.ping < 110) ? SDL_Color{234, 179, 8, 255} : SDL_Color{239, 68, 68, 255});
            std::string pingStr = std::to_string(p.ping) + "ms";
            fontSmall.drawTextShadow(renderer, pingStr, cx + colW - 10.0f, py + 3.0f, pingCol, TextAlign::Right, 0.86f);

            py += 26.0f;
        }
    };

    drawTeamCol(1, mx + 16.0f,               "🔴 Red Team",    {185, 28, 28, 230}, {239, 68, 68, 220});
    drawTeamCol(0, mx + 32.0f + colW,        "⚪ Spectators",  {51, 65, 85, 230},  {100, 116, 139, 220});
    drawTeamCol(2, mx + 48.0f + colW * 2.0f, "🔵 Blue Team",   {29, 78, 216, 230}, {59, 130, 246, 220});

    float infoY = colY + colH + 12.0f;
    std::string stName = world.stadium ? world.stadium->name : "Classic";
    char infoBuf[256];
    std::snprintf(infoBuf, sizeof(infoBuf), "Stadium: %s   •   Time Limit: %d min   •   Score Limit: %d   •   Total Players: %zu",
                  stName.c_str(), world.timeLimit, world.scoreLimit, world.players.size());
    fontSmall.drawText(renderer, infoBuf, mx + modalW * 0.5f, infoY, {160, 180, 205, 255}, TextAlign::Center, 0.88f);

    float btnY = my + modalH - 48.0f;
    float btnW = 145.0f;
    float btnH = 34.0f;

    if (isAdmin) {

        SDL_Color matchBg = world.hasMatch ? SDL_Color{185, 28, 28, 235} : SDL_Color{16, 185, 129, 235};
        SDL_Color matchBr = world.hasMatch ? SDL_Color{239, 68, 68, 220} : SDL_Color{52, 211, 153, 220};
        drawRoundedRect(renderer, mx + modalW * 0.5f - btnW - 85.0f, btnY, btnW, btnH, 7.0f, matchBg, matchBr, 1.2f);
        fontSmall.drawTextShadow(renderer, world.hasMatch ? "⏹ Stop Match" : "▶ Start Match",
                                 mx + modalW * 0.5f - btnW * 0.5f - 85.0f, btnY + 10.0f, {255, 255, 255, 255}, TextAlign::Center, 0.92f);

        drawRoundedRect(renderer, mx + modalW * 0.5f - 72.5f, btnY, 145.0f, btnH, 7.0f, {217, 119, 6, 235}, {245, 158, 11, 220}, 1.2f);
        fontSmall.drawTextShadow(renderer, (world.pauseTickCounter == 120) ? "▶ Resume [P]" : "⏸ Pause [P]",
                                 mx + modalW * 0.5f, btnY + 10.0f, {255, 255, 255, 255}, TextAlign::Center, 0.92f);

        drawRoundedRect(renderer, mx + modalW * 0.5f + 85.0f, btnY, btnW, btnH, 7.0f, {79, 70, 229, 235}, {129, 140, 248, 220}, 1.2f);
        fontSmall.drawTextShadow(renderer, "⚡ Auto Teams",
                                 mx + modalW * 0.5f + 85.0f + btnW * 0.5f, btnY + 10.0f, {255, 255, 255, 255}, TextAlign::Center, 0.92f);
    } else {
        drawRoundedRect(renderer, mx + modalW * 0.5f - 220.0f, btnY + 2.0f, 440.0f, 30.0f, 6.0f,
                        {20, 28, 42, 210}, {51, 65, 85, 160}, 1.0f);
        fontSmall.drawText(renderer, "Match Controls (Start / Stop / Pause / Auto) require Room Admin",
                           mx + modalW * 0.5f, btnY + 9.5f, {148, 163, 184, 230}, TextAlign::Center, 0.88f);
    }
}

void GameRenderer::drawMenuTabsHeader(SDL_Renderer* renderer, float x, float y, float w, int activeTab) {
    const char* tabNames[3] = {
        "⚡ [F1] Network & Lag",
        "🎯 [F2] Gameplay & Movement",
        "🎨 [F3] Visuals & Pitch"
    };
    SDL_Color tabAccents[3] = {
        {56, 189, 248, 255},
        {52, 211, 153, 255},
        {192, 132, 252, 255}
    };

    float tabW = (w - 36.0f) / 3.0f;
    for (int i = 0; i < 3; ++i) {
        float tx = x + 12.0f + i * (tabW + 6.0f);
        bool sel = (activeTab == i + 1);
        SDL_Color bg = sel ? SDL_Color{30, 41, 59, 255} : SDL_Color{15, 23, 42, 180};
        SDL_Color br = sel ? tabAccents[i] : SDL_Color{51, 65, 85, 160};

        drawRoundedRect(renderer, tx, y + 10.0f, tabW, 30.0f, 6.0f, bg, br, sel ? 1.5f : 1.0f);
        fontSmall.drawTextShadow(renderer, tabNames[i], tx + tabW * 0.5f, y + 18.0f,
                                 sel ? SDL_Color{255, 255, 255, 255} : SDL_Color{148, 163, 184, 230},
                                 TextAlign::Center, 0.90f);

        if (sel) {
            drawRoundedRect(renderer, tx + 8.0f, y + 37.0f, tabW - 16.0f, 3.0f, 1.5f, tabAccents[i]);
        }
    }
    drawAALine(renderer, x + 12.0f, y + 46.0f, x + w - 12.0f, y + 46.0f, 1.2f, {45, 58, 80, 200});
}

void GameRenderer::drawSliderRow(SDL_Renderer* renderer, float x, float y, float w, const std::string& label, const std::string& valStr, float normVal, SDL_Color barColor) {
    float clamped = std::clamp(normVal, 0.0f, 1.0f);

    fontSmall.drawText(renderer, label, x + 16.0f, y + 2.0f, {226, 232, 240, 255}, TextAlign::Left, 0.92f);

    float tagW = std::max(64.0f, valStr.size() * 8.5f + 16.0f);
    float tagX = x + w - 16.0f - tagW;
    drawRoundedRect(renderer, tagX, y - 1.0f, tagW, 20.0f, 5.0f, {26, 36, 52, 240}, {51, 65, 85, 180}, 1.0f);
    fontSmall.drawTextShadow(renderer, valStr, tagX + tagW * 0.5f, y + 2.0f, barColor, TextAlign::Center, 0.90f);

    float barX = x + 16.0f;
    float barY = y + 22.0f;
    float barW = w - 32.0f;
    drawRoundedRect(renderer, barX, barY, barW, 8.0f, 4.0f, {10, 15, 24, 255}, {45, 58, 80, 220}, 1.0f);

    if (clamped > 0.005f) {
        drawRoundedRect(renderer, barX, barY, barW * clamped, 8.0f, 4.0f, barColor);
    }

    float knobX = barX + barW * clamped;
    float knobY = barY + 4.0f;
    drawAACircleFilled(renderer, knobX, knobY, 8.0f, {0, 0, 0, 160}, 18);
    drawAACircleFilled(renderer, knobX, knobY, 6.5f, {255, 255, 255, 255}, 18);
    drawAACircleFilled(renderer, knobX, knobY, 3.2f, barColor, 14);
}

void GameRenderer::scrollSettingsPanel(float deltaPx, int screenH) {
    float desiredH = (activeMenuTab == 1) ? 620.0f : 510.0f;
    float h = std::min(desiredH, std::max(260.0f, static_cast<float>(screenH) - 48.0f));
    float maxScroll = std::max(0.0f, desiredH - h);
    settingsScrollY = std::clamp(settingsScrollY + deltaPx, 0.0f, maxScroll);
}

void GameRenderer::drawUnifiedDashboardModal(SDL_Renderer* renderer, const World& world, int screenW, int screenH) {
    float w = (activeMenuTab == 1) ? 580.0f : 520.0f;
    float desiredH = (activeMenuTab == 1) ? 620.0f : 510.0f;
    float h = std::min(desiredH, std::max(260.0f, static_cast<float>(screenH) - 48.0f));
    float maxScroll = std::max(0.0f, desiredH - h);
    settingsScrollY = std::clamp(settingsScrollY, 0.0f, maxScroll);
    float x = (screenW - w) * 0.5f;
    float y = (screenH - h) * 0.5f;

    SDL_Color accentCol = (activeMenuTab == 0)
        ? SDL_Color{56, 189, 248, 255}
        : ((activeMenuTab == 1)
            ? SDL_Color{52, 211, 153, 255}
            : SDL_Color{192, 132, 252, 255});

    drawRoundedRect(renderer, x + 4.0f, y + 6.0f, w, h, 14.0f, {0, 0, 0, 160});
    drawRoundedRect(renderer, x, y, w, h, 14.0f, {13, 17, 26, 252}, accentCol, 1.8f);

    font.drawTextShadow(renderer, "⚡ HAXBALL SUITE // ESPORTS CLIENT", x + 16.0f, y + 14.0f, {241, 245, 249, 255}, TextAlign::Left, 0.88f);

    float closeX = x + w - 38.0f;
    float closeY = y + 10.0f;
    drawRoundedRect(renderer, closeX, closeY, 26.0f, 22.0f, 5.0f, {30, 41, 59, 220}, {71, 85, 105, 180}, 1.0f);
    fontSmall.drawText(renderer, "✕", closeX + 13.0f, closeY + 3.0f, {226, 232, 240, 255}, TextAlign::Center, 0.90f);

    drawMenuTabsHeader(renderer, x, y + 26.0f, w, activeMenuTab + 1);

    SDL_Rect bodyClip = {
        static_cast<int>(x + 2.0f),
        static_cast<int>(y + 72.0f),
        static_cast<int>(w - 4.0f),
        std::max(1, static_cast<int>(h - 78.0f))
    };
    SDL_RenderSetClipRect(renderer, &bodyClip);

    float ry = y + 78.0f - settingsScrollY;

    if (activeMenuTab == 0) {

        drawSliderRow(renderer, x, ry, w, "Client Extrapolation:",
                      std::to_string(settings.extrapolationMs) + " ms",
                      (settings.extrapolationMs + 200.0f) / 700.0f, {56, 189, 248, 255});
        ry += 34.0f;

        const char* exBtns[7] = {"-25", "-5", "0ms", "+5", "+25", "+60", "+120"};
        float bx = x + 16.0f;
        float bw = (w - 32.0f - 30.0f) / 7.0f;
        for (int i = 0; i < 7; ++i) {
            float btnX = bx + i * (bw + 5.0f);
            drawRoundedRect(renderer, btnX, ry, bw, 22.0f, 5.0f, {24, 34, 52, 235}, {51, 65, 85, 200}, 1.0f);
            fontSmall.drawText(renderer, exBtns[i], btnX + bw * 0.5f, ry + 4.0f, {226, 232, 240, 255}, TextAlign::Center, 0.88f);
        }
        ry += 28.0f;

        drawSliderRow(renderer, x, ry, w, "Ping Delay (/handicap):",
                      std::to_string(settings.handicapMs) + " ms",
                      settings.handicapMs / 250.0f, {52, 211, 153, 255});
        ry += 34.0f;

        std::string fpsStr = (settings.fpsCap >= 500 || settings.fpsCap <= 0) ? "Uncapped" : (std::to_string(settings.fpsCap) + " FPS");
        float fpsNorm = (settings.fpsCap >= 500 || settings.fpsCap <= 0) ? 1.0f : ((settings.fpsCap - 60.0f) / 440.0f);
        drawSliderRow(renderer, x, ry, w, "FPS Limit:", fpsStr, fpsNorm, {251, 191, 36, 255});
        ry += 34.0f;

        char zBuf[32];
        std::snprintf(zBuf, sizeof(zBuf), "%.2fx", zoom);
        drawSliderRow(renderer, x, ry, w, "Camera Zoom:",
                      zBuf, (zoom - 0.45f) / 2.15f, {192, 132, 252, 255});
        ry += 34.0f;

        char smBuf[32];
        std::snprintf(smBuf, sizeof(smBuf), "%d%%", (int)std::round(settings.cameraSmoothness * 100.0f));
        drawSliderRow(renderer, x, ry, w, "Camera Smoothness:",
                      smBuf, (settings.cameraSmoothness - 0.05f) / 0.95f, {45, 212, 191, 255});
        ry += 36.0f;

        float stepY = 28.0f;
        auto drawClickRow = [&](const std::string& key, const std::string& val, SDL_Color valCol = {52, 211, 153, 255}) {
            drawRoundedRect(renderer, x + 14.0f, ry, w - 28.0f, 25.0f, 5.0f, {20, 28, 42, 210}, {45, 58, 80, 160}, 1.0f);
            fontSmall.drawText(renderer, key, x + 24.0f, ry + 4.5f, {226, 232, 240, 255}, TextAlign::Left, 0.90f);
            fontSmall.drawTextShadow(renderer, val, x + w - 24.0f, ry + 4.5f, valCol, TextAlign::Right, 0.90f);
            ry += stepY;
        };

        const char* camNames[] = {"Adaptive", "Player Lock", "Ball Lock", "Full Pitch"};
        drawClickRow("Camera Mode [V]:", camNames[settings.cameraMode % 4], {56, 189, 248, 255});

        const char* rotNames[] = {"0° Horizontal", "90° Vertical ↑", "180° Horizontal", "270° Vertical ↓", "Auto Attack-Up ↑"};
        drawClickRow("Field Rotation [R]:", rotNames[settings.fieldRotation % 5], {251, 191, 36, 255});

        drawClickRow("VSync:", settings.vsync ? "ON" : "OFF (Low Latency)",
                     settings.vsync ? SDL_Color{251, 191, 36, 255} : SDL_Color{52, 211, 153, 255});
        drawClickRow("NetGraph [F8]:", settings.showNetGraph ? "ON" : "OFF");
        drawClickRow("Invisible Walls [F9]:", settings.showInvisibleSegments ? "ON" : "OFF");
        drawClickRow("Ghost Desync (Real/Fake):", settings.showDesyncGhost ? "ON (Visualizing Real Pos)" : "OFF",
                     settings.showDesyncGhost ? SDL_Color{251, 191, 36, 255} : SDL_Color{148, 163, 184, 255});

    } else if (activeMenuTab == 1) {

        float colW = (w - 38.0f) * 0.5f;
        float rowH = 24.0f;

        auto drawAssistToggleCol = [&](int colIdx, int rowIdx, const std::string& title, bool active) {
            float tx = x + 14.0f + colIdx * (colW + 10.0f);
            float ty = ry + rowIdx * rowH;
            SDL_Color bg = active ? SDL_Color{16, 36, 38, 230} : SDL_Color{18, 25, 38, 200};
            SDL_Color border = active ? SDL_Color{52, 211, 153, 220} : SDL_Color{45, 58, 80, 160};
            drawRoundedRect(renderer, tx, ty, colW, 21.0f, 4.5f, bg, border, 1.0f);

            if (active) {
                drawRoundedRect(renderer, tx, ty, 3.0f, 21.0f, 1.5f, {52, 211, 153, 255});
            }

            fontSmall.drawText(renderer, title, tx + 8.0f, ty + 3.5f, {245, 248, 255, 255}, TextAlign::Left, 0.85f);

            float swW = 34.0f;
            float swH = 14.0f;
            float swX = tx + colW - 42.0f;
            float swY = ty + 3.5f;
            SDL_Color swBg = active ? SDL_Color{16, 185, 129, 255} : SDL_Color{51, 65, 85, 220};
            drawRoundedRect(renderer, swX, swY, swW, swH, 7.0f, swBg);

            float knobX = active ? (swX + swW - 7.5f) : (swX + 7.5f);
            drawAACircleFilled(renderer, knobX, swY + 7.0f, 5.0f, {255, 255, 255, 255}, 14);
            if (active) {
                drawAACircleFilled(renderer, knobX, swY + 7.0f, 2.0f, {16, 185, 129, 255}, 10);
            }
        };

        drawAssistToggleCol(0, 0, "Live Aim Trajectory", assistant.showLiveAimTrajectory);
        drawAssistToggleCol(0, 1, "Ball Roll Trajectory", assistant.showBallRollTrajectory);
        drawAssistToggleCol(0, 2, "Auto-Goal [0]", assistant.autoGoalEnabled);
        drawAssistToggleCol(0, 3, "Auto-Goal: Any Goal", assistant.autoGoalAnyGoal);
        drawAssistToggleCol(0, 4, "Auto-Save [6]", assistant.autoSaveEnabled);
        drawAssistToggleCol(0, 5, "First-Touch Cushion", assistant.firstTouchCushionEnabled);
        drawAssistToggleCol(0, 6, "Auto-Shaker [B]", assistant.autoShakeEnabled);
        drawAssistToggleCol(0, 7, "Moon Orbit [O]", assistant.ballOrbitEnabled);
        drawAssistToggleCol(0, 8, "GK Danger Cone", assistant.showGkDangerCone);
        drawAssistToggleCol(0, 9, "Extended Fake Shot", assistant.fakeShotExtendEnabled);

        drawAssistToggleCol(1, 0, "Fake Typing Troll [J]", assistant.fakeTypingActive);
        drawAssistToggleCol(1, 1, "Typing Strobe [U]", assistant.typingSpamEnabled);
        drawAssistToggleCol(1, 2, "Fake Shot Feint [F]", assistant.fakeShotEnabled);
        drawAssistToggleCol(1, 3, "Hold [N] Auto-Click", assistant.realShotEnabled);
        drawAssistToggleCol(1, 4, "Anti-Fake Filter", assistant.antiFakeShotFilter);
        drawAssistToggleCol(1, 5, "Real Soccer Super", assistant.rsHelperEnabled);
        drawAssistToggleCol(1, 6, "Fast-Fake Osc [P]", assistant.fastFakeEnabled);
        drawAssistToggleCol(1, 7, "Smart GK [7]", assistant.smartGkEnabled);
        drawAssistToggleCol(1, 8, "Other Goals Cone", assistant.showOtherGoalsCone);
        drawAssistToggleCol(1, 9, "Anti-OPMode", assistant.antiOpMode);

        ry += 10 * rowH + 6.0f;

        float sColW = colW;
        float sRowH = 32.0f;

        auto drawSliderCol = [&](int colIdx, int sRowIdx, const std::string& label, const std::string& valStr, float normVal, SDL_Color barColor) {
            float sx = x + 14.0f + colIdx * (sColW + 10.0f);
            float sy = ry + sRowIdx * sRowH;
            float clamped = std::clamp(normVal, 0.0f, 1.0f);

            fontSmall.drawText(renderer, label, sx + 4.0f, sy + 1.0f, {226, 232, 240, 255}, TextAlign::Left, 0.82f);

            float tagW = std::max(52.0f, valStr.size() * 7.5f + 12.0f);
            float tagX = sx + sColW - tagW;
            drawRoundedRect(renderer, tagX, sy - 1.0f, tagW, 17.0f, 4.0f, {26, 36, 52, 240}, {51, 65, 85, 180}, 1.0f);
            fontSmall.drawTextShadow(renderer, valStr, tagX + tagW * 0.5f, sy + 1.5f, barColor, TextAlign::Center, 0.82f);

            float barX = sx + 4.0f;
            float barY = sy + 18.0f;
            float barW = sColW - 8.0f;
            drawRoundedRect(renderer, barX, barY, barW, 6.0f, 3.0f, {10, 15, 24, 255}, {45, 58, 80, 220}, 1.0f);

            if (clamped > 0.005f) {
                drawRoundedRect(renderer, barX, barY, barW * clamped, 6.0f, 3.0f, barColor);
            }

            float knobX = barX + barW * clamped;
            float knobY = barY + 3.0f;
            drawAACircleFilled(renderer, knobX, knobY, 5.5f, {0, 0, 0, 160}, 16);
            drawAACircleFilled(renderer, knobX, knobY, 4.5f, {255, 255, 255, 255}, 16);
            drawAACircleFilled(renderer, knobX, knobY, 2.2f, barColor, 12);
        };

        char agAccBuf[32];
        std::snprintf(agAccBuf, sizeof(agAccBuf), "%d%%", std::clamp(assistant.autoGoalAccuracy, 30, 100));
        drawSliderCol(0, 0, "Auto-Goal Accuracy:", agAccBuf, (std::clamp(assistant.autoGoalAccuracy, 30, 100) - 30.0f) / 70.0f, {52, 211, 153, 255});

        char fakeBuf[32];
        std::snprintf(fakeBuf, sizeof(fakeBuf), "%d ms", assistant.fakeShotIntervalMs);
        drawSliderCol(0, 1, "Fake Shot Cooldown:", fakeBuf, (assistant.fakeShotIntervalMs - 50.0f) / 950.0f, {245, 158, 11, 255});

        char realBuf[32];
        if (assistant.realShotIntervalMs <= 0) std::snprintf(realBuf, sizeof(realBuf), "0 ms (Max)");
        else std::snprintf(realBuf, sizeof(realBuf), "%d ms", assistant.realShotIntervalMs);
        drawSliderCol(0, 2, "Auto-Click Cadence:", realBuf, assistant.realShotIntervalMs / 1000.0f, {239, 68, 68, 255});

        char slowBuf[32];
        std::snprintf(slowBuf, sizeof(slowBuf), "%d%% speed", assistant.slowSpeedPercent);
        drawSliderCol(0, 3, "Hold [M] Slow Speed:", slowBuf, (assistant.slowSpeedPercent - 10.0f) / 80.0f, {251, 191, 36, 255});

        char shakeBuf[32];
        std::snprintf(shakeBuf, sizeof(shakeBuf), "%d ticks", assistant.autoShakeFrequencyTicks);
        drawSliderCol(0, 4, "Auto-Shake Cadence:", shakeBuf, (assistant.autoShakeFrequencyTicks - 1.0f) / 11.0f, {251, 191, 36, 255});

        char fastFakeBuf[32];
        if (assistant.fastFakeSpeedTicks <= 1) std::snprintf(fastFakeBuf, sizeof(fastFakeBuf), "1 tick (Hyper)");
        else std::snprintf(fastFakeBuf, sizeof(fastFakeBuf), "%d ticks", assistant.fastFakeSpeedTicks);
        drawSliderCol(0, 5, "Fast-Fake Speed [P]:", fastFakeBuf, (assistant.fastFakeSpeedTicks - 1.0f) / 9.0f, {245, 158, 11, 255});

        char extFakeBuf[32];
        int extT = std::clamp(assistant.fakeShotExtendTicks, 0, 15);
        SDL_Color extCol = (extT == 0) ? SDL_Color{52, 211, 153, 255}
                          : ((extT <= 5) ? SDL_Color{251, 191, 36, 255} : SDL_Color{239, 68, 68, 255});
        if (extT == 0) std::snprintf(extFakeBuf, sizeof(extFakeBuf), "Auto (Safe)");
        else if (extT <= 5) std::snprintf(extFakeBuf, sizeof(extFakeBuf), "+%dt (Safe)", extT);
        else std::snprintf(extFakeBuf, sizeof(extFakeBuf), "+%dt (Unsafe!)", extT);
        drawSliderCol(0, 6, "Fake Extend Hold:", extFakeBuf, extT / 15.0f, extCol);

        char aimDistBuf[32];
        std::snprintf(aimDistBuf, sizeof(aimDistBuf), "%d u", std::clamp(assistant.aimLineDistance, 20, 300));
        drawSliderCol(1, 0, "Aim Line Distance:", aimDistBuf, (std::clamp(assistant.aimLineDistance, 20, 300) - 20.0f) / 280.0f, {34, 235, 115, 255});

        char spamBuf[32];
        std::snprintf(spamBuf, sizeof(spamBuf), "%d ms", assistant.typingSpamSpeedMs);
        drawSliderCol(1, 1, "Typing Strobe Speed:", spamBuf, (assistant.typingSpamSpeedMs - 5.0f) / 495.0f, {245, 158, 11, 255});

        char orbitBuf[32];
        if (assistant.ballOrbitDistance <= 0) std::snprintf(orbitBuf, sizeof(orbitBuf), "0 (Surface)");
        else std::snprintf(orbitBuf, sizeof(orbitBuf), "+%d gap", assistant.ballOrbitDistance);
        drawSliderCol(1, 2, "Moon Orbit Radius [O]:", orbitBuf, assistant.ballOrbitDistance / 15.0f, {56, 189, 248, 255});

        char opBuf[32];
        std::snprintf(opBuf, sizeof(opBuf), "%d%%", (int)std::round(assistant.lineOpacity * 100.0f));
        drawSliderCol(1, 3, "Aim Line Opacity:", opBuf, (assistant.lineOpacity - 0.10f) / 0.90f, {56, 189, 248, 255});

        char tkBuf[32];
        std::snprintf(tkBuf, sizeof(tkBuf), "%d ticks", assistant.trajectoryPreviewTicks);
        drawSliderCol(1, 4, "Trajectory Lookahead:", tkBuf, (assistant.trajectoryPreviewTicks - 30.0f) / 150.0f, {52, 211, 153, 255});

        {
            float sx = x + 14.0f + 1 * (sColW + 10.0f);
            float sy = ry + 5 * sRowH;
            fontSmall.drawText(renderer, "Exclude Goal Color:", sx + 4.0f, sy + 1.0f, {226, 232, 240, 255}, TextAlign::Left, 0.82f);

            std::vector<uint32_t> goalColors;
            if (world.stadium) {
                goalColors = TacticalAssistant::getAvailableGoalColors(*world.stadium);
            }

            bool hasExcl = (assistant.autoGoalExcludeColor >= 0);
            char exBuf[24];
            SDL_Color exCol = {148, 163, 184, 255};
            if (hasExcl) {
                uint32_t c = static_cast<uint32_t>(assistant.autoGoalExcludeColor);
                std::snprintf(exBuf, sizeof(exBuf), "#%06X", c & 0xFFFFFFu);
                exCol = {
                    static_cast<Uint8>((c >> 16) & 0xFF),
                    static_cast<Uint8>((c >> 8) & 0xFF),
                    static_cast<Uint8>(c & 0xFF),
                    255
                };
            } else {
                std::snprintf(exBuf, sizeof(exBuf), "None");
            }
            float tagW = std::max(52.0f, std::strlen(exBuf) * 7.5f + 12.0f);
            float tagX = sx + sColW - tagW;
            drawRoundedRect(renderer, tagX, sy - 1.0f, tagW, 17.0f, 4.0f, {26, 36, 52, 240}, {51, 65, 85, 180}, 1.0f);
            fontSmall.drawTextShadow(renderer, exBuf, tagX + tagW * 0.5f, sy + 1.5f, exCol, TextAlign::Center, 0.82f);

            float swY = sy + 16.0f;
            float swW = 22.0f;
            float swH = 13.0f;
            float swGap = 6.0f;
            if (goalColors.empty()) {
                fontSmall.drawText(renderer, "No goals in match", sx + 4.0f, swY, {100, 116, 139, 255}, TextAlign::Left, 0.78f);
            } else {
                for (size_t i = 0; i < goalColors.size(); ++i) {
                    float swX = sx + 4.0f + static_cast<float>(i) * (swW + swGap);
                    if (swX + swW > sx + sColW) break;
                    uint32_t c = goalColors[i];
                    SDL_Color fill = {
                        static_cast<Uint8>((c >> 16) & 0xFF),
                        static_cast<Uint8>((c >> 8) & 0xFF),
                        static_cast<Uint8>(c & 0xFF),
                        255
                    };
                    bool selected = (assistant.autoGoalExcludeColor == static_cast<int32_t>(c));
                    SDL_Color border = selected ? SDL_Color{255, 255, 255, 255} : SDL_Color{71, 85, 105, 200};
                    drawRoundedRect(renderer, swX, swY, swW, swH, 4.0f, fill, border, selected ? 2.0f : 1.0f);
                    if (selected) {
                        drawAALine(renderer, swX + 4.0f, swY + 3.0f, swX + swW - 4.0f, swY + swH - 3.0f, 2.0f, {239, 68, 68, 255});
                        drawAALine(renderer, swX + swW - 4.0f, swY + 3.0f, swX + 4.0f, swY + swH - 3.0f, 2.0f, {239, 68, 68, 255});
                    }
                }
            }
        }

        ry += 7 * sRowH + 6.0f;

        drawRoundedRect(renderer, x + 14.0f, ry, w - 28.0f, 23.0f, 5.0f, {15, 23, 38, 230}, {51, 65, 85, 180}, 1.0f);
        fontSmall.drawText(renderer, "Hotkeys: [0] Auto-Goal  •  [6] Auto-Save  •  [7] Smart GK  •  [P] Fast-Fake  •  [O] Orbit",
                           x + w * 0.5f, ry + 4.5f, {56, 189, 248, 255}, TextAlign::Center, 0.82f);

    } else if (activeMenuTab == 2) {

        float stepY = 26.0f;
        auto drawSelectorRow = [&](const std::string& key, const std::string& val, SDL_Color valCol) {
            drawRoundedRect(renderer, x + 14.0f, ry, w - 28.0f, 23.0f, 5.0f, {22, 26, 42, 210}, {50, 60, 90, 160}, 1.0f);
            fontSmall.drawText(renderer, key, x + 24.0f, ry + 4.0f, {226, 232, 240, 255}, TextAlign::Left, 0.90f);
            fontSmall.drawTextShadow(renderer, val, x + w - 24.0f, ry + 4.0f, valCol, TextAlign::Right, 0.90f);
            ry += stepY;
        };

        const char* rotNames[] = {"0° Horizontal", "90° Vertical ↑", "180° Horizontal", "270° Vertical ↓", "Auto Attack-Up ↑"};
        drawSelectorRow("Field Rotation [R]:", rotNames[settings.fieldRotation % 5], {251, 191, 36, 255});

        const char* themeNames[] = {"Auto", "Pro Grass", "Ice Rink", "Midnight Neon", "Street Asphalt", "Royal Gold"};
        drawSelectorRow("Pitch Theme:", themeNames[settings.pitchTheme % 6], {52, 211, 153, 255});

        const char* trailNames[] = {"OFF", "Auto-Team", "Neon Comet", "Fire Speed", "Pure White", "Rainbow Ghost"};
        drawSelectorRow("Ball Trail Effect:", trailNames[settings.ballTrailStyle % 6], {56, 189, 248, 255});

        drawSelectorRow("Team Jersey Colors [F4]:", settings.showTeamColors ? "ON" : "OFF", {52, 211, 153, 255});
        drawSelectorRow("Player Avatars [F6]:", settings.showAvatars ? "ON" : "OFF", {52, 211, 153, 255});
        drawSelectorRow("Player IDs [F5]:", settings.showPlayerIds ? "ON" : "OFF", {52, 211, 153, 255});
        drawSelectorRow("Local Player Halo [F7]:", settings.showLocalHalo ? "ON" : "OFF", {52, 211, 153, 255});

        ry += 4.0f;

        char trBuf[32];
        std::snprintf(trBuf, sizeof(trBuf), "%d%%", (int)std::round(settings.ballTrailOpacity * 100.0f));
        drawSliderRow(renderer, x, ry, w, "Ball Trail Opacity:",
                      trBuf, (settings.ballTrailOpacity - 0.15f) / 0.85f, {56, 189, 248, 255});
        ry += 32.0f;

        char outBuf[32];
        std::snprintf(outBuf, sizeof(outBuf), "%.1f px", settings.discOutlineWidth);
        drawSliderRow(renderer, x, ry, w, "Player Outline Width:",
                      outBuf, (settings.discOutlineWidth - 1.0f) / 3.5f, {192, 132, 252, 255});
        ry += 32.0f;

        char haloBuf[32];
        std::snprintf(haloBuf, sizeof(haloBuf), "%d%%", (int)std::round(settings.localHaloOpacity * 100.0f));
        drawSliderRow(renderer, x, ry, w, "Local Player Halo Glow:",
                      haloBuf, (settings.localHaloOpacity - 0.10f) / 0.90f, {45, 212, 191, 255});
        ry += 34.0f;

        float startEx = x + 16.0f;
        float eBtnW = (w - 32.0f) / 14.0f;
        for (int i = 0; i < 14; ++i) {
            float ex = startEx + i * eBtnW;
            bool sel = (settings.pendingAvatarChange == kQuickEmojis[i]);
            drawRoundedRect(renderer, ex, ry, eBtnW - 3.0f, 26.0f, 5.0f,
                            sel ? SDL_Color{88, 28, 135, 255} : SDL_Color{24, 30, 48, 240},
                            sel ? SDL_Color{192, 132, 252, 255} : SDL_Color{60, 75, 105, 190}, 1.2f);
            font.drawText(renderer, kQuickEmojis[i], ex + (eBtnW - 3.0f) * 0.5f, ry + 4.0f, {255, 255, 255, 255}, TextAlign::Center, 0.90f);
        }
    }

    SDL_RenderSetClipRect(renderer, nullptr);

    if (maxScroll > 1.0f) {
        float trackX = x + w - 7.0f;
        float trackY = y + 76.0f;
        float trackH = std::max(20.0f, h - 86.0f);
        drawRoundedRect(renderer, trackX, trackY, 4.0f, trackH, 2.0f, {25, 35, 52, 180});
        float thumbH = std::max(28.0f, trackH * (h / desiredH));
        float thumbY = trackY + (trackH - thumbH) * (settingsScrollY / maxScroll);
        drawRoundedRect(renderer, trackX, thumbY, 4.0f, thumbH, 2.0f, accentCol);
    }
}

OverlayAction GameRenderer::handleOverlayDrag(int mx, int my, int screenW, int screenH) {
    if (!showSettingsPanel || useImGuiDashboard) return OverlayAction::None;

    float w = (activeMenuTab == 1) ? 580.0f : 520.0f;
    float desiredH = (activeMenuTab == 1) ? 620.0f : 510.0f;
    float h = std::min(desiredH, std::max(260.0f, static_cast<float>(screenH) - 48.0f));
    float maxScroll = std::max(0.0f, desiredH - h);
    settingsScrollY = std::clamp(settingsScrollY, 0.0f, maxScroll);
    float x = (screenW - w) * 0.5f;
    float y = (screenH - h) * 0.5f;
    if (mx < x || mx > x + w || my < y + 72.0f || my > y + h) return OverlayAction::None;

    float ry = y + 78.0f - settingsScrollY;

    if (activeMenuTab == 0) {

        float barX = x + 16.0f;
        float barW = w - 32.0f;
        if (mx < barX - 8.0f || mx > barX + barW + 8.0f) return OverlayAction::None;
        float pct = std::clamp((mx - barX) / barW, 0.0f, 1.0f);

        if (my >= ry + 12.0f && my <= ry + 34.0f) {
            int rawMs = static_cast<int>(std::round(-200.0f + pct * 700.0f));
            settings.extrapolationMs = (rawMs / 5) * 5;
            return OverlayAction::ExtrapolationChanged;
        }
        ry += 34.0f + 28.0f;

        if (my >= ry + 12.0f && my <= ry + 34.0f) {
            settings.handicapMs = static_cast<int>(std::round(pct * 250.0f));
            return OverlayAction::ExtrapolationChanged;
        }
        ry += 34.0f;

        if (my >= ry + 12.0f && my <= ry + 34.0f) {
            settings.fpsCap = static_cast<int>(std::round(60.0f + pct * 440.0f));
            return OverlayAction::AssistanceChanged;
        }
        ry += 34.0f;

        if (my >= ry + 12.0f && my <= ry + 34.0f) {
            setZoom(0.45f + pct * 2.15f);
            return OverlayAction::AssistanceChanged;
        }
        ry += 34.0f;

        if (my >= ry + 12.0f && my <= ry + 34.0f) {
            settings.cameraSmoothness = 0.05f + pct * 0.95f;
            return OverlayAction::AssistanceChanged;
        }
    } else if (activeMenuTab == 1) {

        float colW = (w - 38.0f) * 0.5f;
        float sRowH = 32.0f;
        float slidersY = ry + 10 * 24.0f + 6.0f;

        int colIdx = -1;
        float colX0 = x + 14.0f;
        float colX1 = colX0 + colW + 10.0f;
        float barW = colW - 8.0f;
        float curBarX = 0.0f;

        if (mx >= colX0 - 4.0f && mx <= colX0 + colW + 4.0f) {
            colIdx = 0;
            curBarX = colX0 + 4.0f;
        } else if (mx >= colX1 - 4.0f && mx <= colX1 + colW + 4.0f) {
            colIdx = 1;
            curBarX = colX1 + 4.0f;
        }

        if (colIdx >= 0) {
            float pct = std::clamp((mx - curBarX) / barW, 0.0f, 1.0f);

            for (int r = 0; r < 7; ++r) {
                float sy = slidersY + r * sRowH;
                if (my >= sy + 10.0f && my <= sy + 32.0f) {
                    if (colIdx == 0) {
                        switch (r) {
                            case 0: {
                                assistant.autoGoalAccuracy = 30 + static_cast<int>(std::round(pct * 70.0f));
                                return OverlayAction::AssistanceChanged;
                            }
                            case 1: {
                                int rawMs = static_cast<int>(std::round(50.0f + pct * 950.0f));
                                assistant.fakeShotIntervalMs = (rawMs / 10) * 10;
                                return OverlayAction::AssistanceChanged;
                            }
                            case 2: {
                                int rawMs = static_cast<int>(std::round(pct * 1000.0f));
                                assistant.realShotIntervalMs = (rawMs <= 5) ? 0 : (rawMs / 5) * 5;
                                return OverlayAction::AssistanceChanged;
                            }
                            case 3: {
                                assistant.slowSpeedPercent = 10 + static_cast<int>(std::round(pct * 80.0f));
                                return OverlayAction::AssistanceChanged;
                            }
                            case 4: {
                                assistant.autoShakeFrequencyTicks = 1 + static_cast<int>(std::round(pct * 11.0f));
                                return OverlayAction::AssistanceChanged;
                            }
                            case 5: {
                                assistant.fastFakeSpeedTicks = 1 + static_cast<int>(std::round(pct * 9.0f));
                                return OverlayAction::AssistanceChanged;
                            }
                            case 6: {
                                assistant.fakeShotExtendTicks = static_cast<int>(std::round(pct * 15.0f));
                                return OverlayAction::AssistanceChanged;
                            }
                        }
                    } else if (colIdx == 1) {
                        switch (r) {
                            case 0: {
                                int rawDist = static_cast<int>(std::round(20.0f + pct * 280.0f));
                                assistant.aimLineDistance = std::clamp((rawDist / 5) * 5, 20, 300);
                                return OverlayAction::AssistanceChanged;
                            }
                            case 1: {
                                assistant.typingSpamSpeedMs = 5 + static_cast<int>(std::round(pct * 495.0f));
                                return OverlayAction::AssistanceChanged;
                            }
                            case 2: {
                                assistant.ballOrbitDistance = static_cast<int>(std::round(pct * 15.0f));
                                return OverlayAction::AssistanceChanged;
                            }
                            case 3: {
                                assistant.lineOpacity = 0.10f + pct * 0.90f;
                                return OverlayAction::AssistanceChanged;
                            }
                            case 4: {
                                assistant.trajectoryPreviewTicks = static_cast<int>(std::round(30.0f + pct * 150.0f));
                                return OverlayAction::AssistanceChanged;
                            }
                        }
                    }
                }
            }
        }
    } else if (activeMenuTab == 2) {

        float barX = x + 16.0f;
        float barW = w - 32.0f;
        if (mx < barX - 8.0f || mx > barX + barW + 8.0f) return OverlayAction::None;
        float pct = std::clamp((mx - barX) / barW, 0.0f, 1.0f);

        ry += 26.0f * 7.0f + 4.0f;

        if (my >= ry + 12.0f && my <= ry + 34.0f) {
            settings.ballTrailOpacity = 0.15f + pct * 0.85f;
            return OverlayAction::AssistanceChanged;
        }
        ry += 32.0f;

        if (my >= ry + 12.0f && my <= ry + 34.0f) {
            settings.discOutlineWidth = 1.0f + pct * 3.5f;
            return OverlayAction::AssistanceChanged;
        }
        ry += 32.0f;

        if (my >= ry + 12.0f && my <= ry + 34.0f) {
            settings.localHaloOpacity = 0.10f + pct * 0.90f;
            return OverlayAction::AssistanceChanged;
        }
    }
    return OverlayAction::None;
}

OverlayAction GameRenderer::handleOverlayClick(int mx, int my, int screenW, int screenH, const World& world, uint32_t myPlayerId, bool isSandbox) {

    if (isChatOpen) {
        float boxX = 14.0f;
        float boxW = std::min(560.0f, screenW * 0.48f);
        float baseBotY = screenH - 14.0f;
        float ebY = baseBotY - 34.0f - 30.0f + 3.0f;
        float startEx = boxX + 66.0f;
        if (my >= ebY && my <= ebY + 26.0f && mx >= startEx && mx <= boxX + boxW) {
            int idx = static_cast<int>((mx - startEx) / 28.0f);
            if (idx >= 0 && idx < 14 && chatInputBuffer.size() < 130) {
                chatInputBuffer += kQuickEmojis[idx];
                return OverlayAction::AssistanceChanged;
            }
        }
    }

    if (mx >= 14 && mx <= 349 && my >= 12 && my <= 44) {
        if (mx < 105) {
            showRoomMenu = !showRoomMenu;
        } else {
            showSettingsPanel = !showSettingsPanel;
        }
        return OverlayAction::AssistanceChanged;
    }

    float teleW = 285.0f;
    float teleX = screenW - teleW - 14.0f;
    if (my >= 31 && my <= 54) {
        if (mx >= teleX + 146.0f && mx <= teleX + 180.0f) {
            settings.extrapolationMs = std::clamp(settings.extrapolationMs - 5, -200, 500);
            return OverlayAction::ExtrapolationChanged;
        }
        if (mx >= teleX + 185.0f && mx <= teleX + 219.0f) {
            settings.extrapolationMs = std::clamp(settings.extrapolationMs + 5, -200, 500);
            return OverlayAction::ExtrapolationChanged;
        }
        if (mx >= teleX + 224.0f && mx <= teleX + 274.0f) {
            settings.fieldRotation = (settings.fieldRotation + 1) % 5;
            return OverlayAction::AssistanceChanged;
        }
    }

    if (showSettingsPanel && !useImGuiDashboard) {
        float w = (activeMenuTab == 1) ? 580.0f : 520.0f;
        float desiredH = (activeMenuTab == 1) ? 620.0f : 510.0f;
        float h = std::min(desiredH, std::max(260.0f, static_cast<float>(screenH) - 48.0f));
        float maxScroll = std::max(0.0f, desiredH - h);
        settingsScrollY = std::clamp(settingsScrollY, 0.0f, maxScroll);
        float x = (screenW - w) * 0.5f;
        float y = (screenH - h) * 0.5f;

        if (mx >= x && mx <= x + w && my >= y && my <= y + h) {

            float closeX = x + w - 38.0f;
            float closeY = y + 10.0f;
            if (mx >= closeX && mx <= closeX + 28.0f && my >= closeY && my <= closeY + 24.0f) {
                showSettingsPanel = false;
                return OverlayAction::AssistanceChanged;
            }

            float tabHeaderY = y + 26.0f;
            if (my >= tabHeaderY + 10.0f && my <= tabHeaderY + 42.0f && mx >= x + 12.0f && mx <= x + w - 12.0f) {
                float tabW = (w - 36.0f) / 3.0f;
                int tabIdx = static_cast<int>((mx - (x + 12.0f)) / (tabW + 6.0f));
                if (tabIdx >= 0 && tabIdx <= 2) {
                    activeMenuTab = tabIdx;
                    settingsScrollY = 0.0f;
                    return OverlayAction::AssistanceChanged;
                }
            }

            auto dragRes = handleOverlayDrag(mx, my, screenW, screenH);
            if (dragRes != OverlayAction::None) return dragRes;

            float ry = y + 78.0f - settingsScrollY;

            if (activeMenuTab == 0) {

                float by = ry + 34.0f;
                float bx = x + 16.0f;
                float bw = (w - 32.0f - 30.0f) / 7.0f;
                const int deltas[7] = {-25, -5, 0, +5, +25, +60, +120};
                if (my >= by && my <= by + 24.0f) {
                    for (int i = 0; i < 7; ++i) {
                        float bLeft = bx + i * (bw + 5.0f);
                        if (mx >= bLeft && mx <= bLeft + bw) {
                            if (i == 2) settings.extrapolationMs = 0;
                            else settings.extrapolationMs = std::clamp(settings.extrapolationMs + deltas[i], -200, 500);
                            return OverlayAction::ExtrapolationChanged;
                        }
                    }
                }

                float clickY = ry + 34.0f + 28.0f + 34.0f * 3.0f + 36.0f;
                float stepY = 28.0f;
                if (mx >= x + 14.0f && mx <= x + w - 14.0f && my >= clickY && my < clickY + stepY * 6.0f) {
                    int idx = static_cast<int>((my - clickY) / stepY);
                    switch (idx) {
                        case 0: settings.cameraMode = (settings.cameraMode + 1) % 4; break;
                        case 1: settings.fieldRotation = (settings.fieldRotation + 1) % 5; break;
                        case 2: settings.vsync = !settings.vsync; return OverlayAction::ToggleVSync;
                        case 3: settings.showNetGraph = !settings.showNetGraph; break;
                        case 4: settings.showInvisibleSegments = !settings.showInvisibleSegments; break;
                        case 5: settings.showDesyncGhost = !settings.showDesyncGhost; break;
                    }
                }
                return OverlayAction::AssistanceChanged;

            } else if (activeMenuTab == 1) {

                float colW = (w - 38.0f) * 0.5f;
                float rowH = 24.0f;
                float colX0 = x + 14.0f;
                float colX1 = colX0 + colW + 10.0f;

                if (my >= ry && my < ry + rowH * 10.0f) {
                    int r = static_cast<int>((my - ry) / rowH);
                    if (mx >= colX0 && mx <= colX0 + colW && r < 10) {
                        switch (r) {
                            case 0: assistant.showLiveAimTrajectory = !assistant.showLiveAimTrajectory; break;
                            case 1: assistant.showBallRollTrajectory = !assistant.showBallRollTrajectory; break;
                            case 2: assistant.autoGoalEnabled = !assistant.autoGoalEnabled; break;
                            case 3: assistant.autoGoalAnyGoal = !assistant.autoGoalAnyGoal; break;
                            case 4: assistant.autoSaveEnabled = !assistant.autoSaveEnabled; break;
                            case 5: assistant.firstTouchCushionEnabled = !assistant.firstTouchCushionEnabled; break;
                            case 6: assistant.autoShakeEnabled = !assistant.autoShakeEnabled; break;
                            case 7: assistant.ballOrbitEnabled = !assistant.ballOrbitEnabled; break;
                            case 8: assistant.showGkDangerCone = !assistant.showGkDangerCone; break;
                            case 9: assistant.fakeShotExtendEnabled = !assistant.fakeShotExtendEnabled; break;
                        }
                        return OverlayAction::AssistanceChanged;
                    } else if (mx >= colX1 && mx <= colX1 + colW && r < 10) {
                        switch (r) {
                            case 0: assistant.fakeTypingActive = !assistant.fakeTypingActive; break;
                            case 1: assistant.typingSpamEnabled = !assistant.typingSpamEnabled; break;
                            case 2: assistant.fakeShotEnabled = !assistant.fakeShotEnabled; break;
                            case 3: assistant.realShotEnabled = !assistant.realShotEnabled; break;
                            case 4: assistant.antiFakeShotFilter = !assistant.antiFakeShotFilter; break;
                            case 5: assistant.rsHelperEnabled = !assistant.rsHelperEnabled; break;
                            case 6: assistant.fastFakeEnabled = !assistant.fastFakeEnabled; break;
                            case 7: {
                                assistant.smartGkEnabled = !assistant.smartGkEnabled;
                                if (assistant.smartGkEnabled) assistant.penaltyGkEnabled = false;
                                break;
                            }
                            case 8: assistant.showOtherGoalsCone = !assistant.showOtherGoalsCone; break;
                            case 9: assistant.antiOpMode = !assistant.antiOpMode; break;
                        }
                        return OverlayAction::AssistanceChanged;
                    }
                }

                float slidersY = ry + 10 * rowH + 6.0f;
                float sRowH = 32.0f;
                float sx = colX1;
                float sy = slidersY + 5 * sRowH;
                if (mx >= sx && mx <= sx + colW && my >= sy && my <= sy + sRowH && world.stadium) {
                    if (mx >= sx + colW - 60.0f && my < sy + 16.0f) {
                        assistant.autoGoalExcludeColor = -1;
                        return OverlayAction::AssistanceChanged;
                    }
                    auto goalColors = TacticalAssistant::getAvailableGoalColors(*world.stadium);
                    float swW = 22.0f;
                    float swGap = 6.0f;
                    for (size_t i = 0; i < goalColors.size(); ++i) {
                        float swX = sx + 4.0f + static_cast<float>(i) * (swW + swGap);
                        if (swX + swW > sx + colW) break;
                        if (mx >= swX && mx <= swX + swW && my >= sy + 14.0f && my <= sy + sRowH) {
                            int32_t c = static_cast<int32_t>(goalColors[i]);
                            if (assistant.autoGoalExcludeColor == c) {
                                assistant.autoGoalExcludeColor = -1;
                            } else {
                                assistant.autoGoalExcludeColor = c;
                                assistant.autoGoalAnyGoal = true;
                            }
                            return OverlayAction::AssistanceChanged;
                        }
                    }
                }
                return OverlayAction::AssistanceChanged;
            } else if (activeMenuTab == 2) {

                float stepY = 26.0f;
                if (mx >= x + 14.0f && mx <= x + w - 14.0f && my >= ry && my < ry + stepY * 7.0f) {
                    int idx = static_cast<int>((my - ry) / stepY);
                    switch (idx) {
                        case 0: settings.fieldRotation = (settings.fieldRotation + 1) % 5; break;
                        case 1: settings.pitchTheme = (settings.pitchTheme + 1) % 6; break;
                        case 2: settings.ballTrailStyle = (settings.ballTrailStyle + 1) % 6; break;
                        case 3: settings.showTeamColors = !settings.showTeamColors; break;
                        case 4: settings.showAvatars = !settings.showAvatars; break;
                        case 5: settings.showPlayerIds = !settings.showPlayerIds; break;
                        case 6: settings.showLocalHalo = !settings.showLocalHalo; break;
                    }
                    return OverlayAction::AssistanceChanged;
                }

                float ey = ry + stepY * 7.0f + 4.0f + 32.0f * 3.0f + 34.0f;
                float startEx = x + 16.0f;
                float eBtnW = (w - 32.0f) / 14.0f;
                if (my >= ey && my <= ey + 28.0f && mx >= startEx && mx <= x + w - 16.0f) {
                    int idx = static_cast<int>((mx - startEx) / eBtnW);
                    if (idx >= 0 && idx < 14) {
                        settings.pendingAvatarChange = kQuickEmojis[idx];
                        return OverlayAction::AvatarChanged;
                    }
                }
                return OverlayAction::AssistanceChanged;
            }
        }
    }

    if (world.hasMatch && !showRoomMenu) return OverlayAction::None;

    const Player* me = world.getPlayer(myPlayerId);
    bool isAdmin = isSandbox || (me && me->admin);
    bool canChangeTeam = isAdmin || (!world.teamsLocked && !world.hasMatch);

    float modalW = std::min(780.0f, screenW - 40.0f);
    float modalH = std::min(490.0f, screenH - 80.0f);
    float x0 = (screenW - modalW) * 0.5f;
    float y0 = (screenH - modalH) * 0.5f;

    if (mx >= x0 + modalW - 124.0f && mx <= x0 + modalW - 16.0f &&
        my >= y0 + 13.0f && my <= y0 + 43.0f) {
        return OverlayAction::LeaveRoom;
    }

    if (canChangeTeam) {
        float colW = (modalW - 64.0f) / 3.0f;
        float colY = y0 + 64.0f;
        if (my >= colY && my <= colY + 36.0f) {
            if (mx >= x0 + 16.0f && mx <= x0 + 16.0f + colW) return OverlayAction::JoinRed;
            if (mx >= x0 + 32.0f + colW && mx <= x0 + 32.0f + colW * 2.0f) return OverlayAction::JoinSpectators;
            if (mx >= x0 + 48.0f + colW * 2.0f && mx <= x0 + 48.0f + colW * 3.0f) return OverlayAction::JoinBlue;
        }
    }

    if (isAdmin) {
        float btnY = y0 + modalH - 48.0f;
        float btnW = 145.0f;
        if (my >= btnY && my <= btnY + 34.0f) {
            float midX = x0 + modalW * 0.5f;
            if (mx >= midX - btnW - 85.0f && mx <= midX - 85.0f) return OverlayAction::StartStopGame;
            if (mx >= midX - 72.5f && mx <= midX + 72.5f) return OverlayAction::PauseResumeGame;
            if (mx >= midX + 85.0f && mx <= midX + 85.0f + btnW) return OverlayAction::AutoTeams;
        }
    }

    return OverlayAction::None;
}

}
