#include "font.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>
#include <zlib.h>

#include <fstream>
#include <iostream>
#include <unordered_map>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace haxball {

namespace {

static uint16_t readU16BE(const uint8_t* p) {
    return (static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]);
}

static uint32_t readU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
           static_cast<uint32_t>(p[3]);
}

static uint8_t paethPredictor(int a, int b, int c) {
    int p = a + b - c;
    int pa = std::abs(p - a);
    int pb = std::abs(p - b);
    int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return static_cast<uint8_t>(a);
    if (pb <= pc) return static_cast<uint8_t>(b);
    return static_cast<uint8_t>(c);
}

static bool decodeEmbeddedPngRGBA(const uint8_t* pngData, size_t pngLen, int& outW, int& outH, std::vector<uint32_t>& outRGBA) {
    if (pngLen < 33 || std::memcmp(pngData, "\x89PNG\r\n\x1a\n", 8) != 0) return false;

    outW = static_cast<int>(readU32BE(pngData + 16));
    outH = static_cast<int>(readU32BE(pngData + 20));
    uint8_t bitDepth = pngData[24];
    uint8_t colorType = pngData[25];
    uint8_t interlace = pngData[28];

    if (outW <= 0 || outH <= 0 || outW > 512 || outH > 512 || interlace != 0) return false;
    if (!((bitDepth == 8 && colorType == 6) || (bitDepth == 8 && colorType == 3) || (bitDepth == 4 && colorType == 3))) {
        return false;
    }

    uint8_t palette[256][4];
    for (int i = 0; i < 256; ++i) {
        palette[i][0] = 0; palette[i][1] = 0; palette[i][2] = 0; palette[i][3] = 255;
    }

    std::vector<uint8_t> idatCompressed;
    idatCompressed.reserve(pngLen);

    size_t pos = 8;
    while (pos + 12 <= pngLen) {
        uint32_t chunkLen = readU32BE(pngData + pos);
        const uint8_t* chunkType = pngData + pos + 4;
        const uint8_t* chunkPayload = pngData + pos + 8;
        if (pos + 12 + chunkLen > pngLen) break;

        if (std::memcmp(chunkType, "PLTE", 4) == 0) {
            int entries = std::min<int>(256, chunkLen / 3);
            for (int i = 0; i < entries; ++i) {
                palette[i][0] = chunkPayload[i * 3 + 0];
                palette[i][1] = chunkPayload[i * 3 + 1];
                palette[i][2] = chunkPayload[i * 3 + 2];
            }
        } else if (std::memcmp(chunkType, "tRNS", 4) == 0) {
            int entries = std::min<int>(256, chunkLen);
            for (int i = 0; i < entries; ++i) {
                palette[i][3] = chunkPayload[i];
            }
        } else if (std::memcmp(chunkType, "IDAT", 4) == 0) {
            idatCompressed.insert(idatCompressed.end(), chunkPayload, chunkPayload + chunkLen);
        } else if (std::memcmp(chunkType, "IEND", 4) == 0) {
            break;
        }
        pos += 12 + chunkLen;
    }

    if (idatCompressed.empty()) return false;

    int bpp = (colorType == 6) ? 4 : 1;
    int stride = (colorType == 6) ? (outW * 4) : ((bitDepth == 8) ? outW : ((outW + 1) / 2));
    uLongf expectedRawSize = static_cast<uLongf>(outH * (1 + stride));
    std::vector<uint8_t> raw(expectedRawSize);

    if (uncompress(raw.data(), &expectedRawSize, idatCompressed.data(), idatCompressed.size()) != Z_OK) {
        return false;
    }

    std::vector<uint8_t> unfiltered(outH * stride, 0);
    for (int y = 0; y < outH; ++y) {
        uint8_t filterType = raw[y * (1 + stride)];
        const uint8_t* srcRow = &raw[y * (1 + stride) + 1];
        uint8_t* dstRow = &unfiltered[y * stride];
        const uint8_t* prevRow = (y > 0) ? &unfiltered[(y - 1) * stride] : nullptr;

        for (int x = 0; x < stride; ++x) {
            uint8_t rawByte = srcRow[x];
            uint8_t a = (x >= bpp) ? dstRow[x - bpp] : 0;
            uint8_t b = prevRow ? prevRow[x] : 0;
            uint8_t c = (prevRow && x >= bpp) ? prevRow[x - bpp] : 0;

            switch (filterType) {
                case 0: dstRow[x] = rawByte; break;
                case 1: dstRow[x] = static_cast<uint8_t>(rawByte + a); break;
                case 2: dstRow[x] = static_cast<uint8_t>(rawByte + b); break;
                case 3: dstRow[x] = static_cast<uint8_t>(rawByte + ((static_cast<int>(a) + static_cast<int>(b)) >> 1)); break;
                case 4: dstRow[x] = static_cast<uint8_t>(rawByte + paethPredictor(a, b, c)); break;
                default: dstRow[x] = rawByte; break;
            }
        }
    }

    outRGBA.resize(outW * outH);
    for (int y = 0; y < outH; ++y) {
        const uint8_t* row = &unfiltered[y * stride];
        for (int x = 0; x < outW; ++x) {
            uint8_t r = 0, g = 0, b = 0, a = 0;
            if (colorType == 6) {
                r = row[x * 4 + 0];
                g = row[x * 4 + 1];
                b = row[x * 4 + 2];
                a = row[x * 4 + 3];
            } else if (bitDepth == 8) {
                uint8_t idx = row[x];
                r = palette[idx][0];
                g = palette[idx][1];
                b = palette[idx][2];
                a = palette[idx][3];
            } else {
                uint8_t byteVal = row[x >> 1];
                uint8_t idx = (x & 1) ? (byteVal & 0x0F) : (byteVal >> 4);
                r = palette[idx][0];
                g = palette[idx][1];
                b = palette[idx][2];
                a = palette[idx][3];
            }

            outRGBA[y * outW + x] = static_cast<uint32_t>(r) |
                                    (static_cast<uint32_t>(g) << 8) |
                                    (static_cast<uint32_t>(b) << 16) |
                                    (static_cast<uint32_t>(a) << 24);
        }
    }
    return true;
}

