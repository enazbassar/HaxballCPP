#pragma once

#include <SDL2/SDL.h>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <nlohmann/json.hpp>
#include "../net/crypto_auth.hpp"

namespace haxball {

struct AuthProfile {
    std::string id;
    std::string label;
    std::string key;
};

class AppConfig {
public:
    std::string playerName = "ProPlayer";
    std::string playerAvatar = "⚽";
    std::string playerFlag = "nl";
    std::string directRoomId = "";
    std::string directPassword = "";
    std::string cfClearance = "";

    std::string activeAuthId = "auth_main";
    std::vector<AuthProfile> authProfiles;

    int extrapolationMs = 0;
    int handicapMs = 0;
    bool antiFakeShot = true;
    bool antiOpMode = false;
    bool autoFakeShot = true;
    bool fakeShotExtendEnabled = true;
    int fakeShotExtendTicks = 0;
    bool socdLastInputPriority = true;
    int socdDelayTicks = 0;
    int fakeShotIntervalMs = 500;
    int realShotIntervalMs = 60;
    int slowSpeedPercent = 50;
    bool autoShakeEnabled = true;
    int autoShakeFrequencyTicks = 4;
    bool ballOrbitEnabled = true;
    int ballOrbitDistance = 0;
    bool fakeTypingIndicator = false;
    bool typingSpammerEnabled = false;
    int typingSpamSpeedMs = 100;
    bool firstTouchCushionEnabled = true;
    bool fastFakeEnabled = true;
    int fastFakeSpeedTicks = 2;
    bool autoGoalEnabled = true;
    bool autoGoalAnyGoal = false;
    int autoGoalAccuracy = 95;
    bool autoSaveEnabled = true;
    int aimLineDistance = 40;
    bool showLiveAimTrajectory = true;
    bool showBallRollTrajectory = true;
    bool showGkDangerCone = true;
    bool showOtherGoalsCone = true;
    bool showDesyncGhost = false;

    bool menuLightMode = false;
    int menuAccentIdx = 0;
    float menuOpacity = 0.96f;
    bool showX88Hud = true;
    int x88Position = 0;
    bool x88ActiveOnly = false;
    bool x88ShowHotkeys = true;
    float x88Opacity = 0.92f;
    float x88Scale = 1.00f;

    int keySlide = SDLK_c;
    int keySprint = SDLK_v;
    int keySprintAlt = SDLK_e;
    int keyRealShot = SDLK_n;
    int keyAutoShake = SDLK_b;
    int keyBallOrbit = SDLK_o;
    int keyFakeTyping = SDLK_j;
    int keyTypingSpam = SDLK_u;
    int keyAvatarSpam = SDLK_i;
    int keySlowMove = SDLK_m;
    int keyFakeToggle = SDLK_f;
    int keyDribbleToggle = SDLK_k;

    bool vsync = false;
    int fpsCap = 240;
    int ballTrailStyle = 1;
    int pitchTheme = 0;

    float masterVolume = 0.8f;
    bool soundKick = true;
    bool soundGoal = true;
    bool soundChat = true;

    static AppConfig& get() {
        static AppConfig instance;
        return instance;
    }

    void ensureDefaultAuthKey() {
        if (authProfiles.empty()) {
            AuthSigner signer;
            AuthProfile p;
            p.id = "auth_main";
            p.label = "Main Profile";
            p.key = signer.getAuthKeyString();
            authProfiles.push_back(p);
            activeAuthId = p.id;
            save();
        } else {
            bool found = false;
            for (const auto& p : authProfiles) {
                if (p.id == activeAuthId) {
                    found = true;
                    break;
                }
            }
            if (!found && !authProfiles.empty()) {
                activeAuthId = authProfiles[0].id;
                save();
            }
        }
    }

    std::string getActiveAuthKey() const {
        for (const auto& p : authProfiles) {
            if (p.id == activeAuthId) return p.key;
        }
        if (!authProfiles.empty()) return authProfiles[0].key;
        return "";
    }

    std::string getActiveAuthLabel() const {
        for (const auto& p : authProfiles) {
            if (p.id == activeAuthId) return p.label;
        }
        if (!authProfiles.empty()) return authProfiles[0].label;
        return "Unknown";
    }

    bool addAuthKey(const std::string& label, const std::string& keyStr) {

        AuthSigner testSigner;
        if (!testSigner.loadFromKey(keyStr)) {
            return false;
        }

        AuthProfile p;
        p.id = "auth_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        p.label = label.empty() ? ("Profile #" + std::to_string(authProfiles.size() + 1)) : label;
        p.key = keyStr;
        authProfiles.push_back(p);
        save();
        return true;
    }

