#pragma once

#include <vector>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <zlib.h>

inline std::vector<uint8_t> raw_deflate(const uint8_t* in_data, size_t in_size) {
    z_stream def;
    memset(&def, 0, sizeof(def));
    deflateInit2(&def, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
    std::vector<uint8_t> out(in_size + 1024);
    def.next_in = (Bytef*)in_data;
    def.avail_in = (uInt)in_size;
    def.next_out = out.data();
    def.avail_out = (uInt)out.size();
    deflate(&def, Z_FINISH);
    out.resize(out.size() - def.avail_out);
    deflateEnd(&def);
    return out;
}

inline std::vector<uint8_t> raw_inflate(const uint8_t* in_data, size_t in_size) {
    constexpr size_t kMaxInflateBytes = 16u * 1024u * 1024u;
    z_stream inf;
    memset(&inf, 0, sizeof(inf));
    if (inflateInit2(&inf, -15) != Z_OK) {
        throw std::runtime_error("inflateInit2 failed");
    }
    std::vector<uint8_t> out(std::min<size_t>(kMaxInflateBytes, std::max<size_t>(4096u, in_size * 4u)));
    inf.next_in = (Bytef*)in_data;
    inf.avail_in = (uInt)in_size;
    inf.next_out = out.data();
    inf.avail_out = (uInt)out.size();
    while (true) {
        int ret = inflate(&inf, Z_NO_FLUSH);
        if (ret == Z_STREAM_END) break;
        if (ret != Z_OK) {
            inflateEnd(&inf);
            throw std::runtime_error("inflateRaw failed with code " + std::to_string(ret));
        }
        if (inf.avail_out == 0) {
            size_t curr = out.size();
            if (curr >= kMaxInflateBytes) {
                inflateEnd(&inf);
                throw std::runtime_error("inflateRaw exceeded 16 MiB safety limit");
            }
            size_t nextSize = std::min<size_t>(curr * 2u, kMaxInflateBytes);
            out.resize(nextSize);
            inf.next_out = out.data() + curr;
            inf.avail_out = static_cast<uInt>(nextSize - curr);
        }
    }
    out.resize(out.size() - inf.avail_out);
    inflateEnd(&inf);
    return out;
}