struct CachedEmojiGlyph {
    bool valid = false;
    bool isColorEmoji = false;
    SDL_Texture* texture = nullptr;
    int width = 0;
    int height = 0;
};

class SharedEmojiSystem {
public:
    bool initialized = false;
    SDL_Renderer* boundRenderer = nullptr;
    std::vector<uint8_t> colorEmojiFontData;
    uint32_t cmapFmt12Offset = 0;
    uint32_t cblcOffset = 0;
    uint32_t cbdtOffset = 0;

    std::vector<uint8_t> symbolFontData;
    stbtt_fontinfo symbolFontInfo{};
    bool hasSymbolFont = false;

    std::unordered_map<uint32_t, CachedEmojiGlyph> glyphCache;

    void ensureInitialized(SDL_Renderer* renderer) {
        if (initialized && boundRenderer == renderer) return;
        if (boundRenderer != renderer) {
            for (auto& kv : glyphCache) {
                if (kv.second.texture) SDL_DestroyTexture(kv.second.texture);
            }
            glyphCache.clear();
            boundRenderer = renderer;
        }
        if (initialized) return;
        initialized = true;

        const char* emojiPaths[] = {
            "assets/fonts/NotoColorEmoji.ttf",
            "../assets/fonts/NotoColorEmoji.ttf",
            "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf"
        };
        for (const char* path : emojiPaths) {
            std::ifstream f(path, std::ios::binary | std::ios::ate);
            if (f.is_open()) {
                size_t sz = f.tellg();
                f.seekg(0, std::ios::beg);
                colorEmojiFontData.resize(sz);
                if (f.read(reinterpret_cast<char*>(colorEmojiFontData.data()), sz)) {
                    parseColorEmojiTables();
                    break;
                }
            }
        }

        const char* symPaths[] = {
            "assets/fonts/DejaVuSans.ttf",
            "../assets/fonts/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf"
        };
        for (const char* path : symPaths) {
            std::ifstream f(path, std::ios::binary | std::ios::ate);
            if (f.is_open()) {
                size_t sz = f.tellg();
                f.seekg(0, std::ios::beg);
                symbolFontData.resize(sz);
                if (f.read(reinterpret_cast<char*>(symbolFontData.data()), sz)) {
                    if (stbtt_InitFont(&symbolFontInfo, symbolFontData.data(), 0)) {
                        hasSymbolFont = true;
                        break;
                    }
                }
            }
        }
    }

