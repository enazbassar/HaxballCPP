#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <optional>

struct BufferWriter {
    std::vector<uint8_t> buf;

    void writeU8(uint8_t v) { buf.push_back(v); }
    void writeI8(int8_t v) { buf.push_back((uint8_t)v); }

    void writeU16BE(uint16_t v) {
        buf.push_back((v >> 8) & 0xFF);
        buf.push_back(v & 0xFF);
    }
    void writeU16LE(uint16_t v) {
        buf.push_back(v & 0xFF);
        buf.push_back((v >> 8) & 0xFF);
    }
    void writeI16BE(int16_t v) { writeU16BE((uint16_t)v); }
    void writeI16LE(int16_t v) { writeU16LE((uint16_t)v); }

    void writeU32BE(uint32_t v) {
        buf.push_back((v >> 24) & 0xFF);
        buf.push_back((v >> 16) & 0xFF);
        buf.push_back((v >> 8) & 0xFF);
        buf.push_back(v & 0xFF);
    }
    void writeU32LE(uint32_t v) {
        buf.push_back(v & 0xFF);
        buf.push_back((v >> 8) & 0xFF);
        buf.push_back((v >> 16) & 0xFF);
        buf.push_back((v >> 24) & 0xFF);
    }
    void writeI32BE(int32_t v) { writeU32BE((uint32_t)v); }
    void writeI32LE(int32_t v) { writeU32LE((uint32_t)v); }

    void writeF32BE(float v) {
        uint32_t u;
        memcpy(&u, &v, 4);
        writeU32BE(u);
    }
    void writeF32LE(float v) {
        uint32_t u;
        memcpy(&u, &v, 4);
        writeU32LE(u);
    }
    void writeF64BE(double v) {
        uint64_t u;
        memcpy(&u, &v, 8);
        for (int i = 7; i >= 0; --i) {
            buf.push_back((u >> (i * 8)) & 0xFF);
        }
    }

    void writeVarInt(uint32_t v) {
        do {
            uint8_t b = v & 0x7F;
            v >>= 7;
            if (v != 0) b |= 0x80;
            buf.push_back(b);
        } while (v != 0);
    }

    void writeStr(const std::string& s) {
        writeVarInt((uint32_t)s.size());
        buf.insert(buf.end(), s.begin(), s.end());
    }

    void writeOptStr(const std::string* s) {
        if (!s || s->empty()) {
            writeVarInt(0);
        } else {
            writeVarInt((uint32_t)s->size() + 1);
            buf.insert(buf.end(), s->begin(), s->end());
        }
    }

    void writeBytes(const uint8_t* p, size_t len) {
        buf.insert(buf.end(), p, p + len);
    }
};