    bool generateNewAuthKey(const std::string& label) {
        AuthSigner signer;
        AuthProfile p;
        p.id = "auth_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        p.label = label.empty() ? ("Key #" + std::to_string(authProfiles.size() + 1)) : label;
        p.key = signer.getAuthKeyString();
        authProfiles.push_back(p);
        save();
        return true;
    }

    bool deleteAuthKey(const std::string& id) {
        if (authProfiles.size() <= 1) return false;
        auto it = std::remove_if(authProfiles.begin(), authProfiles.end(), [&](const AuthProfile& p) {
            return p.id == id;
        });
        if (it != authProfiles.end()) {
            authProfiles.erase(it, authProfiles.end());
            if (activeAuthId == id) {
                activeAuthId = authProfiles[0].id;
            }
            save();
            return true;
        }
        return false;
    }

    void setActiveAuth(const std::string& id) {
        for (const auto& p : authProfiles) {
            if (p.id == id) {
                activeAuthId = id;
                save();
                return;
            }
        }
    }

    bool load(const std::string& filePath = "haxball_config.json") {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            ensureDefaultAuthKey();
            save(filePath);
            return false;
        }

        try {
            nlohmann::json j;
            file >> j;

            if (j.contains("playerName") && j["playerName"].is_string())
                playerName = j["playerName"].get<std::string>();
            if (j.contains("playerAvatar") && j["playerAvatar"].is_string())
                playerAvatar = j["playerAvatar"].get<std::string>();
            if (j.contains("playerFlag") && j["playerFlag"].is_string())
                playerFlag = j["playerFlag"].get<std::string>();
            if (j.contains("directRoomId") && j["directRoomId"].is_string())
                directRoomId = j["directRoomId"].get<std::string>();
            if (j.contains("directPassword") && j["directPassword"].is_string())
                directPassword = j["directPassword"].get<std::string>();
            if (j.contains("cfClearance") && j["cfClearance"].is_string())
                cfClearance = j["cfClearance"].get<std::string>();
            if (j.contains("activeAuthId") && j["activeAuthId"].is_string())
                activeAuthId = j["activeAuthId"].get<std::string>();

            if (j.contains("authProfiles") && j["authProfiles"].is_array()) {
                authProfiles.clear();
                for (const auto& item : j["authProfiles"]) {
                    if (item.contains("id") && item.contains("label") && item.contains("key")) {
                        AuthProfile p;
                        p.id = item["id"].get<std::string>();
                        p.label = item["label"].get<std::string>();
                        p.key = item["key"].get<std::string>();
                        authProfiles.push_back(p);
                    }
                }
            }

            if (j.contains("settings") && j["settings"].is_object()) {
                const auto& s = j["settings"];
                if (s.contains("extrapolationMs")) extrapolationMs = s["extrapolationMs"].get<int>();
                if (s.contains("handicapMs")) handicapMs = s["handicapMs"].get<int>();
                if (s.contains("antiFakeShot")) antiFakeShot = s["antiFakeShot"].get<bool>();
                if (s.contains("antiOpMode")) antiOpMode = s["antiOpMode"].get<bool>();
                if (s.contains("autoFakeShot")) autoFakeShot = s["autoFakeShot"].get<bool>();
                if (s.contains("fakeShotExtendEnabled")) fakeShotExtendEnabled = s["fakeShotExtendEnabled"].get<bool>();
                if (s.contains("fakeShotExtendTicks")) fakeShotExtendTicks = std::clamp(s["fakeShotExtendTicks"].get<int>(), 0, 15);
                if (s.contains("socdLastInputPriority")) socdLastInputPriority = s["socdLastInputPriority"].get<bool>();
                if (s.contains("socdDelayTicks")) socdDelayTicks = s["socdDelayTicks"].get<int>();
                if (s.contains("fakeShotIntervalMs")) fakeShotIntervalMs = s["fakeShotIntervalMs"].get<int>();
                if (s.contains("realShotIntervalMs")) realShotIntervalMs = s["realShotIntervalMs"].get<int>();
                if (s.contains("slowSpeedPercent")) slowSpeedPercent = s["slowSpeedPercent"].get<int>();
                if (s.contains("autoShakeEnabled")) autoShakeEnabled = s["autoShakeEnabled"].get<bool>();
                if (s.contains("autoShakeFrequencyTicks")) autoShakeFrequencyTicks = s["autoShakeFrequencyTicks"].get<int>();
                if (s.contains("ballOrbitEnabled")) ballOrbitEnabled = s["ballOrbitEnabled"].get<bool>();
                if (s.contains("ballOrbitDistance")) ballOrbitDistance = s["ballOrbitDistance"].get<int>();
                if (s.contains("fakeTypingIndicator")) fakeTypingIndicator = s["fakeTypingIndicator"].get<bool>();
                if (s.contains("typingSpammerEnabled")) typingSpammerEnabled = s["typingSpammerEnabled"].get<bool>();
                if (s.contains("typingSpamSpeedMs")) typingSpamSpeedMs = s["typingSpamSpeedMs"].get<int>();
                if (s.contains("firstTouchCushionEnabled")) firstTouchCushionEnabled = s["firstTouchCushionEnabled"].get<bool>();
                if (s.contains("fastFakeEnabled")) fastFakeEnabled = s["fastFakeEnabled"].get<bool>();
                if (s.contains("fastFakeSpeedTicks")) fastFakeSpeedTicks = std::clamp(s["fastFakeSpeedTicks"].get<int>(), 1, 10);
                if (s.contains("autoGoalEnabled")) autoGoalEnabled = s["autoGoalEnabled"].get<bool>();
                if (s.contains("autoGoalAnyGoal")) autoGoalAnyGoal = s["autoGoalAnyGoal"].get<bool>();
                if (s.contains("autoGoalAccuracy")) autoGoalAccuracy = std::clamp(s["autoGoalAccuracy"].get<int>(), 40, 100);
                if (s.contains("autoSaveEnabled")) autoSaveEnabled = s["autoSaveEnabled"].get<bool>();
                if (s.contains("aimLineDistance")) aimLineDistance = std::clamp(s["aimLineDistance"].get<int>(), 20, 300);
                if (s.contains("showLiveAimTrajectory")) showLiveAimTrajectory = s["showLiveAimTrajectory"].get<bool>();
                if (s.contains("showBallRollTrajectory")) showBallRollTrajectory = s["showBallRollTrajectory"].get<bool>();
                if (s.contains("showGkDangerCone")) showGkDangerCone = s["showGkDangerCone"].get<bool>();
                if (s.contains("showOtherGoalsCone")) showOtherGoalsCone = s["showOtherGoalsCone"].get<bool>();
                if (s.contains("showDesyncGhost")) showDesyncGhost = s["showDesyncGhost"].get<bool>();
                if (s.contains("menuLightMode")) menuLightMode = s["menuLightMode"].get<bool>();
                if (s.contains("menuAccentIdx")) menuAccentIdx = std::clamp(s["menuAccentIdx"].get<int>(), 0, 4);
                if (s.contains("menuOpacity")) menuOpacity = std::clamp(s["menuOpacity"].get<float>(), 0.65f, 1.0f);
                if (s.contains("showX88Hud")) showX88Hud = s["showX88Hud"].get<bool>();
                if (s.contains("x88Position")) x88Position = std::clamp(s["x88Position"].get<int>(), 0, 3);
                if (s.contains("x88ActiveOnly")) x88ActiveOnly = s["x88ActiveOnly"].get<bool>();
                if (s.contains("x88ShowHotkeys")) x88ShowHotkeys = s["x88ShowHotkeys"].get<bool>();
                if (s.contains("x88Opacity")) x88Opacity = std::clamp(s["x88Opacity"].get<float>(), 0.30f, 1.0f);
                if (s.contains("x88Scale")) x88Scale = std::clamp(s["x88Scale"].get<float>(), 0.70f, 1.60f);
                if (s.contains("keySlide")) keySlide = s["keySlide"].get<int>();
                if (s.contains("keySprint")) keySprint = s["keySprint"].get<int>();
                if (s.contains("keySprintAlt")) keySprintAlt = s["keySprintAlt"].get<int>();
                if (s.contains("keyRealShot")) keyRealShot = s["keyRealShot"].get<int>();
                if (s.contains("keyAutoShake")) keyAutoShake = s["keyAutoShake"].get<int>();
                if (s.contains("keyBallOrbit")) keyBallOrbit = s["keyBallOrbit"].get<int>();
                if (s.contains("keyFakeTyping")) keyFakeTyping = s["keyFakeTyping"].get<int>();
                if (s.contains("keyTypingSpam")) keyTypingSpam = s["keyTypingSpam"].get<int>();
                if (s.contains("keySlowMove")) keySlowMove = s["keySlowMove"].get<int>();
                if (s.contains("keyFakeToggle")) keyFakeToggle = s["keyFakeToggle"].get<int>();
                if (s.contains("keyDribbleToggle")) keyDribbleToggle = s["keyDribbleToggle"].get<int>();
                if (s.contains("vsync")) vsync = s["vsync"].get<bool>();
                if (s.contains("fpsCap")) fpsCap = s["fpsCap"].get<int>();
                if (s.contains("ballTrailStyle")) ballTrailStyle = s["ballTrailStyle"].get<int>();
                if (s.contains("pitchTheme")) pitchTheme = s["pitchTheme"].get<int>();
                if (s.contains("masterVolume")) masterVolume = s["masterVolume"].get<float>();
                if (s.contains("soundKick")) soundKick = s["soundKick"].get<bool>();
                if (s.contains("soundGoal")) soundGoal = s["soundGoal"].get<bool>();
                if (s.contains("soundChat")) soundChat = s["soundChat"].get<bool>();
            }

            ensureDefaultAuthKey();
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[AppConfig] Error parsing config file: " << e.what() << std::endl;
            ensureDefaultAuthKey();
            save(filePath);
            return false;
        }
    }