    void parseColorEmojiTables() {
        if (colorEmojiFontData.size() < 64) return;
        const uint8_t* data = colorEmojiFontData.data();
        uint16_t numTables = readU16BE(data + 4);
        uint32_t cmapOffset = 0;

        for (uint16_t i = 0; i < numTables; ++i) {
            size_t rec = 12 + i * 16;
            if (rec + 16 > colorEmojiFontData.size()) break;
            const uint8_t* tag = data + rec;
            uint32_t off = readU32BE(data + rec + 8);
            if (std::memcmp(tag, "cmap", 4) == 0) cmapOffset = off;
            else if (std::memcmp(tag, "CBLC", 4) == 0) cblcOffset = off;
            else if (std::memcmp(tag, "CBDT", 4) == 0) cbdtOffset = off;
        }

        if (cmapOffset != 0 && cmapOffset + 4 < colorEmojiFontData.size()) {
            uint16_t numSub = readU16BE(data + cmapOffset + 2);
            for (uint16_t i = 0; i < numSub; ++i) {
                uint32_t subOff = readU32BE(data + cmapOffset + 4 + i * 8 + 4);
                if (cmapOffset + subOff + 16 < colorEmojiFontData.size()) {
                    uint16_t fmt = readU16BE(data + cmapOffset + subOff);
                    if (fmt == 12) {
                        cmapFmt12Offset = cmapOffset + subOff;
                        break;
                    }
                }
            }
        }
    }

    uint32_t findColorEmojiGlyphId(uint32_t cp) const {
        if (cmapFmt12Offset == 0 || cmapFmt12Offset + 16 > colorEmojiFontData.size()) return 0;
        const uint8_t* data = colorEmojiFontData.data();
        uint32_t nGroups = readU32BE(data + cmapFmt12Offset + 12);
        uint32_t lo = 0, hi = nGroups;
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2;
            size_t gOff = cmapFmt12Offset + 16 + mid * 12;
            if (gOff + 12 > colorEmojiFontData.size()) break;
            uint32_t startCp = readU32BE(data + gOff);
            uint32_t endCp   = readU32BE(data + gOff + 4);
            uint32_t startG  = readU32BE(data + gOff + 8);
            if (cp < startCp) {
                hi = mid;
            } else if (cp > endCp) {
                lo = mid + 1;
            } else {
                return startG + (cp - startCp);
            }
        }
        return 0;
    }

    bool decodeColorEmojiPixelData(uint32_t cp, int& outW, int& outH, std::vector<uint32_t>& outRGBA) {
        if (!initialized) ensureInitialized(nullptr);
        uint32_t gid = findColorEmojiGlyphId(cp);
        if (gid == 0 || cblcOffset == 0 || cbdtOffset == 0 || cblcOffset + 56 >= colorEmojiFontData.size()) {
            return false;
        }
        const uint8_t* data = colorEmojiFontData.data();
        uint32_t subArrOffset = cblcOffset + readU32BE(data + cblcOffset + 8);
        uint32_t numSubTables = readU32BE(data + cblcOffset + 16);

        for (uint32_t s = 0; s < numSubTables && subArrOffset + (s + 1) * 8 <= colorEmojiFontData.size(); ++s) {
            uint16_t firstG = readU16BE(data + subArrOffset + s * 8);
            uint16_t lastG  = readU16BE(data + subArrOffset + s * 8 + 2);
            uint32_t addOff = readU32BE(data + subArrOffset + s * 8 + 4);
            if (gid >= firstG && gid <= lastG) {
                size_t stOff = subArrOffset + addOff;
                if (stOff + 8 + (gid - firstG + 1) * 4 <= colorEmojiFontData.size()) {
                    uint16_t idxFmt = readU16BE(data + stOff);
                    uint16_t imgFmt = readU16BE(data + stOff + 2);
                    uint32_t imgDataOff = readU32BE(data + stOff + 4);
                    if (idxFmt == 1 && imgFmt == 17) {
                        uint32_t relOff = readU32BE(data + stOff + 8 + (gid - firstG) * 4);
                        size_t glyphOff = cbdtOffset + imgDataOff + relOff;
                        if (glyphOff + 9 <= colorEmojiFontData.size()) {
                            uint32_t pngLen = readU32BE(data + glyphOff + 5);
                            if (glyphOff + 9 + pngLen <= colorEmojiFontData.size()) {
                                return decodeEmbeddedPngRGBA(data + glyphOff + 9, pngLen, outW, outH, outRGBA);
                            }
                        }
                    }
                }
                break;
            }
        }
        return false;
    }

    const CachedEmojiGlyph* getGlyph(SDL_Renderer* renderer, uint32_t cp) {
        ensureInitialized(renderer);
        auto it = glyphCache.find(cp);
        if (it != glyphCache.end()) {
            return &it->second;
        }

        if (glyphCache.size() >= 512) {
            for (auto& kv : glyphCache) {
                if (kv.second.texture) SDL_DestroyTexture(kv.second.texture);
            }
            glyphCache.clear();
        }

        CachedEmojiGlyph entry;

        int cw = 0, ch = 0;
        std::vector<uint32_t> crgba;
        if (decodeColorEmojiPixelData(cp, cw, ch, crgba)) {
            SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, cw, ch);
            if (tex) {
                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
                SDL_UpdateTexture(tex, nullptr, crgba.data(), cw * sizeof(uint32_t));
                entry.valid = true;
                entry.isColorEmoji = true;
                entry.texture = tex;
                entry.width = cw;
                entry.height = ch;
            }
        }

        if (!entry.valid && hasSymbolFont) {
            int gIdx = stbtt_FindGlyphIndex(&symbolFontInfo, static_cast<int>(cp));
            if (gIdx != 0) {
                float scale = stbtt_ScaleForPixelHeight(&symbolFontInfo, 40.0f);
                int w = 0, h = 0, xoff = 0, yoff = 0;
                unsigned char* bmp = stbtt_GetGlyphBitmap(&symbolFontInfo, scale, scale, gIdx, &w, &h, &xoff, &yoff);
                if (bmp && w > 0 && h > 0) {
                    std::vector<uint32_t> rgba(w * h);
                    for (int i = 0; i < w * h; ++i) {
                        rgba[i] = (static_cast<uint32_t>(bmp[i]) << 24) | 0x00FFFFFF;
                    }
                    SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
                    if (tex) {
                        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
                        SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
                        SDL_UpdateTexture(tex, nullptr, rgba.data(), w * sizeof(uint32_t));
                        entry.valid = true;
                        entry.isColorEmoji = false;
                        entry.texture = tex;
                        entry.width = w;
                        entry.height = h;
                    }
                }
                if (bmp) stbtt_FreeBitmap(bmp, nullptr);
            }
        }

        auto res = glyphCache.emplace(cp, entry);
        return &res.first->second;
    }
};

