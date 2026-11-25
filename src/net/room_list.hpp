#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace haxball {

struct RoomInfo {
    std::string id;
    std::string name;
    std::string flag;
    float lat = 0.0f;
    float lon = 0.0f;
    bool hasPassword = false;
    uint8_t players = 0;
    uint8_t maxPlayers = 0;
    float distance = 0.0f;
};

std::vector<RoomInfo> fetchRoomList();

}