    bool save(const std::string& filePath = "haxball_config.json") const {
        try {
            nlohmann::json j;
            j["playerName"] = playerName;
            j["playerAvatar"] = playerAvatar;
            j["playerFlag"] = playerFlag;
            j["directRoomId"] = directRoomId;
            j["directPassword"] = directPassword;
            j["cfClearance"] = cfClearance;
            j["activeAuthId"] = activeAuthId;

            nlohmann::json profilesArr = nlohmann::json::array();
            for (const auto& p : authProfiles) {
                profilesArr.push_back({
                    {"id", p.id},
                    {"label", p.label},
                    {"key", p.key}
                });
            }
            j["authProfiles"] = profilesArr;

            nlohmann::json s;
            s["extrapolationMs"] = extrapolationMs;
            s["handicapMs"] = handicapMs;
            s["antiFakeShot"] = antiFakeShot;
            s["antiOpMode"] = antiOpMode;
            s["autoFakeShot"] = autoFakeShot;
            s["fakeShotExtendEnabled"] = fakeShotExtendEnabled;
            s["fakeShotExtendTicks"] = fakeShotExtendTicks;
            s["socdLastInputPriority"] = socdLastInputPriority;
            s["socdDelayTicks"] = socdDelayTicks;
            s["fakeShotIntervalMs"] = fakeShotIntervalMs;
            s["realShotIntervalMs"] = realShotIntervalMs;
            s["slowSpeedPercent"] = slowSpeedPercent;
            s["autoShakeEnabled"] = autoShakeEnabled;
            s["autoShakeFrequencyTicks"] = autoShakeFrequencyTicks;
            s["ballOrbitEnabled"] = ballOrbitEnabled;
            s["ballOrbitDistance"] = ballOrbitDistance;
            s["fakeTypingIndicator"] = fakeTypingIndicator;
            s["typingSpammerEnabled"] = typingSpammerEnabled;
            s["typingSpamSpeedMs"] = typingSpamSpeedMs;
            s["firstTouchCushionEnabled"] = firstTouchCushionEnabled;
            s["fastFakeEnabled"] = fastFakeEnabled;
            s["fastFakeSpeedTicks"] = fastFakeSpeedTicks;
            s["autoGoalEnabled"] = autoGoalEnabled;
            s["autoGoalAnyGoal"] = autoGoalAnyGoal;
            s["autoGoalAccuracy"] = autoGoalAccuracy;
            s["autoSaveEnabled"] = autoSaveEnabled;
            s["aimLineDistance"] = aimLineDistance;
            s["showLiveAimTrajectory"] = showLiveAimTrajectory;
            s["showBallRollTrajectory"] = showBallRollTrajectory;
            s["showGkDangerCone"] = showGkDangerCone;
            s["showOtherGoalsCone"] = showOtherGoalsCone;
            s["showDesyncGhost"] = showDesyncGhost;
            s["menuLightMode"] = menuLightMode;
            s["menuAccentIdx"] = menuAccentIdx;
            s["menuOpacity"] = menuOpacity;
            s["showX88Hud"] = showX88Hud;
            s["x88Position"] = x88Position;
            s["x88ActiveOnly"] = x88ActiveOnly;
            s["x88ShowHotkeys"] = x88ShowHotkeys;
            s["x88Opacity"] = x88Opacity;
            s["x88Scale"] = x88Scale;
            s["keySlide"] = keySlide;
            s["keySprint"] = keySprint;
            s["keySprintAlt"] = keySprintAlt;
            s["keyRealShot"] = keyRealShot;
            s["keyAutoShake"] = keyAutoShake;
            s["keyBallOrbit"] = keyBallOrbit;
            s["keyFakeTyping"] = keyFakeTyping;
            s["keyTypingSpam"] = keyTypingSpam;
            s["keySlowMove"] = keySlowMove;
            s["keyFakeToggle"] = keyFakeToggle;
            s["keyDribbleToggle"] = keyDribbleToggle;
            s["vsync"] = vsync;
            s["fpsCap"] = fpsCap;
            s["ballTrailStyle"] = ballTrailStyle;
            s["pitchTheme"] = pitchTheme;
            s["masterVolume"] = masterVolume;
            s["soundKick"] = soundKick;
            s["soundGoal"] = soundGoal;
            s["soundChat"] = soundChat;
            j["settings"] = s;

            std::ofstream file(filePath);
            if (!file.is_open()) return false;
            file << j.dump(4);
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[AppConfig] Error saving config file: " << e.what() << std::endl;
            return false;
        }
    }
};

}