static SharedEmojiSystem g_emojiSystem;

}

static uint32_t normalizeCodepoint(uint32_t cp) {
    if (cp == 0x3000 || cp == 0x00A0 || (cp >= 0x2000 && cp <= 0x200A)) return ' ';

    if (cp == 0x200B || cp == 0x200C || cp == 0x200D || cp == 0xFEFF) return 0;
    if (cp >= 0xFE00 && cp <= 0xFE0F) return 0;
    if (cp >= 0x1F3FB && cp <= 0x1F3FF) return 0;

    if (cp >= 0x0300 && cp <= 0x036F) return 0;

    if (cp >= 0xFF01 && cp <= 0xFF5E) return cp - 0xFEE0;

    static const uint32_t mathAlphaStarts[] = {
        0x1D400, 0x1D434, 0x1D468, 0x1D49C, 0x1D4D0, 0x1D504,
        0x1D538, 0x1D56C, 0x1D5A0, 0x1D5D4, 0x1D608, 0x1D63C, 0x1D670
    };
    for (uint32_t start : mathAlphaStarts) {
        if (cp >= start && cp < start + 26) return 'A' + (cp - start);
        if (cp >= start + 26 && cp < start + 52) return 'a' + (cp - (start + 26));
    }
    static const uint32_t mathDigitStarts[] = {
        0x1D7CE, 0x1D7D8, 0x1D7E2, 0x1D7EC, 0x1D7F6
    };
    for (uint32_t start : mathDigitStarts) {
        if (cp >= start && cp < start + 10) return '0' + (cp - start);
    }

    if (cp == 0x2502 || cp == 0x2503 || cp == 0x2506 || cp == 0x2507 ||
        cp == 0x250A || cp == 0x250B || cp == 0x2758 || cp == 0x2759 || cp == 0x275A) {
        return '|';
    }
    if (cp >= 0x2500 && cp <= 0x257F) return '-';
    if (cp >= 0x2010 && cp <= 0x2015) return '-';
    if (cp == 0x2018 || cp == 0x2019) return '\'';
    if (cp == 0x201C || cp == 0x201D) return '"';
    if (cp == 0x2026) return '.';

    switch (cp) {
        case 0x011E: return 'G'; case 0x011F: return 'g';
        case 0x0130: return 'I'; case 0x0131: return 'i';
        case 0x015E: return 'S'; case 0x015F: return 's';
        case 0x0152: return 'O'; case 0x0153: return 'o';
        case 0x0178: return 0xFF;
        default: break;
    }

    if (cp >= 0x1F150 && cp <= 0x1F169) return 'A' + (cp - 0x1F150);
    if (cp >= 0x1F170 && cp <= 0x1F189) return 'A' + (cp - 0x1F170);
    if (cp >= 0x24B6 && cp <= 0x24CF) return 'A' + (cp - 0x24B6);
    if (cp >= 0x24D0 && cp <= 0x24E9) return 'a' + (cp - 0x24D0);

    switch (cp) {
        case 0x1D00: return 'A'; case 0x0299: return 'B'; case 0x1D04: return 'C';
        case 0x1D05: return 'D'; case 0x1D07: return 'E'; case 0x0493: return 'F';
        case 0x0262: return 'G'; case 0x029C: return 'H'; case 0x026A: return 'I';
        case 0x1D0A: return 'J'; case 0x1D0B: return 'K'; case 0x029F: return 'L';
        case 0x1D0D: return 'M'; case 0x0274: return 'N'; case 0x1D0F: return 'O';
        case 0x1D18: return 'P'; case 0x0280: return 'R'; case 0xA731: return 'S';
        case 0x1D1B: return 'T'; case 0x1D1C: return 'U'; case 0x1D20: return 'V';
        case 0x1D21: return 'W'; case 0x028F: return 'Y'; case 0x1D22: return 'Z';
        default: break;
    }

    if (cp >= 32) return cp;
    return 0;
}

