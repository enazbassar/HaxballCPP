#include "room_list.hpp"
#include "buffer.hpp"
#include "../config/app_config.hpp"

#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <iostream>
#include <cmath>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cctype>

namespace haxball {

static std::vector<uint8_t> httpsGet(const std::string& host, const std::string& path) {
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();

    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) return {};

    BIO* bio = BIO_new_ssl_connect(ctx);
    if (!bio) {
        SSL_CTX_free(ctx);
        return {};
    }

    SSL* ssl = nullptr;
    BIO_get_ssl(bio, &ssl);
    SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
    SSL_set_tlsext_host_name(ssl, host.c_str());
    BIO_set_conn_hostname(bio, (host + ":443").c_str());

    if (BIO_do_connect(bio) <= 0) {
        BIO_free_all(bio);
        SSL_CTX_free(ctx);
        return {};
    }

    std::string cookieHeader = "";
    auto& cfg = AppConfig::get();
    if (!cfg.cfClearance.empty()) {
        cookieHeader = "Cookie: cf_clearance=" + cfg.cfClearance + "\r\n";
    }

    std::string req = "GET " + path + " HTTP/1.0\r\n"
                      "Host: " + host + "\r\n"
                      "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36\r\n"
                      "Accept: */*\r\n"
                      "Referer: https://www.haxball.com/play\r\n"
                      + cookieHeader +
                      "Accept-Language: en-US,en;q=0.9\r\n"
                      "Connection: close\r\n\r\n";
    BIO_write(bio, req.data(), (int)req.size());

    std::vector<uint8_t> resp;
    char buf[8192];
    int len = 0;
    while ((len = BIO_read(bio, buf, sizeof(buf))) > 0) {
        resp.insert(resp.end(), buf, buf + len);
    }
    BIO_free_all(bio);
    SSL_CTX_free(ctx);

    size_t headerEnd = std::string::npos;
    for (size_t i = 0; i + 3 < resp.size(); ++i) {
        if (resp[i] == '\r' && resp[i+1] == '\n' && resp[i+2] == '\r' && resp[i+3] == '\n') {
            headerEnd = i;
            break;
        }
    }

    if (headerEnd == std::string::npos) {
        std::cerr << "[RoomList] Failed to find HTTP header delimiter in response" << std::endl;
        return {};
    }

    std::string headerStr(reinterpret_cast<const char*>(resp.data()), headerEnd);

    size_t firstLineEnd = headerStr.find("\r\n");
    std::string statusLine = (firstLineEnd != std::string::npos) ? headerStr.substr(0, firstLineEnd) : headerStr;
    if (statusLine.find("200") == std::string::npos) {
        std::cerr << "[RoomList] HTTP status error: " << statusLine << std::endl;
        return {};
    }

    std::string lowerHeaders = headerStr;
    for (char& c : lowerHeaders) c = static_cast<char>(std::tolower(c));
    if (lowerHeaders.find("content-type: text/html") != std::string::npos) {
        std::cerr << "[RoomList] Received HTML response instead of binary room data" << std::endl;
        return {};
    }

    return std::vector<uint8_t>(resp.begin() + headerEnd + 4, resp.end());
}

std::vector<RoomInfo> fetchRoomList() {
    std::vector<RoomInfo> rooms;
    std::vector<uint8_t> body;

    for (int attempt = 0; attempt < 3; ++attempt) {
        body = httpsGet("www.haxball.com", "/rs/api/list");
        if (!body.empty()) break;
        if (attempt < 2) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200 * (attempt + 1)));
        }
    }

    if (body.empty()) {
        std::cerr << "[RoomList] Empty body received from room list API" << std::endl;
        return rooms;
    }

    try {
        BufferReader r(body.data(), body.size());
        if (r.remaining() < 1) return rooms;
        r.readU8();

        while (r.remaining() >= 4) {
            uint16_t idLen = r.readU16BE();
            if (r.remaining() < static_cast<size_t>(idLen) + 2) {
                break;
            }
            auto idBytes = r.readBytes(idLen);
            std::string id(reinterpret_cast<const char*>(idBytes.data()), idBytes.size());

            uint16_t dataLen = r.readU16BE();
            if (r.remaining() < dataLen) {
                break;
            }
            auto dBytes = r.readBytes(dataLen);

            if (dBytes.size() < 15) {
                continue;
            }

            BufferReader dr(dBytes.data(), dBytes.size());
            if (dr.remaining() < 2) continue;
            dr.readU16LE();

            if (dr.remaining() < 1) continue;
            uint8_t nameLen = dr.readU8();
            if (dr.remaining() < static_cast<size_t>(nameLen) + 1) continue;
            auto nameB = dr.readBytes(nameLen);
            std::string name(reinterpret_cast<const char*>(nameB.data()), nameB.size());

            uint8_t flagLen = dr.readU8();

            if (dr.remaining() < static_cast<size_t>(flagLen) + 11) continue;
            auto flagB = dr.readBytes(flagLen);
            std::string flag(reinterpret_cast<const char*>(flagB.data()), flagB.size());

            float lat = dr.readF32LE();
            float lon = dr.readF32LE();
            bool pass = dr.readU8() != 0;
            uint8_t maxP = dr.readU8();
            uint8_t p = dr.readU8();

            RoomInfo info;
            info.id = std::move(id);
            info.name = std::move(name);
            info.flag = std::move(flag);
            info.lat = lat;
            info.lon = lon;
            info.hasPassword = pass;
            info.maxPlayers = maxP;
            info.players = p;

            rooms.push_back(std::move(info));
        }
    } catch (const std::exception& e) {
        std::cerr << "[RoomList] Parse error: " << e.what() << std::endl;
    }

    return rooms;
}

}
