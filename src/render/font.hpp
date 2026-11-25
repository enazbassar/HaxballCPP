#pragma once

#include <SDL2/SDL.h>
#include <string>
#include <vector>
#include <cstdint>

namespace haxball {

enum class TextAlign {
    Left,
    Center,
    Right
};

class FontRenderer {
public:
    FontRenderer();
    ~FontRenderer();

    bool init(SDL_Renderer* renderer, const std::string& fontPath, float fontSize = 18.0f);
    void drawText(SDL_Renderer* renderer, const std::string& text, float x, float y, SDL_Color color, TextAlign align = TextAlign::Left, float scale = 1.0f);
    void drawTextShadow(SDL_Renderer* renderer, const std::string& text, float x, float y, SDL_Color color, TextAlign align = TextAlign::Left, float scale = 1.0f, float shadowOffset = 1.5f);
    float measureText(const std::string& text, float scale = 1.0f);
    float getLineHeight(float scale = 1.0f) const { return fontSize * scale; }

    static std::vector<uint32_t> decodeNormalizedUTF8(const std::string& text);
    static std::string clipUTF8Codepoints(const std::string& text, size_t maxCodepoints);
    static size_t countUTF8Codepoints(const std::string& text);
    static bool decodeColorEmojiRGBA(uint32_t codepoint, int& outW, int& outH, std::vector<uint32_t>& outRGBA);

private:
    void drawDecodedSpan(SDL_Renderer* renderer, const uint32_t* cps, size_t count, float totalW, float x, float y, SDL_Color color, TextAlign align, float scale, bool isShadowPass);
    float measureDecodedSpan(const uint32_t* cps, size_t count, float scale) const;

    float fontSize = 18.0f;
    SDL_Texture* fontAtlas = nullptr;
    int atlasWidth = 1024;
    int atlasHeight = 512;

    struct GlyphInfo {
        SDL_Rect srcRect{0, 0, 0, 0};
        float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
        float xOffset = 0.0f;
        float yOffset = 0.0f;
        float xAdvance = 0.0f;
    };
    std::vector<GlyphInfo> glyphs;
};

}