std::vector<uint32_t> FontRenderer::decodeNormalizedUTF8(const std::string& text) {
    std::vector<uint32_t> out;
    out.reserve(text.size());
    size_t i = 0;
    size_t n = text.size();
    while (i < n) {
        uint8_t b0 = (uint8_t)text[i];
        uint32_t cp = 0;
        if ((b0 & 0x80) == 0) {
            cp = b0;
            i += 1;
        } else if ((b0 & 0xE0) == 0xC0 && i + 1 < n) {
            cp = ((b0 & 0x1F) << 6) | ((uint8_t)text[i + 1] & 0x3F);
            i += 2;
        } else if ((b0 & 0xF0) == 0xE0 && i + 2 < n) {
            cp = ((b0 & 0x0F) << 12) | (((uint8_t)text[i + 1] & 0x3F) << 6) | ((uint8_t)text[i + 2] & 0x3F);
            i += 3;
        } else if ((b0 & 0xF8) == 0xF0 && i + 3 < n) {
            cp = ((b0 & 0x07) << 18) | (((uint8_t)text[i + 1] & 0x3F) << 12) | (((uint8_t)text[i + 2] & 0x3F) << 6) | ((uint8_t)text[i + 3] & 0x3F);
            i += 4;
        } else {
            i += 1;
            continue;
        }

        uint32_t norm = normalizeCodepoint(cp);
        if (norm != 0) {
            if (norm == ' ' && out.empty()) continue;
            out.push_back(norm);
        }
    }
    return out;
}

std::string FontRenderer::clipUTF8Codepoints(const std::string& text, size_t maxCodepoints) {
    size_t i = 0;
    size_t n = text.size();
    size_t count = 0;
    while (i < n && count < maxCodepoints) {
        uint8_t b0 = static_cast<uint8_t>(text[i]);
        uint32_t cp = 0;
        size_t step = 1;
        if ((b0 & 0x80) == 0) {
            cp = b0; step = 1;
        } else if ((b0 & 0xE0) == 0xC0 && i + 1 < n) {
            cp = ((b0 & 0x1F) << 6) | (static_cast<uint8_t>(text[i + 1]) & 0x3F); step = 2;
        } else if ((b0 & 0xF0) == 0xE0 && i + 2 < n) {
            cp = ((b0 & 0x0F) << 12) | ((static_cast<uint8_t>(text[i + 1]) & 0x3F) << 6) | (static_cast<uint8_t>(text[i + 2]) & 0x3F); step = 3;
        } else if ((b0 & 0xF8) == 0xF0 && i + 3 < n) {
            cp = ((b0 & 0x07) << 18) | ((static_cast<uint8_t>(text[i + 1]) & 0x3F) << 12) | ((static_cast<uint8_t>(text[i + 2]) & 0x3F) << 6) | (static_cast<uint8_t>(text[i + 3]) & 0x3F); step = 4;
        }
        i += step;

        if (cp >= 0xFE00 && cp <= 0xFE0F) continue;
        if (cp == 0x200D) continue;
        count++;

        if (i + 2 < n && static_cast<uint8_t>(text[i]) == 0xEF && static_cast<uint8_t>(text[i + 1]) == 0xB8) {
            i += 3;
        }
    }
    return text.substr(0, i);
}