struct BufferReader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;

    BufferReader(const uint8_t* d, size_t s) : data(d), size(s), pos(0) {}

    bool hasRemaining() const { return pos < size; }
    size_t remaining() const { return pos < size ? size - pos : 0; }

    uint8_t readU8() {
        if (pos >= size) throw std::runtime_error("Buffer underflow U8");
        return data[pos++];
    }
    int8_t readI8() { return (int8_t)readU8(); }

    uint16_t readU16BE() {
        if (pos + 2 > size) throw std::runtime_error("Buffer underflow U16BE");
        uint16_t v = ((uint16_t)data[pos] << 8) | data[pos + 1];
        pos += 2;
        return v;
    }
    uint16_t readU16LE() {
        if (pos + 2 > size) throw std::runtime_error("Buffer underflow U16LE");
        uint16_t v = data[pos] | ((uint16_t)data[pos + 1] << 8);
        pos += 2;
        return v;
    }
    int16_t readI16BE() { return (int16_t)readU16BE(); }
    int16_t readI16LE() { return (int16_t)readU16LE(); }

    uint32_t readU32BE() {
        if (pos + 4 > size) throw std::runtime_error("Buffer underflow U32BE");
        uint32_t v = ((uint32_t)data[pos] << 24) |
                     ((uint32_t)data[pos + 1] << 16) |
                     ((uint32_t)data[pos + 2] << 8) |
                     data[pos + 3];
        pos += 4;
        return v;
    }
    uint32_t readU32LE() {
        if (pos + 4 > size) throw std::runtime_error("Buffer underflow U32LE");
        uint32_t v = data[pos] |
                     ((uint32_t)data[pos + 1] << 8) |
                     ((uint32_t)data[pos + 2] << 16) |
                     ((uint32_t)data[pos + 3] << 24);
        pos += 4;
        return v;
    }
    int32_t readI32BE() { return (int32_t)readU32BE(); }
    int32_t readI32LE() { return (int32_t)readU32LE(); }

    float readF32BE() {
        if (pos + 4 > size) throw std::runtime_error("Buffer underflow F32BE");
        uint32_t u = readU32BE();
        float v;
        memcpy(&v, &u, 4);
        return v;
    }

    float readF32LE() {
        if (pos + 4 > size) throw std::runtime_error("Buffer underflow F32LE");
        float v;
        memcpy(&v, data + pos, 4);
        pos += 4;
        return v;
    }

    double readF64BE() {
        if (pos + 8 > size) throw std::runtime_error("Buffer underflow F64BE");
        uint64_t u = 0;
        for (int i = 0; i < 8; ++i) {
            u = (u << 8) | data[pos + i];
        }
        pos += 8;
        double v;
        memcpy(&v, &u, 8);
        return v;
    }

    uint32_t readVarInt() {
        uint32_t res = 0;
        int shift = 0;
        while (pos < size) {
            uint8_t b = data[pos++];
            if (shift >= 35) throw std::runtime_error("Malformed VarInt");
            res |= (uint32_t)(b & 0x7F) << shift;
            if (!(b & 0x80)) break;
            shift += 7;
        }
        return res;
    }

    static std::string normalizeCesu8(const uint8_t* ptr, size_t len) {
        std::string out;
        out.reserve(len);
        size_t i = 0;
        while (i < len) {
            if (i + 5 < len &&
                ptr[i] == 0xED && (ptr[i + 1] & 0xF0) == 0xA0 && (ptr[i + 2] & 0xC0) == 0x80 &&
                ptr[i + 3] == 0xED && (ptr[i + 4] & 0xF0) == 0xB0 && (ptr[i + 5] & 0xC0) == 0x80) {
                uint32_t hi = ((ptr[i] & 0x0F) << 12) | ((ptr[i + 1] & 0x3F) << 6) | (ptr[i + 2] & 0x3F);
                uint32_t lo = ((ptr[i + 3] & 0x0F) << 12) | ((ptr[i + 4] & 0x3F) << 6) | (ptr[i + 5] & 0x3F);
                uint32_t cp = 0x10000 + (((hi - 0xD800) << 10) | (lo - 0xDC00));
                out.push_back((char)(0xF0 | ((cp >> 18) & 0x07)));
                out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back((char)(0x80 | (cp & 0x3F)));
                i += 6;
            } else {
                out.push_back((char)ptr[i]);
                i += 1;
            }
        }
        return out;
    }

    std::string readStr() {
        uint32_t len = readVarInt();
        if (pos + len > size) throw std::runtime_error("Buffer underflow Str");
        std::string s = normalizeCesu8(data + pos, len);
        pos += len;
        return s;
    }

    std::optional<std::string> readOptStr() {
        uint32_t len = readVarInt();
        if (len == 0) return std::nullopt;
        if (pos + len - 1 > size) throw std::runtime_error("Buffer underflow OptStr");
        std::string s = normalizeCesu8(data + pos, len - 1);
        pos += len - 1;
        return s;
    }

    std::vector<uint8_t> readBytes(size_t len) {
        if (pos + len > size) throw std::runtime_error("Buffer underflow Bytes");
        std::vector<uint8_t> b(data + pos, data + pos + len);
        pos += len;
        return b;
    }
};