size_t FontRenderer::countUTF8Codepoints(const std::string& text) {
    return decodeNormalizedUTF8(text).size();
}

FontRenderer::FontRenderer() {}

FontRenderer::~FontRenderer() {
    if (fontAtlas) {
        SDL_DestroyTexture(fontAtlas);
        fontAtlas = nullptr;
    }
}

bool FontRenderer::init(SDL_Renderer* renderer, const std::string& fontPath, float size) {
    if (fontAtlas) {
        SDL_DestroyTexture(fontAtlas);
        fontAtlas = nullptr;
    }
    fontSize = size;
    g_emojiSystem.ensureInitialized(renderer);

    std::ifstream file(fontPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[FontRenderer] Failed to open font: " << fontPath << std::endl;
        return false;
    }

    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> fontBuffer(fileSize);
    if (!file.read((char*)fontBuffer.data(), fileSize)) {
        return false;
    }

    atlasWidth = 1024;
    atlasHeight = 512;
    std::vector<unsigned char> tempBitmap(atlasWidth * atlasHeight, 0);
    constexpr int kNumChars = 224;
    stbtt_bakedchar cdata[kNumChars];

    int result = stbtt_BakeFontBitmap(fontBuffer.data(), 0, fontSize, tempBitmap.data(), atlasWidth, atlasHeight, 32, kNumChars, cdata);
    if (result <= 0) {
        std::cerr << "[FontRenderer] Failed to bake font bitmap, code: " << result << std::endl;
        return false;
    }

    std::vector<uint32_t> rgbaPixels(atlasWidth * atlasHeight);
    for (int i = 0; i < atlasWidth * atlasHeight; ++i) {
        uint8_t a = tempBitmap[i];
        rgbaPixels[i] = (static_cast<uint32_t>(a) << 24) | 0x00FFFFFF;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    fontAtlas = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, atlasWidth, atlasHeight);
    if (!fontAtlas) {
        std::cerr << "[FontRenderer] Failed to create SDL texture: " << SDL_GetError() << std::endl;
        return false;
    }

    SDL_SetTextureBlendMode(fontAtlas, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(fontAtlas, SDL_ScaleModeLinear);
    SDL_SetTextureColorMod(fontAtlas, 255, 255, 255);
    SDL_SetTextureAlphaMod(fontAtlas, 255);
    SDL_UpdateTexture(fontAtlas, nullptr, rgbaPixels.data(), atlasWidth * sizeof(uint32_t));

    const float invW = 1.0f / static_cast<float>(atlasWidth);
    const float invH = 1.0f / static_cast<float>(atlasHeight);
    glyphs.resize(kNumChars);
    for (int i = 0; i < kNumChars; ++i) {
        glyphs[i].srcRect = {cdata[i].x0, cdata[i].y0, cdata[i].x1 - cdata[i].x0, cdata[i].y1 - cdata[i].y0};
        glyphs[i].u0 = cdata[i].x0 * invW;
        glyphs[i].v0 = cdata[i].y0 * invH;
        glyphs[i].u1 = cdata[i].x1 * invW;
        glyphs[i].v1 = cdata[i].y1 * invH;
        glyphs[i].xOffset = cdata[i].xoff;
        glyphs[i].yOffset = cdata[i].yoff;
        glyphs[i].xAdvance = cdata[i].xadvance;
    }

    return true;
}

static size_t decodeNormalizedUTF8Into(const std::string& text, uint32_t* dst, size_t dstCap) {
    size_t i = 0;
    size_t n = text.size();
    size_t count = 0;
    while (i < n && count < dstCap) {
        uint8_t b0 = static_cast<uint8_t>(text[i]);
        uint32_t cp = 0;
        if ((b0 & 0x80) == 0) {
            cp = b0;
            i += 1;
        } else if ((b0 & 0xE0) == 0xC0 && i + 1 < n) {
            cp = ((b0 & 0x1F) << 6) | (static_cast<uint8_t>(text[i + 1]) & 0x3F);
            i += 2;
        } else if ((b0 & 0xF0) == 0xE0 && i + 2 < n) {
            cp = ((b0 & 0x0F) << 12) | ((static_cast<uint8_t>(text[i + 1]) & 0x3F) << 6) | (static_cast<uint8_t>(text[i + 2]) & 0x3F);
            i += 3;
        } else if ((b0 & 0xF8) == 0xF0 && i + 3 < n) {
            cp = ((b0 & 0x07) << 18) | ((static_cast<uint8_t>(text[i + 1]) & 0x3F) << 12) | ((static_cast<uint8_t>(text[i + 2]) & 0x3F) << 6) | (static_cast<uint8_t>(text[i + 3]) & 0x3F);
            i += 4;
        } else {
            i += 1;
            continue;
        }

        uint32_t norm = normalizeCodepoint(cp);
        if (norm != 0) {
            if (norm == ' ' && count == 0) continue;
            dst[count++] = norm;
        }
    }
    return count;
}

float FontRenderer::measureDecodedSpan(const uint32_t* cps, size_t count, float scale) const {
    float width = 0.0f;
    const int numGlyphs = static_cast<int>(glyphs.size());
    const float emAdvance = fontSize * 1.02f * scale;
    for (size_t i = 0; i < count; ++i) {
        uint32_t cp = cps[i];
        if (cp <= 255) {
            int idx = static_cast<int>(cp) - 32;
            if (idx >= 0 && idx < numGlyphs) {
                width += glyphs[idx].xAdvance * scale;
            }
        } else {
            width += emAdvance;
        }
    }
    return width;
}

float FontRenderer::measureText(const std::string& text, float scale) {
    if (text.empty()) return 0.0f;
    if (text.size() <= 256) {
        uint32_t stackCps[256];
        size_t cnt = decodeNormalizedUTF8Into(text, stackCps, 256);
        return measureDecodedSpan(stackCps, cnt, scale);
    }
    auto cps = decodeNormalizedUTF8(text);
    return measureDecodedSpan(cps.data(), cps.size(), scale);
}

void FontRenderer::drawDecodedSpan(SDL_Renderer* renderer, const uint32_t* cps, size_t count, float totalW, float x, float y, SDL_Color color, TextAlign align, float scale, bool isShadowPass) {
    if (!fontAtlas || count == 0) return;

    float startX = x;
    if (align == TextAlign::Center) {
        startX -= totalW * 0.5f;
    } else if (align == TextAlign::Right) {
        startX -= totalW;
    }

    thread_local std::vector<SDL_Vertex> batchVerts;
    thread_local std::vector<int> batchIndices;
    batchVerts.clear();
    batchIndices.clear();

    auto flushAtlasBatch = [&]() {
        if (!batchVerts.empty()) {
            SDL_RenderGeometry(renderer, fontAtlas,
                               batchVerts.data(), static_cast<int>(batchVerts.size()),
                               batchIndices.data(), static_cast<int>(batchIndices.size()));
            batchVerts.clear();
            batchIndices.clear();
        }
    };

    float curX = startX;
    float curY = y + fontSize * 0.82f * scale;
    const int numGlyphs = static_cast<int>(glyphs.size());
    const float emAdvance = fontSize * 1.02f * scale;

    for (size_t i = 0; i < count; ++i) {
        uint32_t cp = cps[i];
        if (cp <= 255) {
            int idx = static_cast<int>(cp) - 32;
            if (idx >= 0 && idx < numGlyphs) {
                const auto& g = glyphs[idx];
                if (g.srcRect.w > 0 && g.srcRect.h > 0) {
                    float x0 = curX + g.xOffset * scale;
                    float y0 = curY + g.yOffset * scale;
                    float x1 = x0 + g.srcRect.w * scale;
                    float y1 = y0 + g.srcRect.h * scale;
                    int base = static_cast<int>(batchVerts.size());
                    batchVerts.push_back({{x0, y0}, color, {g.u0, g.v0}});
                    batchVerts.push_back({{x1, y0}, color, {g.u1, g.v0}});
                    batchVerts.push_back({{x1, y1}, color, {g.u1, g.v1}});
                    batchVerts.push_back({{x0, y1}, color, {g.u0, g.v1}});
                    batchIndices.push_back(base);
                    batchIndices.push_back(base + 1);
                    batchIndices.push_back(base + 2);
                    batchIndices.push_back(base);
                    batchIndices.push_back(base + 2);
                    batchIndices.push_back(base + 3);
                }
                curX += g.xAdvance * scale;
            }
        } else {
            flushAtlasBatch();
            const CachedEmojiGlyph* eg = g_emojiSystem.getGlyph(renderer, cp);
            if (eg && eg->valid && eg->texture) {
                float boxH = fontSize * 0.96f * scale;
                float aspect = (eg->height > 0) ? (static_cast<float>(eg->width) / static_cast<float>(eg->height)) : 1.0f;
                float boxW = boxH * aspect;
                SDL_FRect dst;
                dst.x = curX + (emAdvance - boxW) * 0.5f;
                dst.y = y + (fontSize * scale - boxH) * 0.5f + 1.0f * scale;
                dst.w = boxW;
                dst.h = boxH;

                if (eg->isColorEmoji && !isShadowPass) {
                    SDL_SetTextureColorMod(eg->texture, 255, 255, 255);
                } else {
                    SDL_SetTextureColorMod(eg->texture, color.r, color.g, color.b);
                }
                SDL_SetTextureAlphaMod(eg->texture, color.a);
                SDL_RenderCopyF(renderer, eg->texture, nullptr, &dst);
            }
            curX += emAdvance;
        }
    }
    flushAtlasBatch();
}

void FontRenderer::drawText(SDL_Renderer* renderer, const std::string& text, float x, float y, SDL_Color color, TextAlign align, float scale) {
    if (!fontAtlas || text.empty()) return;
    if (text.size() <= 256) {
        uint32_t stackCps[256];
        size_t cnt = decodeNormalizedUTF8Into(text, stackCps, 256);
        if (cnt == 0) return;
        float totalW = (align == TextAlign::Left) ? 0.0f : measureDecodedSpan(stackCps, cnt, scale);
        drawDecodedSpan(renderer, stackCps, cnt, totalW, x, y, color, align, scale, false);
        return;
    }
    auto cps = decodeNormalizedUTF8(text);
    if (cps.empty()) return;
    float totalW = (align == TextAlign::Left) ? 0.0f : measureDecodedSpan(cps.data(), cps.size(), scale);
    drawDecodedSpan(renderer, cps.data(), cps.size(), totalW, x, y, color, align, scale, false);
}

void FontRenderer::drawTextShadow(SDL_Renderer* renderer, const std::string& text, float x, float y, SDL_Color color, TextAlign align, float scale, float shadowOffset) {
    if (!fontAtlas || text.empty()) return;
    uint8_t shadowAlpha = static_cast<uint8_t>((color.a * 190) / 255);
    SDL_Color shCol{0, 0, 0, shadowAlpha};

    if (text.size() <= 256) {
        uint32_t stackCps[256];
        size_t cnt = decodeNormalizedUTF8Into(text, stackCps, 256);
        if (cnt == 0) return;
        float totalW = (align == TextAlign::Left) ? 0.0f : measureDecodedSpan(stackCps, cnt, scale);
        drawDecodedSpan(renderer, stackCps, cnt, totalW, x + shadowOffset, y + shadowOffset, shCol, align, scale, true);
        drawDecodedSpan(renderer, stackCps, cnt, totalW, x, y, color, align, scale, false);
        return;
    }
    auto cps = decodeNormalizedUTF8(text);
    if (cps.empty()) return;
    float totalW = (align == TextAlign::Left) ? 0.0f : measureDecodedSpan(cps.data(), cps.size(), scale);
    drawDecodedSpan(renderer, cps.data(), cps.size(), totalW, x + shadowOffset, y + shadowOffset, shCol, align, scale, true);
    drawDecodedSpan(renderer, cps.data(), cps.size(), totalW, x, y, color, align, scale, false);
}

bool FontRenderer::decodeColorEmojiRGBA(uint32_t codepoint, int& outW, int& outH, std::vector<uint32_t>& outRGBA) {
    return g_emojiSystem.decodeColorEmojiPixelData(codepoint, outW, outH, outRGBA);
}

}
