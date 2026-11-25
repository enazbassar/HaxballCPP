#include "room_browser.hpp"
#include "../render/renderer.hpp"
#include <algorithm>
#include <thread>
#include <cctype>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace haxball {

static const CountryEntry kDefaultCountries[] = {
    {"nl", "Netherlands"},
    {"fr", "France"},
    {"tr", "Turkey"},
    {"de", "Germany"},
    {"es", "Spain"},
    {"gb", "United Kingdom"},
    {"pl", "Poland"},
    {"pt", "Portugal"},
    {"it", "Italy"},
    {"br", "Brazil"},
    {"ar", "Argentina"},
    {"us", "United States"},
    {"be", "Belgium"},
    {"se", "Sweden"},
    {"no", "Norway"},
    {"dk", "Denmark"},
    {"fi", "Finland"},
    {"ch", "Switzerland"},
    {"at", "Austria"},
    {"gr", "Greece"},
    {"ro", "Romania"},
    {"cz", "Czech Republic"},
    {"hu", "Hungary"},
    {"rs", "Serbia"},
    {"hr", "Croatia"},
    {"ba", "Bosnia & Herzegovina"},
    {"bg", "Bulgaria"},
    {"sk", "Slovakia"},
    {"ie", "Ireland"},
    {"ma", "Morocco"},
    {"dz", "Algeria"},
    {"tn", "Tunisia"},
    {"eg", "Egypt"},
    {"ru", "Russia"},
    {"ua", "Ukraine"},
    {"ca", "Canada"},
    {"mx", "Mexico"},
    {"co", "Colombia"},
    {"cl", "Chile"},
    {"uy", "Uruguay"},
    {"pe", "Peru"},
    {"au", "Australia"},
    {"jp", "Japan"},
    {"kr", "South Korea"},
    {"cn", "China"},
    {"sa", "Saudi Arabia"},
    {"ae", "United Arab Emirates"},
    {"il", "Israel"},
    {"in", "India"},
    {"id", "Indonesia"},
    {"my", "Malaysia"},
    {"th", "Thailand"},
    {"vn", "Vietnam"},
    {"za", "South Africa"},
    {"ng", "Nigeria"},
    {"al", "Albania"},
    {"mk", "North Macedonia"},
    {"ge", "Georgia"},
    {"az", "Azerbaijan"},
    {"am", "Armenia"},
    {"kz", "Kazakhstan"},
    {"lt", "Lithuania"},
    {"lv", "Latvia"},
    {"ee", "Estonia"},
    {"is", "Iceland"},
    {"lu", "Luxembourg"},
    {"cy", "Cyprus"},
    {"mt", "Malta"},
    {"me", "Montenegro"},
    {"si", "Slovenia"}
};

static const char* kBrowserQuickEmojis[] = {
    "⚽", "🔥", "👑", "🐐", "💎", "⚡", "💀", "🥶", "😎", "🎯", "🏆", "🤡"
};

static std::string toLowerAscii(const std::string& s) {
    auto cps = FontRenderer::decodeNormalizedUTF8(s);
    std::string out;
    out.reserve(cps.size());
    for (uint32_t cp : cps) {
        if (cp < 128) out.push_back((char)std::tolower((unsigned char)cp));
        else out.push_back((char)cp);
    }
    return out;
}

static void popBackUTF8(std::string& s) {
    while (!s.empty()) {
        uint8_t b = static_cast<uint8_t>(s.back());
        s.pop_back();
        if ((b & 0xC0) != 0x80) break;
    }
}

static std::string unstyleNick(const std::string& text) {
    std::string res;
    size_t i = 0;
    size_t n = text.size();
    while (i < n) {
        uint8_t b0 = (uint8_t)text[i];
        uint32_t cp = 0;
        size_t step = 1;
        if ((b0 & 0x80) == 0) { cp = b0; step = 1; }
        else if ((b0 & 0xE0) == 0xC0 && i + 1 < n) {
            cp = ((b0 & 0x1F) << 6) | ((uint8_t)text[i+1] & 0x3F); step = 2;
        } else if ((b0 & 0xF0) == 0xE0 && i + 2 < n) {
            cp = ((b0 & 0x0F) << 12) | (((uint8_t)text[i+1] & 0x3F) << 6) | ((uint8_t)text[i+2] & 0x3F); step = 3;
        } else if ((b0 & 0xF8) == 0xF0 && i + 3 < n) {
            cp = ((b0 & 0x07) << 18) | (((uint8_t)text[i+1] & 0x3F) << 12) | (((uint8_t)text[i+2] & 0x3F) << 6) | ((uint8_t)text[i+3] & 0x3F); step = 4;
        }
        i += step;

        if (cp >= 0x0300 && cp <= 0x036F) continue;

        if (cp >= 0x1F150 && cp <= 0x1F169) res += static_cast<char>('A' + (cp - 0x1F150));
        else if (cp >= 0x1F170 && cp <= 0x1F189) res += static_cast<char>('A' + (cp - 0x1F170));
        else if (cp >= 0x24B6 && cp <= 0x24CF) res += static_cast<char>('A' + (cp - 0x24B6));
        else if (cp >= 0x24D0 && cp <= 0x24E9) res += static_cast<char>('a' + (cp - 0x24D0));
        else if (cp >= 0x1D670 && cp <= 0x1D689) res += static_cast<char>('A' + (cp - 0x1D670));
        else if (cp >= 0x1D68A && cp <= 0x1D6A3) res += static_cast<char>('a' + (cp - 0x1D68A));
        else if (cp >= 0x1D7F6 && cp <= 0x1D7FF) res += static_cast<char>('0' + (cp - 0x1D7F6));
        else if (cp >= 0x1D400 && cp <= 0x1D419) res += static_cast<char>('A' + (cp - 0x1D400));
        else if (cp >= 0x1D41A && cp <= 0x1D433) res += static_cast<char>('a' + (cp - 0x1D41A));
        else if (cp >= 0x1D7CE && cp <= 0x1D7D7) res += static_cast<char>('0' + (cp - 0x1D7CE));
        else if (cp >= 0x1D434 && cp <= 0x1D44D) res += static_cast<char>('A' + (cp - 0x1D434));
        else if (cp >= 0x1D44E && cp <= 0x1D467) res += static_cast<char>('a' + (cp - 0x1D44E));
        else if (cp >= 0x1D538 && cp <= 0x1D551) res += static_cast<char>('A' + (cp - 0x1D538));
        else if (cp >= 0x1D552 && cp <= 0x1D56B) res += static_cast<char>('a' + (cp - 0x1D552));
        else if (cp >= 0x1D7D8 && cp <= 0x1D7E1) res += static_cast<char>('0' + (cp - 0x1D7D8));
        else {
            switch (cp) {
                case 0x1D00: res += 'a'; break; case 0x0299: res += 'b'; break;
                case 0x1D04: res += 'c'; break; case 0x1D05: res += 'd'; break;
                case 0x1D07: res += 'e'; break; case 0x0493: res += 'f'; break;
                case 0x0262: res += 'g'; break; case 0x029C: res += 'h'; break;
                case 0x026A: res += 'i'; break; case 0x1D0A: res += 'j'; break;
                case 0x1D0B: res += 'k'; break; case 0x029F: res += 'l'; break;
                case 0x1D0D: res += 'm'; break; case 0x0274: res += 'n'; break;
                case 0x1D0F: res += 'o'; break; case 0x1D18: res += 'p'; break;
                case 0x0280: res += 'r'; break; case 0xA731: res += 's'; break;
                case 0x1D1B: res += 't'; break; case 0x1D1C: res += 'u'; break;
                case 0x1D20: res += 'v'; break; case 0x1D21: res += 'w'; break;
                case 0x028F: res += 'y'; break; case 0x1D22: res += 'z'; break;
                default:
                    if (cp < 128) res += static_cast<char>(cp);
                    break;
            }
        }
    }
    return res;
}

static void appendCodepointUTF8(std::string& s, uint32_t cp) {
    if (cp <= 0x7F) {
        s.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        s.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        s.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        s.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

static std::string applyNickStyle(const std::string& rawText, int style) {
    if (style == 0) return rawText;
    std::string plain = unstyleNick(rawText);
    std::string res;
    for (char c : plain) {
        if (style == 1) {
            if (c >= 'A' && c <= 'Z') appendCodepointUTF8(res, 0x1F150 + (c - 'A'));
            else if (c >= 'a' && c <= 'z') appendCodepointUTF8(res, 0x1F170 + (c - 'a'));
            else if (c >= '0' && c <= '9') appendCodepointUTF8(res, 0x2460 + (c - '1' >= 0 ? c - '1' : 9));
            else res.push_back(c);
        } else if (style == 2) {
            if (c >= 'A' && c <= 'Z') appendCodepointUTF8(res, 0x1D670 + (c - 'A'));
            else if (c >= 'a' && c <= 'z') appendCodepointUTF8(res, 0x1D68A + (c - 'a'));
            else if (c >= '0' && c <= '9') appendCodepointUTF8(res, 0x1D7F6 + (c - '0'));
            else res.push_back(c);
        } else if (style == 3) {
            if (c >= 'A' && c <= 'Z') appendCodepointUTF8(res, 0x1D400 + (c - 'A'));
            else if (c >= 'a' && c <= 'z') appendCodepointUTF8(res, 0x1D41A + (c - 'a'));
            else if (c >= '0' && c <= '9') appendCodepointUTF8(res, 0x1D7CE + (c - '0'));
            else res.push_back(c);
        } else if (style == 4) {
            if (c >= 'A' && c <= 'Z') appendCodepointUTF8(res, 0x1D434 + (c - 'A'));
            else if (c >= 'a' && c <= 'z') appendCodepointUTF8(res, 0x1D44E + (c - 'a'));
            else res.push_back(c);
        } else if (style == 5) {
            if (c >= 'A' && c <= 'Z') appendCodepointUTF8(res, 0x1D538 + (c - 'A'));
            else if (c >= 'a' && c <= 'z') appendCodepointUTF8(res, 0x1D552 + (c - 'a'));
            else if (c >= '0' && c <= '9') appendCodepointUTF8(res, 0x1D7D8 + (c - '0'));
            else res.push_back(c);
        } else if (style == 6) {
            static const uint32_t scMap[26] = {
                0x1D00, 0x0299, 0x1D04, 0x1D05, 0x1D07, 0x0493, 0x0262, 0x029C,
                0x026A, 0x1D0A, 0x1D0B, 0x029F, 0x1D0D, 0x0274, 0x1D0F, 0x1D18,
                0x0071, 0x0280, 0xA731, 0x1D1B, 0x1D1C, 0x1D20, 0x1D21, 0x0078,
                0x028F, 0x1D22
            };
            if (c >= 'a' && c <= 'z') appendCodepointUTF8(res, scMap[c - 'a']);
            else if (c >= 'A' && c <= 'Z') appendCodepointUTF8(res, scMap[c - 'A']);
            else res.push_back(c);
        } else if (style == 7) {
            res.push_back(c);
            appendCodepointUTF8(res, 0x035B);
            appendCodepointUTF8(res, 0x0330);
        } else if (style == 8) {
            if (c >= 'A' && c <= 'Z') appendCodepointUTF8(res, 0x1D56C + (c - 'A'));
            else if (c >= 'a' && c <= 'z') appendCodepointUTF8(res, 0x1D586 + (c - 'a'));
            else if (c >= '0' && c <= '9') appendCodepointUTF8(res, 0x1D7CE + (c - '0'));
            else res.push_back(c);
        } else if (style == 9) {
            if (c >= 'A' && c <= 'Z') appendCodepointUTF8(res, 0x1F170 + (c - 'A'));
            else if (c >= 'a' && c <= 'z') appendCodepointUTF8(res, 0x1F170 + (c - 'a'));
            else res.push_back(c);
        } else {
            res.push_back(c);
        }
    }
    return res;
}

RoomBrowser::RoomBrowser() {
    for (const auto& c : kDefaultCountries) {
        allCountries.push_back(c);
    }
    filteredCountries = allCountries;
}

RoomBrowser::~RoomBrowser() {}

bool RoomBrowser::init(SDL_Renderer* renderer, const std::string& fontPath) {
    if (!fontSmall.init(renderer, fontPath, 13.0f)) return false;
    if (!font.init(renderer, fontPath, 15.0f)) return false;
    if (!fontLarge.init(renderer, fontPath, 19.0f)) return false;
    if (!fontTitle.init(renderer, fontPath, 25.0f)) return false;

    AppConfig::get().load();
    syncConfigToUI();

    rebuildFilteredCountries();
    refresh();
    return true;
}

void RoomBrowser::syncConfigToUI() {
    auto& cfg = AppConfig::get();
    playerNameInput = cfg.playerName;
    playerAvatarInput = cfg.playerAvatar;
    playerFlagInput = cfg.playerFlag;
    directIdInput = cfg.directRoomId;
    passwordInput = cfg.directPassword;
}

void RoomBrowser::syncUIToConfig() {
    auto& cfg = AppConfig::get();
    cfg.playerName = playerNameInput;
    cfg.playerAvatar = playerAvatarInput;
    cfg.playerFlag = playerFlagInput;
    cfg.directRoomId = directIdInput;
    cfg.directPassword = passwordInput;
    cfg.save();
}

void RoomBrowser::cycleNameStyle() {
    nameStyleIndex = (nameStyleIndex + 1) % 10;
    static const char* styleNames[] = {
        "Plain Text",
        "Circled Black (🅑🅘🅙🅘)",
        "Monospace (𝙱𝚒𝚓𝚒)",
        "Bold (𝐁𝐢𝐣𝐢)",
        "Italic (𝐵𝑖𝑗𝑖)",
        "Double-Struck (𝔹𝕚𝕛𝕚)",
        "Small Caps (ʙɪᴊɪ)",
        "⚡ Electric Spark Aura (B̰͛ḭ͛j̰͛ḭ͛)",
        "𝕭𝖔𝖑𝖉 𝕲𝖔𝖙𝖍𝖎𝖈 (𝕭𝖎𝖏𝖎)",
        "Squared Blocks (🅱🅸🅹🅸)"
    };
    playerNameInput = applyNickStyle(playerNameInput, nameStyleIndex);
    syncUIToConfig();
    showToast(std::string("Font Style: ") + styleNames[nameStyleIndex]);
}

void RoomBrowser::showToast(const std::string& msg, bool isError, float duration) {
    toastMessage = msg;
    toastIsError = isError;
    toastTimer = duration;
}

void RoomBrowser::update(float dt) {
    if (toastTimer > 0.0f) {
        toastTimer -= dt;
        if (toastTimer <= 0.0f) {
            toastMessage.clear();
        }
    }
}

void RoomBrowser::rebuildFilteredCountries() {
    filteredCountries.clear();
    std::string q = toLowerAscii(flagSearchInput);
    for (const auto& c : allCountries) {
        if (!q.empty()) {
            std::string n = toLowerAscii(c.name);
            std::string code = toLowerAscii(c.code);
            if (n.find(q) == std::string::npos && code.find(q) == std::string::npos) {
                continue;
            }
        }
        filteredCountries.push_back(c);
    }
    flagScrollOffset = 0;
}

void RoomBrowser::rebuildFilteredList() {
    filteredRooms.clear();
    std::string q = toLowerAscii(searchQueryInput);
    bool isLdcSearch = (q.find("ldc") != std::string::npos);

    struct ScoredRoom {
        RoomInfo info;
        int score = 0;
    };
    std::vector<ScoredRoom> scored;

    for (const auto& r : allRooms) {
        std::string normName = toLowerAscii(r.name);
        std::string normFlag = toLowerAscii(r.flag);
        std::string normId   = toLowerAscii(r.id);

        if (!q.empty()) {
            if (normName.find(q) == std::string::npos &&
                normFlag.find(q) == std::string::npos &&
                normId.find(q) == std::string::npos) {
                continue;
            }
        }

        bool hasLdc = (normName.find("ldc") != std::string::npos);
        if (quickFilter == QuickFilter::LDC && !hasLdc) continue;
        if (quickFilter == QuickFilter::OpenOnly && r.hasPassword) continue;
        if (quickFilter == QuickFilter::HasSlots && r.players >= r.maxPlayers) continue;

        int score = 0;

        if (isLdcSearch && hasLdc) {
            score += 1000000;
            if (normName.rfind("ldc", 0) == 0 || normName.rfind("[ldc]", 0) == 0) {
                score += 200000;
            }
        } else if (!q.empty()) {
            if (normName.rfind(q, 0) == 0) score += 20000;
            else if (normName.find(q) != std::string::npos) score += 10000;
        }

        if (hasLdc && quickFilter != QuickFilter::LDC && !isLdcSearch) {
            score += 500;
        }

        if (!r.hasPassword) score += 3000;
        score += r.players * 100;
        if (r.players < r.maxPlayers) score += 1000;

        scored.push_back({r, score});
    }

    std::sort(scored.begin(), scored.end(), [](const ScoredRoom& a, const ScoredRoom& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.info.players > b.info.players;
    });

    for (auto& s : scored) {
        filteredRooms.push_back(std::move(s.info));
    }

    if (selectedIndex >= (int)filteredRooms.size()) {
        selectedIndex = filteredRooms.empty() ? -1 : 0;
    }
    scrollOffset = std::clamp(scrollOffset, 0, std::max(0, (int)filteredRooms.size() - 5));
}

void RoomBrowser::refresh() {
    if (isLoading.load()) return;
    isLoading = true;
    std::thread([this]() {
        auto fetched = fetchRoomList();
        std::sort(fetched.begin(), fetched.end(), [](const RoomInfo& a, const RoomInfo& b) {
            if (a.hasPassword != b.hasPassword) return !a.hasPassword;
            return a.players > b.players;
        });
        {
            std::lock_guard<std::mutex> lock(roomsMutex);
            allRooms = std::move(fetched);
            rebuildFilteredList();
        }
        isLoading = false;
    }).detach();
}

std::string RoomBrowser::getSelectedRoomId() const {
    std::lock_guard<std::mutex> lock(roomsMutex);
    if (selectedIndex >= 0 && selectedIndex < (int)filteredRooms.size()) {
        return filteredRooms[selectedIndex].id;
    }
    return "";
}

BrowserAction RoomBrowser::handleEvent(const SDL_Event& event, int screenW, int screenH) {
    if (event.type == SDL_MOUSEBUTTONUP) {
        if (draggingSlider >= 0) {
            draggingSlider = -1;
            AppConfig::get().save();
            return BrowserAction::SettingsChanged;
        }
    }

    if (event.type == SDL_MOUSEMOTION) {
        int mx = event.motion.x;
        if (draggingSlider >= 0 && currentTab == LauncherTab::Settings) {
            auto& cfg = AppConfig::get();
            float sx = 40.0f;
            float sw = 360.0f;
            float ratio = std::clamp((mx - sx) / sw, 0.0f, 1.0f);
            if (draggingSlider == 0) {
                cfg.extrapolationMs = static_cast<int>(ratio * 150.0f);
            } else if (draggingSlider == 1) {
                cfg.fakeShotIntervalMs = 50 + static_cast<int>(ratio * 950.0f);
            } else if (draggingSlider == 2) {
                int raw = static_cast<int>(ratio * 1000.0f);
                cfg.realShotIntervalMs = std::clamp(raw, 1, 1000);
            } else if (draggingSlider == 3) {
                cfg.slowSpeedPercent = 10 + static_cast<int>(ratio * 80.0f);
            }
            return BrowserAction::SettingsChanged;
        }
    }

    if (event.type == SDL_MOUSEBUTTONDOWN) {
        int mx = event.button.x;
        int my = event.button.y;

        if (showFlagPicker) {
            float mw = 520.0f;
            float mh = 480.0f;
            float mx0 = (screenW - mw) * 0.5f;
            float my0 = (screenH - mh) * 0.5f;

            if (mx >= mx0 + mw - 38 && mx <= mx0 + mw - 10 && my >= my0 + 10 && my <= my0 + 38) {
                showFlagPicker = false;
                return BrowserAction::None;
            }

            if (my >= my0 + 50 && my <= my0 + 82 && mx >= mx0 + 20 && mx <= mx0 + mw - 20) {
                activeField = Field_FlagSearch;
                return BrowserAction::None;
            }

            float listY = my0 + 96.0f;
            float itemH = 34.0f;
            int maxItems = static_cast<int>((mh - 150.0f) / itemH);

            if (my >= listY && my <= listY + maxItems * itemH && mx >= mx0 + 20 && mx <= mx0 + mw - 20) {
                int clickedIdx = static_cast<int>((my - listY) / itemH) + flagScrollOffset;
                if (clickedIdx >= 0 && clickedIdx < (int)filteredCountries.size()) {
                    playerFlagInput = filteredCountries[clickedIdx].code;
                    syncUIToConfig();
                    showFlagPicker = false;
                    showToast("Flag set to " + filteredCountries[clickedIdx].name + " [" + playerFlagInput + "]");
                    return BrowserAction::None;
                }
            }

            if (mx < mx0 || mx > mx0 + mw || my < my0 || my > my0 + mh) {
                showFlagPicker = false;
                return BrowserAction::None;
            }
            return BrowserAction::None;
        }

        if (my >= 0 && my <= 54) {
            rebindingKey = -1;

            if (mx >= 200 && mx <= 285) { currentTab = LauncherTab::Rooms; activeField = Field_None; return BrowserAction::None; }
            if (mx >= 295 && mx <= 370) { currentTab = LauncherTab::Auth; activeField = Field_None; return BrowserAction::None; }
            if (mx >= 380 && mx <= 475) { currentTab = LauncherTab::Settings; activeField = Field_None; return BrowserAction::None; }
            if (mx >= 485 && mx <= 590) { currentTab = LauncherTab::Shortcuts; activeField = Field_None; return BrowserAction::None; }

            if (mx >= screenW - 114 && mx <= screenW - 14) {
                syncUIToConfig();
                return BrowserAction::StartSandbox;
            }

            if (mx >= screenW - 234 && mx <= screenW - 122) {
                syncUIToConfig();
                return BrowserAction::StartPenaltyPractice;
            }

            if (mx >= screenW - 324 && mx <= screenW - 242) {
                showFlagPicker = true;
                flagSearchInput.clear();
                rebuildFilteredCountries();
                activeField = Field_FlagSearch;
                return BrowserAction::None;
            }

            if (mx >= screenW - 370 && mx <= screenW - 332) {
                int nextIdx = 0;
                for (int i = 0; i < 12; ++i) {
                    if (playerAvatarInput == kBrowserQuickEmojis[i]) {
                        nextIdx = (i + 1) % 12;
                        break;
                    }
                }
                playerAvatarInput = kBrowserQuickEmojis[nextIdx];
                syncUIToConfig();
                return BrowserAction::None;
            }

            if (mx >= screenW - 508 && mx <= screenW - 378) {
                activeField = Field_Name;
                return BrowserAction::None;
            }

            if (mx >= screenW - 588 && mx <= screenW - 554) {
                cycleNameStyle();
                return BrowserAction::None;
            }
        }

        if (currentTab == LauncherTab::Rooms) {

            if (my >= 66 && my <= 100) {

                if (mx >= 24 && mx <= 300) {

                    if (!searchQueryInput.empty() && mx >= 275) {
                        searchQueryInput.clear();
                        std::lock_guard<std::mutex> lock(roomsMutex);
                        rebuildFilteredList();
                    } else {
                        activeField = Field_Search;
                    }
                    return BrowserAction::None;
                }

                if (mx >= 310 && mx <= 365) {
                    quickFilter = QuickFilter::All;
                    std::lock_guard<std::mutex> lock(roomsMutex);
                    rebuildFilteredList();
                    return BrowserAction::None;
                }

                if (mx >= 375 && mx <= 465) {
                    quickFilter = (quickFilter == QuickFilter::LDC) ? QuickFilter::All : QuickFilter::LDC;
                    std::lock_guard<std::mutex> lock(roomsMutex);
                    rebuildFilteredList();
                    return BrowserAction::None;
                }

                if (mx >= 475 && mx <= 565) {
                    quickFilter = (quickFilter == QuickFilter::OpenOnly) ? QuickFilter::All : QuickFilter::OpenOnly;
                    std::lock_guard<std::mutex> lock(roomsMutex);
                    rebuildFilteredList();
                    return BrowserAction::None;
                }

                if (mx >= 575 && mx <= 665) {
                    quickFilter = (quickFilter == QuickFilter::HasSlots) ? QuickFilter::All : QuickFilter::HasSlots;
                    std::lock_guard<std::mutex> lock(roomsMutex);
                    rebuildFilteredList();
                    return BrowserAction::None;
                }

                if (mx >= screenW - 120 && mx <= screenW - 24) {
                    refresh();
                    return BrowserAction::RefreshRooms;
                }
            }

            int botY = screenH - 58;
            if (my >= botY - 4 && my <= botY + 44) {

                if (mx >= 85 && mx <= 295) {
                    activeField = Field_DirectId;
                    return BrowserAction::None;
                }

                if (mx >= 350 && mx <= 480) {
                    activeField = Field_Password;
                    return BrowserAction::None;
                }

                if (mx >= 495 && mx <= 615) {
                    if (!directIdInput.empty()) {
                        syncUIToConfig();
                        return BrowserAction::JoinDirectId;
                    }
                }

                if (mx >= screenW - 195 && mx <= screenW - 30) {
                    if (selectedIndex >= 0) {
                        syncUIToConfig();
                        return BrowserAction::JoinSelectedRoom;
                    }
                }
            }

            float tableTop = 106.0f;
            float rowHeight = 36.0f;
            float tableBottom = screenH - 70.0f;

            if (my >= tableTop + 38.0f && my <= tableBottom && mx >= 16.0f && mx <= screenW - 16.0f) {
                activeField = Field_None;
                std::lock_guard<std::mutex> lock(roomsMutex);
                int rowIdx = static_cast<int>((my - (tableTop + 38.0f)) / rowHeight) + scrollOffset;
                if (rowIdx >= 0 && rowIdx < (int)filteredRooms.size()) {
                    if (selectedIndex == rowIdx && event.button.clicks == 2) {
                        syncUIToConfig();
                        return BrowserAction::JoinSelectedRoom;
                    }
                    selectedIndex = rowIdx;
                    directIdInput = filteredRooms[rowIdx].id;
                }
            } else {
                activeField = Field_None;
            }
        }

        else if (currentTab == LauncherTab::Auth) {
            auto& cfg = AppConfig::get();
            float cardY = 72.0f;
            float addY = 205.0f;

            if (my >= cardY + 16.0f && my <= cardY + 48.0f) {

                if (mx >= screenW - 410.0f && mx <= screenW - 275.0f) {
                    std::string key = cfg.getActiveAuthKey();
                    std::string pubId = "";
                    size_t p1 = key.find('.');
                    if (p1 != std::string::npos) {
                        size_t p2 = key.find('.', p1 + 1);
                        if (p2 != std::string::npos) pubId = key.substr(p1 + 1, p2 - p1 - 1);
                    }
                    if (!pubId.empty()) {
                        SDL_SetClipboardText(pubId.c_str());
                        showToast("Copied Public ID (shareable) to clipboard!");
                    }
                    return BrowserAction::None;
                }

                if (mx >= screenW - 265.0f && mx <= screenW - 145.0f) {
                    SDL_SetClipboardText(cfg.getActiveAuthKey().c_str());
                    showToast("Copied Full Private Key (idkey.*) to clipboard!");
                    return BrowserAction::None;
                }

                if (mx >= screenW - 135.0f && mx <= screenW - 35.0f) {
                    showFullActiveKey = !showFullActiveKey;
                    return BrowserAction::None;
                }
            }

            if (my >= addY && my <= addY + 104.0f) {

                if (my >= addY + 34.0f && my <= addY + 62.0f && mx >= 130.0f && mx <= 310.0f) {
                    activeField = Field_NewKeyLabel;
                    return BrowserAction::None;
                }

                if (my >= addY + 32.0f && my <= addY + 62.0f && mx >= screenW - 420.0f && mx <= screenW - 240.0f) {
                    if (cfg.addAuthKey(newKeyLabelInput, newKeyStringInput)) {
                        newKeyLabelInput.clear();
                        newKeyStringInput.clear();
                        showToast("Added new auth profile successfully!");
                    } else {
                        showToast("Error: Invalid key! Must be format idkey.X.Y.D", true);
                    }
                    return BrowserAction::None;
                }

                if (my >= addY + 32.0f && my <= addY + 62.0f && mx >= screenW - 225.0f && mx <= screenW - 30.0f) {
                    cfg.generateNewAuthKey(newKeyLabelInput);
                    newKeyLabelInput.clear();
                    showToast("Generated new cryptographic ECDSA key!");
                    return BrowserAction::None;
                }

                if (my >= addY + 68.0f && my <= addY + 96.0f && mx >= 130.0f && mx <= screenW - 240.0f) {
                    activeField = Field_NewKeyString;
                    return BrowserAction::None;
                }

                if (my >= addY + 68.0f && my <= addY + 96.0f && mx >= screenW - 225.0f && mx <= screenW - 30.0f) {
                    char* clip = SDL_GetClipboardText();
                    if (clip) {
                        newKeyStringInput = clip;
                        SDL_free(clip);
                        showToast("Pasted key from clipboard!");
                    }
                    return BrowserAction::None;
                }
            }

            float listY = 322.0f;
            float profY = listY + 36.0f;
            float profH = 42.0f;
            float listH = screenH - listY - 20.0f;
            int maxP = static_cast<int>((listH - 44.0f) / profH);
            for (int i = 0; i < maxP && (i + authScrollOffset) < (int)cfg.authProfiles.size(); ++i) {
                int pIdx = i + authScrollOffset;
                const auto& p = cfg.authProfiles[pIdx];
                float ry = profY + i * profH;

                if (my >= ry && my <= ry + 36.0f) {

                    if (mx >= screenW - 340.0f && mx <= screenW - 190.0f) {
                        cfg.setActiveAuth(p.id);
                        showToast("Active key set to: " + p.label);
                        return BrowserAction::None;
                    }

                    if (mx >= screenW - 180.0f && mx <= screenW - 100.0f) {
                        SDL_SetClipboardText(p.key.c_str());
                        showToast("Copied " + p.label + " to clipboard!");
                        return BrowserAction::None;
                    }

                    if (mx >= screenW - 90.0f && mx <= screenW - 30.0f && cfg.authProfiles.size() > 1) {
                        cfg.deleteAuthKey(p.id);
                        showToast("Deleted auth profile.");
                        return BrowserAction::None;
                    }
                }
            }
        }

        else if (currentTab == LauncherTab::Settings) {
            auto& cfg = AppConfig::get();
            float setTop = 72.0f;
            float colW = (screenW - 68.0f) / 3.0f;

            float col1X = 24.0f;
            float sX = col1X + 16.0f;
            float sW = colW - 32.0f;

            if (my >= setTop + 54.0f && my <= setTop + 84.0f && mx >= sX && mx <= sX + sW) {
                draggingSlider = 0;
                cfg.extrapolationMs = static_cast<int>(std::clamp((mx - sX) / sW, 0.0f, 1.0f) * 150.0f);
                return BrowserAction::SettingsChanged;
            }

            if (my >= setTop + 90.0f && my <= setTop + 120.0f && mx >= sX && mx <= col1X + colW - 10.0f) {
                cfg.socdLastInputPriority = !cfg.socdLastInputPriority;
                cfg.save();
                showToast(cfg.socdLastInputPriority ? "Last-Input Priority (SOCD): ON" : "Last-Input Priority (SOCD): OFF");
                return BrowserAction::SettingsChanged;
            }

            float curSliderY = setTop + 124.0f;
            if (cfg.socdLastInputPriority) {

                if (my >= curSliderY && my <= curSliderY + 30.0f && mx >= sX && mx <= sX + sW) {
                    float r = std::clamp((mx - sX) / sW, 0.0f, 1.0f);
                    cfg.socdDelayTicks = static_cast<int>(std::round(r * 4.0f));
                    return BrowserAction::SettingsChanged;
                }
                curSliderY += 40.0f;
            }

            if (my >= curSliderY && my <= curSliderY + 30.0f && mx >= sX && mx <= col1X + colW - 10.0f) {
                cfg.antiFakeShot = !cfg.antiFakeShot;
                cfg.save();
                showToast(cfg.antiFakeShot ? "Anti-Fake Shot: ON" : "Anti-Fake Shot: OFF");
                return BrowserAction::SettingsChanged;
            }
            curSliderY += 34.0f;

            if (my >= curSliderY && my <= curSliderY + 30.0f && mx >= sX && mx <= col1X + colW - 10.0f) {
                cfg.autoFakeShot = !cfg.autoFakeShot;
                cfg.save();
                showToast(cfg.autoFakeShot ? "Auto-Fake Shot: ON" : "Auto-Fake Shot: OFF");
                return BrowserAction::SettingsChanged;
            }
            curSliderY += 34.0f;

            if (cfg.autoFakeShot) {

                if (my >= curSliderY && my <= curSliderY + 30.0f && mx >= sX && mx <= sX + sW) {
                    draggingSlider = 1;
                    cfg.fakeShotIntervalMs = 50 + static_cast<int>(std::clamp((mx - sX) / sW, 0.0f, 1.0f) * 950.0f);
                    return BrowserAction::SettingsChanged;
                }
                curSliderY += 40.0f;
            }

            if (my >= curSliderY && my <= curSliderY + 30.0f && mx >= sX && mx <= sX + sW) {
                draggingSlider = 2;
                float r = std::clamp((mx - sX) / sW, 0.0f, 1.0f);
                int raw = static_cast<int>(r * 1000.0f);
                cfg.realShotIntervalMs = std::clamp(raw, 1, 1000);
                return BrowserAction::SettingsChanged;
            }
            curSliderY += 40.0f;

            if (my >= curSliderY && my <= curSliderY + 30.0f && mx >= sX && mx <= sX + sW) {
                draggingSlider = 3;
                cfg.slowSpeedPercent = 10 + static_cast<int>(std::clamp((mx - sX) / sW, 0.0f, 1.0f) * 80.0f);
                return BrowserAction::SettingsChanged;
            }
            curSliderY += 40.0f;

            if (my >= curSliderY && my <= curSliderY + 30.0f && mx >= sX && mx <= col1X + colW - 10.0f) {
                cfg.firstTouchCushionEnabled = !cfg.firstTouchCushionEnabled;
                cfg.save();
                showToast(cfg.firstTouchCushionEnabled ? "First-Touch Cushion: ON" : "First-Touch Cushion: OFF");
                return BrowserAction::SettingsChanged;
            }
            curSliderY += 34.0f;

            if (my >= curSliderY && my <= curSliderY + 30.0f && mx >= sX && mx <= col1X + colW - 10.0f) {
                cfg.fastFakeEnabled = !cfg.fastFakeEnabled;
                cfg.save();
                showToast(cfg.fastFakeEnabled ? "Fast-Fake Osc [P]: ON" : "Fast-Fake Osc [P]: OFF");
                return BrowserAction::SettingsChanged;
            }
            curSliderY += 34.0f;

            if (cfg.fastFakeEnabled && my >= curSliderY && my <= curSliderY + 30.0f && mx >= sX && mx <= sX + sW) {
                float r = std::clamp((mx - sX) / sW, 0.0f, 1.0f);
                cfg.fastFakeSpeedTicks = 1 + static_cast<int>(std::round(r * 9.0f));
                cfg.save();
                return BrowserAction::SettingsChanged;
            }

            float col2X = col1X + colW + 10.0f;

            if (my >= setTop + 54.0f && my <= setTop + 84.0f && mx >= col2X + 16.0f && mx <= col2X + colW - 10.0f) {
                cfg.vsync = !cfg.vsync;
                cfg.save();
                showToast(cfg.vsync ? "VSync: ON" : "VSync: OFF (Low Latency)");
                return BrowserAction::SettingsChanged;
            }

            if (my >= setTop + 115.0f && my <= setTop + 145.0f && mx >= col2X + 16.0f && mx <= col2X + colW - 16.0f) {
                int caps[] = {60, 120, 144, 240, 500};
                float chipW = (colW - 32.0f) / 5.0f;
                int idx = static_cast<int>((mx - (col2X + 16.0f)) / chipW);
                if (idx >= 0 && idx < 5) {
                    cfg.fpsCap = caps[idx];
                    cfg.save();
                    showToast("FPS Target set to " + (caps[idx] == 500 ? std::string("Uncapped") : std::to_string(caps[idx])));
                    return BrowserAction::SettingsChanged;
                }
            }

            if (my >= setTop + 175.0f && my <= setTop + 205.0f && mx >= col2X + 16.0f && mx <= col2X + colW - 16.0f) {
                float chipW = (colW - 32.0f) / 6.0f;
                int tIdx = static_cast<int>((mx - (col2X + 16.0f)) / chipW);
                if (tIdx >= 0 && tIdx < 6) {
                    cfg.ballTrailStyle = tIdx;
                    cfg.save();
                    showToast("Ball Trail changed");
                    return BrowserAction::SettingsChanged;
                }
            }

            if (my >= setTop + 235.0f && my <= setTop + 300.0f && mx >= col2X + 16.0f && mx <= col2X + colW - 16.0f) {
                float thW = (colW - 32.0f) / 3.0f;
                int col = static_cast<int>((mx - (col2X + 16.0f)) / thW);
                int row = (my >= setTop + 267.0f) ? 1 : 0;
                int thIdx = row * 3 + col;
                if (thIdx >= 0 && thIdx < 6) {
                    cfg.pitchTheme = thIdx;
                    cfg.save();
                    showToast("Pitch Theme changed");
                    return BrowserAction::SettingsChanged;
                }
            }

            float col3X = col2X + colW + 10.0f;
            if (mx >= col3X + 16.0f && mx <= col3X + colW - 10.0f) {
                if (my >= setTop + 54.0f && my <= setTop + 84.0f) {
                    cfg.masterVolume = (cfg.masterVolume > 0.05f) ? 0.0f : 0.8f;
                    cfg.save();
                    showToast(cfg.masterVolume > 0.05f ? "Master Sound: ON" : "Master Sound: MUTED");
                    return BrowserAction::SettingsChanged;
                }
                if (my >= setTop + 90.0f && my <= setTop + 120.0f) {
                    cfg.soundKick = !cfg.soundKick;
                    cfg.save();
                    return BrowserAction::SettingsChanged;
                }
                if (my >= setTop + 126.0f && my <= setTop + 156.0f) {
                    cfg.soundGoal = !cfg.soundGoal;
                    cfg.save();
                    return BrowserAction::SettingsChanged;
                }
                if (my >= setTop + 162.0f && my <= setTop + 192.0f) {
                    cfg.soundChat = !cfg.soundChat;
                    cfg.save();
                    return BrowserAction::SettingsChanged;
                }
            }

            if (my >= screenH - 65 && my <= screenH - 33 && mx >= screenW - 240 && mx <= screenW - 40) {
                cfg.extrapolationMs = 0;
                cfg.antiFakeShot = true;
                cfg.autoFakeShot = true;
                cfg.firstTouchCushionEnabled = true;
                cfg.fastFakeEnabled = true;
                cfg.fastFakeSpeedTicks = 2;
                cfg.autoGoalEnabled = true;
                cfg.autoGoalAnyGoal = false;
                cfg.autoGoalAccuracy = 95;
                cfg.aimLineDistance = 40;
                cfg.showLiveAimTrajectory = true;
                cfg.showBallRollTrajectory = true;
                cfg.showGkDangerCone = true;
                cfg.showOtherGoalsCone = true;
                cfg.fakeShotIntervalMs = 500;
                cfg.realShotIntervalMs = 60;
                cfg.slowSpeedPercent = 50;
                cfg.vsync = false;
                cfg.fpsCap = 240;
                cfg.ballTrailStyle = 1;
                cfg.pitchTheme = 0;
                cfg.masterVolume = 0.8f;
                cfg.soundKick = true;
                cfg.soundGoal = true;
                cfg.soundChat = true;
                cfg.save();
                showToast("All settings reset to esports defaults!");
                return BrowserAction::SettingsChanged;
            }
        }
        else if (currentTab == LauncherTab::Shortcuts) {
            float startY = 72.0f + 72.0f;
            float rowH = 46.0f;
            auto& cfg = AppConfig::get();
            for (int i = 0; i < 6; ++i) {
                float ry = startY + i * rowH;

                if (my >= ry && my <= ry + 38.0f && mx >= screenW - 200 && mx <= screenW - 90) {
                    rebindingKey = i;
                    showToast("Press any key to bind, or Escape to cancel...");
                    return BrowserAction::None;
                }

                if (my >= ry && my <= ry + 38.0f && mx >= screenW - 82 && mx <= screenW - 44) {
                    if (i == 0) cfg.keySlide = 0;
                    else if (i == 1) cfg.keySprint = 0;
                    else if (i == 2) cfg.keySprintAlt = 0;
                    else if (i == 3) cfg.keyRealShot = 0;
                    else if (i == 4) cfg.keySlowMove = 0;
                    else if (i == 5) cfg.keyFakeToggle = 0;
                    cfg.save();
                    showToast("Shortcut removed!");
                    return BrowserAction::SettingsChanged;
                }
            }

            if (my >= screenH - 75 && my <= screenH - 35 && mx >= screenW - 240 && mx <= screenW - 40) {
                auto& cfg = AppConfig::get();
                cfg.keySlide = SDLK_c;
                cfg.keySprint = SDLK_v;
                cfg.keySprintAlt = SDLK_e;
                cfg.keyRealShot = SDLK_n;
                cfg.keySlowMove = SDLK_m;
                cfg.keyFakeToggle = SDLK_f;
                cfg.save();
                showToast("Shortcuts reset to defaults!");
                return BrowserAction::SettingsChanged;
            }
        }
    } else if (event.type == SDL_MOUSEWHEEL) {
        if (showFlagPicker) {
            if (event.wheel.y > 0) flagScrollOffset = std::max(0, flagScrollOffset - 3);
            else if (event.wheel.y < 0) flagScrollOffset = std::min(std::max(0, (int)filteredCountries.size() - 8), flagScrollOffset + 3);
        } else if (currentTab == LauncherTab::Rooms) {
            std::lock_guard<std::mutex> lock(roomsMutex);
            if (event.wheel.y > 0) scrollOffset = std::max(0, scrollOffset - 3);
            else if (event.wheel.y < 0) scrollOffset = std::min(std::max(0, (int)filteredRooms.size() - 6), scrollOffset + 3);
        } else if (currentTab == LauncherTab::Auth) {
            auto& cfg = AppConfig::get();
            if (event.wheel.y > 0) authScrollOffset = std::max(0, authScrollOffset - 2);
            else if (event.wheel.y < 0) authScrollOffset = std::min(std::max(0, (int)cfg.authProfiles.size() - 4), authScrollOffset + 2);
        }
    } else if (event.type == SDL_TEXTINPUT) {
        if (activeField == Field_Name && FontRenderer::countUTF8Codepoints(playerNameInput) < 24) {
            playerNameInput += event.text.text;
            syncUIToConfig();
        } else if (activeField == Field_Avatar) {
            std::string cand = playerAvatarInput + event.text.text;
            playerAvatarInput = FontRenderer::clipUTF8Codepoints(cand, 2);
            syncUIToConfig();
        } else if (activeField == Field_Flag && playerFlagInput.size() < 3) {
            playerFlagInput += event.text.text;
            syncUIToConfig();
        } else if (activeField == Field_DirectId && directIdInput.size() < 64) {
            directIdInput += event.text.text;
            syncUIToConfig();
        } else if (activeField == Field_Password && passwordInput.size() < 32) {
            passwordInput += event.text.text;
            syncUIToConfig();
        } else if (activeField == Field_Search && searchQueryInput.size() < 32) {
            searchQueryInput += event.text.text;
            std::lock_guard<std::mutex> lock(roomsMutex);
            rebuildFilteredList();
        } else if (activeField == Field_FlagSearch && flagSearchInput.size() < 24) {
            flagSearchInput += event.text.text;
            rebuildFilteredCountries();
        } else if (activeField == Field_NewKeyLabel && newKeyLabelInput.size() < 28) {
            newKeyLabelInput += event.text.text;
        } else if (activeField == Field_NewKeyString && newKeyStringInput.size() < 256) {
            newKeyStringInput += event.text.text;
        }
    } else if (event.type == SDL_KEYDOWN) {
        bool ctrlOrCmd = (SDL_GetModState() & (KMOD_CTRL | KMOD_GUI)) != 0;

        if (ctrlOrCmd && event.key.keysym.sym == SDLK_v) {
            if (SDL_HasClipboardText()) {
                char* clip = SDL_GetClipboardText();
                if (clip) {
                    std::string clipStr(clip);
                    SDL_free(clip);
                    if (activeField == Field_Name) {
                        playerNameInput += clipStr;
                        playerNameInput = FontRenderer::clipUTF8Codepoints(playerNameInput, 24);
                        syncUIToConfig();
                        showToast("Pasted Nickname from clipboard!");
                    } else if (activeField == Field_Avatar) {
                        playerAvatarInput = FontRenderer::clipUTF8Codepoints(clipStr, 2);
                        syncUIToConfig();
                        showToast("Pasted Avatar emoji!");
                    } else if (activeField == Field_DirectId) {
                        directIdInput = clipStr;
                        syncUIToConfig();
                    } else if (activeField == Field_Password) {
                        passwordInput = clipStr;
                        syncUIToConfig();
                    } else if (activeField == Field_Search) {
                        searchQueryInput += clipStr;
                        std::lock_guard<std::mutex> lock(roomsMutex);
                        rebuildFilteredList();
                    } else if (activeField == Field_NewKeyLabel) {
                        newKeyLabelInput += clipStr;
                    } else if (activeField == Field_NewKeyString) {
                        newKeyStringInput = clipStr;
                    } else if (activeField == Field_FlagSearch) {
                        flagSearchInput = clipStr;
                        rebuildFilteredCountries();
                    }
                    return BrowserAction::None;
                }
            }
        }

        if (rebindingKey >= 0) {
            if (event.key.keysym.sym != SDLK_ESCAPE) {
                auto& cfg = AppConfig::get();
                int newKey = event.key.keysym.sym;
                if (rebindingKey == 0) cfg.keySlide = newKey;
                else if (rebindingKey == 1) cfg.keySprint = newKey;
                else if (rebindingKey == 2) cfg.keySprintAlt = newKey;
                else if (rebindingKey == 3) cfg.keyRealShot = newKey;
                else if (rebindingKey == 4) cfg.keySlowMove = newKey;
                else if (rebindingKey == 5) cfg.keyFakeToggle = newKey;
                cfg.save();
                showToast("Key successfully rebound!");
            }
            rebindingKey = -1;
            return BrowserAction::SettingsChanged;
        }

        if (event.key.keysym.sym == SDLK_ESCAPE) {
            if (showFlagPicker) {
                showFlagPicker = false;
                return BrowserAction::None;
            }
            activeField = Field_None;
        } else if (event.key.keysym.sym == SDLK_BACKSPACE) {
            if (activeField == Field_Name) { popBackUTF8(playerNameInput); syncUIToConfig(); }
            else if (activeField == Field_Avatar) { popBackUTF8(playerAvatarInput); syncUIToConfig(); }
            else if (activeField == Field_Flag) { popBackUTF8(playerFlagInput); syncUIToConfig(); }
            else if (activeField == Field_DirectId) { popBackUTF8(directIdInput); syncUIToConfig(); }
            else if (activeField == Field_Password) { popBackUTF8(passwordInput); syncUIToConfig(); }
            else if (activeField == Field_NewKeyLabel) popBackUTF8(newKeyLabelInput);
            else if (activeField == Field_NewKeyString) popBackUTF8(newKeyStringInput);
            else if (activeField == Field_FlagSearch && !flagSearchInput.empty()) {
                popBackUTF8(flagSearchInput);
                rebuildFilteredCountries();
            } else if (activeField == Field_Search && !searchQueryInput.empty()) {
                popBackUTF8(searchQueryInput);
                std::lock_guard<std::mutex> lock(roomsMutex);
                rebuildFilteredList();
            }
        } else if (event.key.keysym.sym == SDLK_RETURN) {
            if (showFlagPicker) {
                if (!filteredCountries.empty()) {
                    playerFlagInput = filteredCountries[0].code;
                    syncUIToConfig();
                    showFlagPicker = false;
                    showToast("Flag set to " + filteredCountries[0].name);
                }
                return BrowserAction::None;
            }
            if (currentTab == LauncherTab::Rooms) {
                if (activeField == Field_DirectId && !directIdInput.empty()) {
                    syncUIToConfig();
                    return BrowserAction::JoinDirectId;
                }
                if (selectedIndex >= 0) {
                    syncUIToConfig();
                    return BrowserAction::JoinSelectedRoom;
                }
            }
        }
    }

    return BrowserAction::None;
}

void RoomBrowser::renderHeader(SDL_Renderer* renderer, int screenW, int) {

    GameRenderer::drawRoundedRect(renderer, 0.0f, 0.0f, (float)screenW, 54.0f, 0.0f, {13, 17, 24, 255}, {30, 38, 52, 255}, 1.0f);

    fontTitle.drawText(renderer, "HAXBALL C++", 24.0f, 13.0f, {56, 189, 248, 255});

    auto drawTab = [&](const std::string& name, float x, float w, LauncherTab tab) {
        bool sel = (currentTab == tab);
        if (sel) {
            GameRenderer::drawRoundedRect(renderer, x, 10.0f, w, 34.0f, 4.0f, {22, 30, 44, 255});
            GameRenderer::drawRoundedRect(renderer, x + 8.0f, 43.0f, w - 16.0f, 2.5f, 1.0f, {56, 189, 248, 255});
        }
        font.drawText(renderer, name, x + w * 0.5f, 19.0f,
                      sel ? SDL_Color{255, 255, 255, 255} : SDL_Color{140, 155, 175, 255},
                      TextAlign::Center);
    };

    drawTab("ROOMS", 200.0f, 85.0f, LauncherTab::Rooms);
    drawTab("AUTH", 295.0f, 75.0f, LauncherTab::Auth);
    drawTab("SETTINGS", 380.0f, 95.0f, LauncherTab::Settings);
    drawTab("SHORTCUTS", 485.0f, 105.0f, LauncherTab::Shortcuts);

    float curR = (float)screenW - 14.0f;

    curR -= 100.0f;
    GameRenderer::drawRoundedRect(renderer, curR, 10.0f, 100.0f, 34.0f, 4.0f, {16, 88, 48, 255}, {34, 197, 94, 200}, 1.0f);
    fontSmall.drawText(renderer, "▶ Practice", curR + 50.0f, 19.0f, {255, 255, 255, 255}, TextAlign::Center);

    curR -= 120.0f;
    GameRenderer::drawRoundedRect(renderer, curR, 10.0f, 112.0f, 34.0f, 4.0f, {30, 58, 115, 255}, {56, 189, 248, 220}, 1.0f);
    fontSmall.drawText(renderer, "🥅 Penalties", curR + 56.0f, 19.0f, {255, 255, 255, 255}, TextAlign::Center);

    curR -= 90.0f;
    GameRenderer::drawRoundedRect(renderer, curR, 10.0f, 82.0f, 34.0f, 4.0f, {20, 27, 38, 255}, {40, 52, 72, 200}, 1.0f);
    fontSmall.drawText(renderer, "[" + playerFlagInput + "] Flag ▼", curR + 41.0f, 19.0f, {200, 215, 235, 255}, TextAlign::Center);

    curR -= 46.0f;
    bool activeAv = (activeField == Field_Avatar);
    GameRenderer::drawRoundedRect(renderer, curR, 10.0f, 38.0f, 34.0f, 4.0f,
                                  {20, 27, 38, 255},
                                  activeAv ? SDL_Color{56, 189, 248, 255} : SDL_Color{40, 52, 72, 200},
                                  activeAv ? 1.5f : 1.0f);
    font.drawText(renderer, playerAvatarInput, curR + 19.0f, 18.0f, {255, 255, 255, 255}, TextAlign::Center);

    curR -= 138.0f;
    bool activeNick = (activeField == Field_Name);
    GameRenderer::drawRoundedRect(renderer, curR, 10.0f, 130.0f, 34.0f, 4.0f,
                                  {16, 22, 32, 255},
                                  activeNick ? SDL_Color{56, 189, 248, 255} : SDL_Color{40, 52, 72, 200},
                                  activeNick ? 1.5f : 1.0f);
    fontSmall.drawText(renderer, playerNameInput + (activeNick ? "_" : ""), curR + 10.0f, 19.0f, {255, 255, 255, 255});
    fontSmall.drawText(renderer, "Nick:", curR - 38.0f, 20.0f, {120, 140, 165, 255});

    curR -= 80.0f;
    GameRenderer::drawRoundedRect(renderer, curR, 10.0f, 34.0f, 34.0f, 4.0f,
                                  {20, 27, 38, 255},
                                  {56, 189, 248, 180}, 1.0f);
    fontSmall.drawText(renderer, "𝒻", curR + 17.0f, 19.0f, {56, 189, 248, 255}, TextAlign::Center);
}

void RoomBrowser::renderProfileBar(SDL_Renderer*, int, int) {

}

void RoomBrowser::renderRoomsTab(SDL_Renderer* renderer, int screenW, int screenH) {
    float tbY = 66.0f;

    bool activeSearch = (activeField == Field_Search);
    GameRenderer::drawRoundedRect(renderer, 24.0f, tbY, 276.0f, 32.0f, 5.0f, {12, 16, 24, 255},
                                  activeSearch ? SDL_Color{56, 189, 248, 255} : SDL_Color{50, 64, 88, 200},
                                  activeSearch ? 1.6f : 1.0f);
    std::string displaySearch = searchQueryInput.empty() ? "Search room, LDC, flag..." : searchQueryInput;
    SDL_Color sCol = searchQueryInput.empty() ? SDL_Color{100, 120, 145, 200} : SDL_Color{255, 255, 255, 255};
    fontSmall.drawText(renderer, displaySearch + (activeSearch ? "_" : ""), 34.0f, tbY + 9.0f, sCol);

    if (!searchQueryInput.empty()) {
        fontSmall.drawText(renderer, "✕", 284.0f, tbY + 9.0f, {180, 200, 220, 255});
    }

    auto drawChip = [&](const std::string& name, float cx, float cw, bool sel) {
        SDL_Color bg = sel ? SDL_Color{37, 99, 235, 255} : SDL_Color{20, 28, 42, 220};
        SDL_Color br = sel ? SDL_Color{147, 197, 253, 240} : SDL_Color{48, 62, 86, 180};
        GameRenderer::drawRoundedRect(renderer, cx, tbY, cw, 32.0f, 5.0f, bg, br, sel ? 1.4f : 1.0f);
        fontSmall.drawTextShadow(renderer, name, cx + cw * 0.5f, tbY + 9.0f,
                                 sel ? SDL_Color{255, 255, 255, 255} : SDL_Color{160, 180, 205, 240},
                                 TextAlign::Center);
    };

    drawChip("All", 310.0f, 55.0f, quickFilter == QuickFilter::All);
    drawChip("🏆 LDC Only", 375.0f, 90.0f, quickFilter == QuickFilter::LDC);
    drawChip("Open Only", 475.0f, 90.0f, quickFilter == QuickFilter::OpenOnly);
    drawChip("Has Slots", 575.0f, 90.0f, quickFilter == QuickFilter::HasSlots);

    GameRenderer::drawRoundedRect(renderer, screenW - 120.0f, tbY, 96.0f, 32.0f, 5.0f, {30, 42, 60, 240}, {60, 85, 125, 220}, 1.2f);
    fontSmall.drawTextShadow(renderer, "⟳ Refresh", screenW - 72.0f, tbY + 9.0f, {220, 235, 255, 255}, TextAlign::Center);

    float tableTop = 106.0f;
    float tableH = screenH - 176.0f;
    GameRenderer::drawRoundedRect(renderer, 16.0f, tableTop, screenW - 32.0f, tableH, 8.0f, {15, 20, 30, 240}, {38, 50, 72, 220}, 1.2f);

    GameRenderer::drawRoundedRect(renderer, 20.0f, tableTop + 4.0f, screenW - 40.0f, 30.0f, 5.0f, {11, 15, 23, 255});
    fontSmall.drawText(renderer, "Flag", 36.0f, tableTop + 11.0f, {125, 145, 175, 255});
    fontSmall.drawText(renderer, "Room Name", 105.0f, tableTop + 11.0f, {125, 145, 175, 255});
    fontSmall.drawText(renderer, "Room ID", screenW - 350.0f, tableTop + 11.0f, {125, 145, 175, 255});
    fontSmall.drawText(renderer, "Players", screenW - 190.0f, tableTop + 11.0f, {125, 145, 175, 255});
    fontSmall.drawText(renderer, "Access", screenW - 90.0f, tableTop + 11.0f, {125, 145, 175, 255});

    {
        std::lock_guard<std::mutex> lock(roomsMutex);
        float rowY = tableTop + 38.0f;
        float rowHeight = 36.0f;
        int maxRows = static_cast<int>((tableH - 44.0f) / rowHeight);

        for (int i = 0; i < maxRows && (i + scrollOffset) < (int)filteredRooms.size(); ++i) {
            int idx = i + scrollOffset;
            const auto& r = filteredRooms[idx];
            bool isSel = (idx == selectedIndex);

            SDL_Color rowCol = isSel
                ? SDL_Color{35, 75, 135, 255}
                : ((idx % 2 == 0) ? SDL_Color{20, 27, 40, 255} : SDL_Color{16, 22, 33, 255});
            SDL_Color borderCol = isSel ? SDL_Color{56, 189, 248, 240} : SDL_Color{0, 0, 0, 0};

            GameRenderer::drawRoundedRect(renderer, 22.0f, rowY, screenW - 44.0f, rowHeight - 2.0f, 4.0f, rowCol, borderCol, isSel ? 1.4f : 0.0f);

            GameRenderer::drawRoundedRect(renderer, 32.0f, rowY + 6.0f, 42.0f, 22.0f, 3.5f, {26, 38, 56, 240}, {50, 70, 100, 180}, 1.0f);
            fontSmall.drawText(renderer, r.flag, 53.0f, rowY + 10.0f, {220, 235, 255, 255}, TextAlign::Center);

            std::string lowerName = toLowerAscii(r.name);
            bool isLdc = (lowerName.find("ldc") != std::string::npos);
            if (isLdc) {
                GameRenderer::drawRoundedRect(renderer, 85.0f, rowY + 7.0f, 46.0f, 20.0f, 3.0f, {180, 83, 9, 220}, {251, 191, 36, 200}, 1.0f);
                fontSmall.drawTextShadow(renderer, "LDC", 108.0f, rowY + 10.0f, {255, 255, 255, 255}, TextAlign::Center);
            }

            float textX = isLdc ? 140.0f : 90.0f;
            font.drawTextShadow(renderer, r.name, textX, rowY + 9.0f, {245, 248, 255, 255}, TextAlign::Left, 0.92f);
            fontSmall.drawText(renderer, r.id, screenW - 350.0f, rowY + 10.0f, {135, 160, 190, 240});

            std::string pCount = std::to_string(r.players) + " / " + std::to_string(r.maxPlayers);
            SDL_Color pCol = (r.players >= r.maxPlayers) ? SDL_Color{239, 68, 68, 255} : (r.players > 0 ? SDL_Color{34, 197, 94, 255} : SDL_Color{148, 163, 184, 255});
            fontSmall.drawTextShadow(renderer, pCount, screenW - 190.0f, rowY + 10.0f, pCol);

            if (r.hasPassword) {
                GameRenderer::drawRoundedRect(renderer, screenW - 95.0f, rowY + 7.0f, 54.0f, 20.0f, 3.0f, {120, 53, 15, 220}, {245, 158, 11, 200}, 1.0f);
                fontSmall.drawText(renderer, "🔒 PASS", screenW - 68.0f, rowY + 10.0f, {254, 240, 138, 255}, TextAlign::Center);
            } else {
                GameRenderer::drawRoundedRect(renderer, screenW - 95.0f, rowY + 7.0f, 54.0f, 20.0f, 3.0f, {20, 83, 45, 220}, {34, 197, 94, 200}, 1.0f);
                fontSmall.drawText(renderer, "OPEN", screenW - 68.0f, rowY + 10.0f, {220, 252, 231, 255}, TextAlign::Center);
            }

            rowY += rowHeight;
        }
    }

    float botY = screenH - 58.0f;
    GameRenderer::drawRoundedRect(renderer, 16.0f, botY - 4.0f, screenW - 32.0f, 48.0f, 8.0f, {18, 24, 34, 255}, {42, 56, 80, 220}, 1.2f);

    auto drawInputField = [&](const std::string& label, float lx, float bx, float bw, const std::string& val, ActiveField field) {
        fontSmall.drawText(renderer, label, lx, botY + 10.0f, {150, 175, 205, 255});
        bool active = (activeField == field);
        GameRenderer::drawRoundedRect(renderer, bx, botY + 3.0f, bw, 32.0f, 5.0f, {11, 15, 22, 255},
                                      active ? SDL_Color{56, 189, 248, 255} : SDL_Color{48, 62, 86, 200},
                                      active ? 1.6f : 1.0f);
        fontSmall.drawText(renderer, val + (active ? "_" : ""), bx + 8.0f, botY + 10.0f, {245, 248, 255, 255});
    };

    drawInputField("Direct ID:", 26.0f, 85.0f, 210.0f, directIdInput, Field_DirectId);
    drawInputField("Pass:", 310.0f, 350.0f, 130.0f, passwordInput, Field_Password);

    GameRenderer::drawRoundedRect(renderer, 495.0f, botY + 3.0f, 120.0f, 32.0f, 5.0f, {30, 64, 120, 255}, {59, 130, 246, 220}, 1.2f);
    fontSmall.drawTextShadow(renderer, "Join ID", 555.0f, botY + 11.0f, {255, 255, 255, 255}, TextAlign::Center);

    bool canJoin = (selectedIndex >= 0 && selectedIndex < (int)filteredRooms.size());
    SDL_Color joinBg = canJoin ? SDL_Color{2, 132, 199, 255} : SDL_Color{30, 41, 59, 255};
    SDL_Color joinBorder = canJoin ? SDL_Color{56, 189, 248, 220} : SDL_Color{51, 65, 85, 180};
    GameRenderer::drawRoundedRect(renderer, screenW - 195.0f, botY + 3.0f, 165.0f, 32.0f, 5.0f, joinBg, joinBorder, 1.3f);
    font.drawTextShadow(renderer, "JOIN ROOM ▶", screenW - 112.5f, botY + 9.0f, {255, 255, 255, 255}, TextAlign::Center);
}

void RoomBrowser::renderAuthTab(SDL_Renderer* renderer, int screenW, int screenH) {
    auto& cfg = AppConfig::get();

    float cardY = 72.0f;
    GameRenderer::drawRoundedRect(renderer, 24.0f, cardY, screenW - 48.0f, 120.0f, 4.0f, {18, 24, 34, 255}, {36, 48, 68, 255}, 1.0f);

    fontLarge.drawTextShadow(renderer, "✔ Active Identity Profile: " + cfg.getActiveAuthLabel(), 40.0f, cardY + 14.0f, {56, 189, 248, 255});
    std::string key = cfg.getActiveAuthKey();

    std::string pubId = "N/A";
    size_t p1 = key.find('.');
    if (p1 != std::string::npos) {
        size_t p2 = key.find('.', p1 + 1);
        if (p2 != std::string::npos) {
            pubId = key.substr(p1 + 1, p2 - p1 - 1);
        }
    }

    fontSmall.drawText(renderer, "Public ID (Shareable): " + pubId, 40.0f, cardY + 44.0f, {134, 239, 172, 255});

    std::string dispKey = showFullActiveKey ? key : (key.substr(0, 24) + " ... " + key.substr(std::max(0, (int)key.size() - 16)));
    fontSmall.drawText(renderer, "Private Key (Secret):  " + dispKey, 40.0f, cardY + 68.0f, {251, 146, 60, 255});
    fontSmall.drawText(renderer, "Saved locally in haxball_config.json. Never share your Private Key with anyone.", 40.0f, cardY + 92.0f, {120, 145, 175, 220});

    GameRenderer::drawRoundedRect(renderer, screenW - 410.0f, cardY + 16.0f, 135.0f, 32.0f, 5.0f, {20, 83, 45, 255}, {34, 197, 94, 220}, 1.2f);
    fontSmall.drawTextShadow(renderer, "📋 Copy Public ID", screenW - 342.5f, cardY + 24.0f, {255, 255, 255, 255}, TextAlign::Center);

    GameRenderer::drawRoundedRect(renderer, screenW - 265.0f, cardY + 16.0f, 120.0f, 32.0f, 5.0f, {30, 64, 120, 255}, {59, 130, 246, 220}, 1.2f);
    fontSmall.drawTextShadow(renderer, "📋 Copy Key", screenW - 205.0f, cardY + 24.0f, {255, 255, 255, 255}, TextAlign::Center);

    GameRenderer::drawRoundedRect(renderer, screenW - 135.0f, cardY + 16.0f, 100.0f, 32.0f, 5.0f, {26, 36, 52, 255}, {60, 80, 115, 200}, 1.0f);
    fontSmall.drawText(renderer, showFullActiveKey ? "👁️ Hide" : "👁️ Show", screenW - 85.0f, cardY + 24.0f, {200, 220, 245, 255}, TextAlign::Center);

    float addY = 205.0f;
    GameRenderer::drawRoundedRect(renderer, 24.0f, addY, screenW - 48.0f, 104.0f, 4.0f, {18, 24, 34, 255}, {36, 48, 68, 255}, 1.0f);
    font.drawTextShadow(renderer, "Add or Import Key", 40.0f, addY + 10.0f, {220, 235, 255, 255});

    fontSmall.drawText(renderer, "Profile Label:", 40.0f, addY + 40.0f, {140, 165, 195, 255});
    bool activeLabel = (activeField == Field_NewKeyLabel);
    GameRenderer::drawRoundedRect(renderer, 130.0f, addY + 34.0f, 180.0f, 28.0f, 4.0f, {10, 14, 22, 255},
                                  activeLabel ? SDL_Color{56, 189, 248, 255} : SDL_Color{45, 60, 85, 200},
                                  activeLabel ? 1.5f : 1.0f);
    fontSmall.drawText(renderer, newKeyLabelInput + (activeLabel ? "_" : ""), 138.0f, addY + 39.0f, {255, 255, 255, 255});

    fontSmall.drawText(renderer, "idkey String:", 40.0f, addY + 74.0f, {140, 165, 195, 255});
    bool activeKey = (activeField == Field_NewKeyString);
    float keyW = screenW - 240.0f - 130.0f;
    GameRenderer::drawRoundedRect(renderer, 130.0f, addY + 68.0f, keyW, 28.0f, 4.0f, {10, 14, 22, 255},
                                  activeKey ? SDL_Color{56, 189, 248, 255} : SDL_Color{45, 60, 85, 200},
                                  activeKey ? 1.5f : 1.0f);
    std::string keyDisp = newKeyStringInput.empty() ? "Paste idkey.X.Y.D here..." : newKeyStringInput;
    SDL_Color kdCol = newKeyStringInput.empty() ? SDL_Color{100, 120, 145, 180} : SDL_Color{255, 255, 255, 255};
    fontSmall.drawText(renderer, keyDisp + (activeKey ? "_" : ""), 138.0f, addY + 73.0f, kdCol);

    GameRenderer::drawRoundedRect(renderer, screenW - 225.0f, addY + 68.0f, 195.0f, 28.0f, 4.0f, {30, 45, 68, 255}, {60, 85, 125, 200}, 1.0f);
    fontSmall.drawText(renderer, "📋 Paste Clipboard", screenW - 127.5f, addY + 73.0f, {210, 230, 255, 255}, TextAlign::Center);

    GameRenderer::drawRoundedRect(renderer, screenW - 420.0f, addY + 32.0f, 175.0f, 30.0f, 5.0f, {20, 83, 45, 255}, {34, 197, 94, 220}, 1.2f);
    fontSmall.drawTextShadow(renderer, "+ Add & Save Key", screenW - 332.5f, addY + 39.0f, {255, 255, 255, 255}, TextAlign::Center);

    GameRenderer::drawRoundedRect(renderer, screenW - 225.0f, addY + 32.0f, 195.0f, 30.0f, 5.0f, {124, 45, 18, 255}, {249, 115, 22, 220}, 1.2f);
    fontSmall.drawTextShadow(renderer, "⚡ Generate Fresh Key", screenW - 127.5f, addY + 39.0f, {255, 255, 255, 255}, TextAlign::Center);

    float listY = 322.0f;
    float listH = screenH - listY - 20.0f;
    GameRenderer::drawRoundedRect(renderer, 24.0f, listY, screenW - 48.0f, listH, 4.0f, {18, 24, 34, 255}, {36, 48, 68, 255}, 1.0f);

    font.drawTextShadow(renderer, "Saved Identity Profiles", 40.0f, listY + 10.0f, {220, 235, 255, 255});

    float profY = listY + 36.0f;
    float profH = 42.0f;
    int maxP = static_cast<int>((listH - 44.0f) / profH);

    for (int i = 0; i < maxP && (i + authScrollOffset) < (int)cfg.authProfiles.size(); ++i) {
        int pIdx = i + authScrollOffset;
        const auto& p = cfg.authProfiles[pIdx];
        bool isActive = (p.id == cfg.activeAuthId);

        float ry = profY + i * profH;
        SDL_Color bgCol = isActive ? SDL_Color{22, 38, 58, 255} : SDL_Color{16, 22, 32, 240};
        SDL_Color brCol = isActive ? SDL_Color{56, 189, 248, 220} : SDL_Color{35, 48, 68, 180};
        GameRenderer::drawRoundedRect(renderer, 34.0f, ry, screenW - 68.0f, 36.0f, 5.0f, bgCol, brCol, isActive ? 1.4f : 1.0f);

        font.drawTextShadow(renderer, p.label, 48.0f, ry + 8.0f, isActive ? SDL_Color{56, 189, 248, 255} : SDL_Color{240, 245, 255, 255});
        std::string sub = p.key.substr(0, 16) + "..." + p.key.substr(std::max(0, (int)p.key.size() - 10));
        fontSmall.drawText(renderer, sub, 260.0f, ry + 10.0f, {140, 165, 195, 220});

        if (isActive) {
            GameRenderer::drawRoundedRect(renderer, screenW - 340.0f, ry + 4.0f, 140.0f, 28.0f, 4.0f, {20, 83, 45, 240}, {34, 197, 94, 220}, 1.0f);
            fontSmall.drawText(renderer, "✔ Active In-Play", screenW - 270.0f, ry + 9.0f, {220, 252, 231, 255}, TextAlign::Center);
        } else {
            GameRenderer::drawRoundedRect(renderer, screenW - 340.0f, ry + 4.0f, 140.0f, 28.0f, 4.0f, {30, 58, 95, 255}, {56, 189, 248, 200}, 1.0f);
            fontSmall.drawTextShadow(renderer, "Play With Key", screenW - 270.0f, ry + 9.0f, {255, 255, 255, 255}, TextAlign::Center);
        }

        GameRenderer::drawRoundedRect(renderer, screenW - 180.0f, ry + 4.0f, 75.0f, 28.0f, 4.0f, {26, 38, 56, 240}, {60, 85, 125, 200}, 1.0f);
        fontSmall.drawText(renderer, "📋 Copy", screenW - 142.5f, ry + 9.0f, {210, 230, 255, 255}, TextAlign::Center);

        if (cfg.authProfiles.size() > 1) {
            GameRenderer::drawRoundedRect(renderer, screenW - 90.0f, ry + 4.0f, 55.0f, 28.0f, 4.0f, {127, 29, 29, 240}, {239, 68, 68, 200}, 1.0f);
            fontSmall.drawText(renderer, "🗑️", screenW - 62.5f, ry + 9.0f, {255, 255, 255, 255}, TextAlign::Center);
        }
    }
}

void RoomBrowser::renderSettingsTab(SDL_Renderer* renderer, int screenW, int screenH) {
    auto& cfg = AppConfig::get();
    float setTop = 72.0f;
    float setH = screenH - setTop - 24.0f;

    float colW = (screenW - 68.0f) / 3.0f;

    auto drawSlider = [&](const std::string& label, const std::string& valStr, float x, float y, float w, float ratio) {
        fontSmall.drawText(renderer, label, x, y, {180, 200, 225, 255});
        fontSmall.drawText(renderer, valStr, x + w, y, {56, 189, 248, 255}, TextAlign::Right);
        float by = y + 18.0f;
        GameRenderer::drawRoundedRect(renderer, x, by, w, 10.0f, 5.0f, {10, 14, 22, 255}, {40, 52, 74, 200}, 1.0f);
        GameRenderer::drawRoundedRect(renderer, x, by, w * std::clamp(ratio, 0.0f, 1.0f), 10.0f, 5.0f, {56, 189, 248, 255});
    };

    auto drawToggle = [&](const std::string& label, bool enabled, float x, float y) {
        fontSmall.drawText(renderer, label, x, y + 5.0f, {200, 215, 235, 255});
        SDL_Color bg = enabled ? SDL_Color{20, 83, 45, 255} : SDL_Color{35, 45, 62, 255};
        SDL_Color br = enabled ? SDL_Color{34, 197, 94, 220} : SDL_Color{60, 75, 100, 200};
        GameRenderer::drawRoundedRect(renderer, x + colW - 75.0f, y, 65.0f, 26.0f, 4.0f, bg, br, 1.2f);
        fontSmall.drawText(renderer, enabled ? "ON" : "OFF", x + colW - 42.5f, y + 5.0f, {255, 255, 255, 255}, TextAlign::Center);
    };

    float col1X = 24.0f;
    GameRenderer::drawRoundedRect(renderer, col1X, setTop, colW, setH, 8.0f, {15, 20, 30, 240}, {38, 50, 72, 220}, 1.2f);
    fontLarge.drawTextShadow(renderer, "⚡ Netcode & Prediction", col1X + 16.0f, setTop + 14.0f, {56, 189, 248, 255});

    drawSlider("Extrapolation:", std::to_string(cfg.extrapolationMs) + " ms", col1X + 16.0f, setTop + 54.0f, colW - 32.0f, cfg.extrapolationMs / 150.0f);
    drawToggle("Last-Input Priority (SOCD):", cfg.socdLastInputPriority, col1X + 16.0f, setTop + 90.0f);

    float curSliderY = setTop + 124.0f;
    if (cfg.socdLastInputPriority) {
        std::string delayStr = (cfg.socdDelayTicks == 0) ? "0 ticks (Instant)" : (std::to_string(cfg.socdDelayTicks) + " ticks (" + std::to_string(cfg.socdDelayTicks * 16) + "ms)");
        drawSlider("SOCD Transition Delay:", delayStr, col1X + 16.0f, curSliderY, colW - 32.0f, cfg.socdDelayTicks / 4.0f);
        curSliderY += 40.0f;
    }

    drawToggle("Anti-Fake Shot Filter:", cfg.antiFakeShot, col1X + 16.0f, curSliderY);
    curSliderY += 34.0f;
    drawToggle("Auto-Fake Shot Near Ball:", cfg.autoFakeShot, col1X + 16.0f, curSliderY);
    curSliderY += 34.0f;

    if (cfg.autoFakeShot) {
        std::string fakeCdStr = (cfg.fakeShotIntervalMs >= 1000) ? "1.00 s" : (std::to_string(cfg.fakeShotIntervalMs) + " ms");
        drawSlider("Fake Shot Cooldown:", fakeCdStr, col1X + 16.0f, curSliderY, colW - 32.0f, (cfg.fakeShotIntervalMs - 50.0f) / 950.0f);
        curSliderY += 40.0f;
    }

    drawSlider("Auto-Click Speed:", std::to_string(cfg.realShotIntervalMs) + " ms", col1X + 16.0f, curSliderY, colW - 32.0f, (cfg.realShotIntervalMs - 1.0f) / 999.0f);
    curSliderY += 40.0f;

    std::string slowStr = std::to_string(cfg.slowSpeedPercent) + "% speed";
    drawSlider("Hold 'M' Slow Movement:", slowStr, col1X + 16.0f, curSliderY, colW - 32.0f, (cfg.slowSpeedPercent - 10.0f) / 80.0f);
    curSliderY += 40.0f;

    drawToggle("First-Touch Trap Cushion:", cfg.firstTouchCushionEnabled, col1X + 16.0f, curSliderY);
    curSliderY += 34.0f;
    drawToggle("Fast-Fake Oscillator [P]:", cfg.fastFakeEnabled, col1X + 16.0f, curSliderY);
    curSliderY += 34.0f;
    if (cfg.fastFakeEnabled) {
        std::string ffStr = (cfg.fastFakeSpeedTicks <= 1) ? "1 tick (Hyper)" : (std::to_string(cfg.fastFakeSpeedTicks) + " ticks");
        drawSlider("Fast-Fake Speed [P]:", ffStr, col1X + 16.0f, curSliderY, colW - 32.0f, (cfg.fastFakeSpeedTicks - 1.0f) / 9.0f);
        curSliderY += 40.0f;
    }

    fontSmall.drawText(renderer, "Holding [M] modulates movement ticks for precision micro-touches.", col1X + 16.0f, setTop + setH - 24.0f, {120, 145, 175, 220});

    float col2X = col1X + colW + 10.0f;
    GameRenderer::drawRoundedRect(renderer, col2X, setTop, colW, setH, 8.0f, {15, 20, 30, 240}, {38, 50, 72, 220}, 1.2f);
    fontLarge.drawTextShadow(renderer, "🎨 Graphics & Display", col2X + 16.0f, setTop + 14.0f, {251, 191, 36, 255});
    fontSmall.drawText(renderer, "(Changeable in game via F3)", col2X + colW - 16.0f, setTop + 18.0f, {150, 175, 205, 220}, TextAlign::Right);

    drawToggle("VSync (Smooth Sync):", cfg.vsync, col2X + 16.0f, setTop + 54.0f);

    fontSmall.drawText(renderer, "Target FPS:", col2X + 16.0f, setTop + 95.0f, {180, 200, 225, 255});
    int fpsCaps[] = {60, 120, 144, 240, 500};
    const char* fpsLabels[] = {"60", "120", "144", "240", "Max"};
    for (int i = 0; i < 5; ++i) {
        float cx = col2X + 16.0f + i * ((colW - 32.0f) / 5.0f);
        float cw = (colW - 36.0f) / 5.0f;
        bool sel = (cfg.fpsCap == fpsCaps[i]);
        GameRenderer::drawRoundedRect(renderer, cx, setTop + 115.0f, cw, 26.0f, 4.0f,
                                      sel ? SDL_Color{37, 99, 235, 255} : SDL_Color{20, 28, 42, 220},
                                      sel ? SDL_Color{147, 197, 253, 240} : SDL_Color{48, 62, 86, 180}, 1.0f);
        fontSmall.drawText(renderer, fpsLabels[i], cx + cw * 0.5f, setTop + 120.0f, {255, 255, 255, 255}, TextAlign::Center);
    }

    fontSmall.drawText(renderer, "Ball Trail:", col2X + 16.0f, setTop + 155.0f, {180, 200, 225, 255});
    const char* trailLabels[] = {"Off", "Auto", "Neon", "Fire", "White", "Ghost"};
    for (int i = 0; i < 6; ++i) {
        float cx = col2X + 16.0f + i * ((colW - 32.0f) / 6.0f);
        float cw = (colW - 36.0f) / 6.0f;
        bool sel = (cfg.ballTrailStyle == i);
        GameRenderer::drawRoundedRect(renderer, cx, setTop + 175.0f, cw, 26.0f, 4.0f,
                                      sel ? SDL_Color{124, 58, 237, 255} : SDL_Color{20, 28, 42, 220},
                                      sel ? SDL_Color{196, 181, 253, 240} : SDL_Color{48, 62, 86, 180}, 1.0f);
        fontSmall.drawText(renderer, trailLabels[i], cx + cw * 0.5f, setTop + 180.0f, {255, 255, 255, 255}, TextAlign::Center);
    }

    fontSmall.drawText(renderer, "Pitch Theme:", col2X + 16.0f, setTop + 215.0f, {180, 200, 225, 255});
    const char* themeLabels[] = {"Grass", "Check", "Ice", "Neon", "Street", "Gold"};
    for (int i = 0; i < 6; ++i) {
        float cx = col2X + 16.0f + (i % 3) * ((colW - 32.0f) / 3.0f);
        float cy = setTop + 235.0f + (i / 3) * 32.0f;
        float cw = (colW - 36.0f) / 3.0f;
        bool sel = (cfg.pitchTheme == i);
        GameRenderer::drawRoundedRect(renderer, cx, cy, cw, 26.0f, 4.0f,
                                      sel ? SDL_Color{5, 150, 105, 255} : SDL_Color{20, 28, 42, 220},
                                      sel ? SDL_Color{110, 231, 183, 240} : SDL_Color{48, 62, 86, 180}, 1.0f);
        fontSmall.drawText(renderer, themeLabels[i], cx + cw * 0.5f, cy + 5.0f, {255, 255, 255, 255}, TextAlign::Center);
    }

    float col3X = col2X + colW + 10.0f;
    GameRenderer::drawRoundedRect(renderer, col3X, setTop, colW, setH, 8.0f, {15, 20, 30, 240}, {38, 50, 72, 220}, 1.2f);
    fontLarge.drawTextShadow(renderer, "🔊 Audio & Keybinds", col3X + 16.0f, setTop + 14.0f, {168, 85, 247, 255});

    drawToggle("Master Sound:", cfg.masterVolume > 0.05f, col3X + 16.0f, setTop + 54.0f);
    drawToggle("Kick Sound:", cfg.soundKick, col3X + 16.0f, setTop + 90.0f);
    drawToggle("Goal Horn:", cfg.soundGoal, col3X + 16.0f, setTop + 126.0f);
    drawToggle("Chat Notification:", cfg.soundChat, col3X + 16.0f, setTop + 162.0f);

    fontSmall.drawText(renderer, "Superpower Keybinds:", col3X + 16.0f, setTop + 205.0f, {251, 191, 36, 255});
    fontSmall.drawText(renderer, "• Shoot: Space / X", col3X + 24.0f, setTop + 225.0f, {200, 215, 235, 255});
    fontSmall.drawText(renderer, "• Slide Superpower: [C]", col3X + 24.0f, setTop + 245.0f, {200, 215, 235, 255});
    fontSmall.drawText(renderer, "• Sprint Superpower: [V] or [E]", col3X + 24.0f, setTop + 265.0f, {200, 215, 235, 255});
    fontSmall.drawText(renderer, "• Fake Shot: [F]  |  Pitch Rotate: [R]", col3X + 24.0f, setTop + 285.0f, {200, 215, 235, 255});

    GameRenderer::drawRoundedRect(renderer, screenW - 240.0f, screenH - 65.0f, 200.0f, 32.0f, 5.0f, {127, 29, 29, 255}, {239, 68, 68, 220}, 1.2f);
    fontSmall.drawTextShadow(renderer, "↺ Reset to Defaults", screenW - 140.0f, screenH - 57.0f, {255, 255, 255, 255}, TextAlign::Center);
}

void RoomBrowser::renderFlagModal(SDL_Renderer* renderer, int screenW, int screenH) {

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
    SDL_Rect fullR{0, 0, screenW, screenH};
    SDL_RenderFillRect(renderer, &fullR);

    float mw = 520.0f;
    float mh = 480.0f;
    float mx = (screenW - mw) * 0.5f;
    float my = (screenH - mh) * 0.5f;

    GameRenderer::drawRoundedRect(renderer, mx, my, mw, mh, 10.0f, {16, 22, 34, 255}, {56, 189, 248, 220}, 1.6f);

    fontLarge.drawTextShadow(renderer, "Select Country Flag", mx + 20.0f, my + 14.0f, {56, 189, 248, 255});
    fontSmall.drawText(renderer, "✕", mx + mw - 28.0f, my + 14.0f, {200, 215, 235, 255});

    bool activeFs = (activeField == Field_FlagSearch);
    GameRenderer::drawRoundedRect(renderer, mx + 20.0f, my + 50.0f, mw - 40.0f, 32.0f, 5.0f, {10, 14, 22, 255},
                                  activeFs ? SDL_Color{56, 189, 248, 255} : SDL_Color{48, 62, 86, 200},
                                  activeFs ? 1.5f : 1.0f);
    std::string qDisp = flagSearchInput.empty() ? "Search country or 2-letter code (e.g. Netherlands, FR, TR)..." : flagSearchInput;
    SDL_Color qc = flagSearchInput.empty() ? SDL_Color{100, 120, 145, 200} : SDL_Color{255, 255, 255, 255};
    fontSmall.drawText(renderer, qDisp + (activeFs ? "_" : ""), mx + 30.0f, my + 59.0f, qc);

    float listY = my + 94.0f;
    float itemH = 34.0f;
    int maxItems = static_cast<int>((mh - 110.0f) / itemH);

    for (int i = 0; i < maxItems && (i + flagScrollOffset) < (int)filteredCountries.size(); ++i) {
        int idx = i + flagScrollOffset;
        const auto& c = filteredCountries[idx];
        bool isCurrent = (c.code == playerFlagInput);

        float iy = listY + i * itemH;
        SDL_Color bg = isCurrent ? SDL_Color{35, 75, 135, 255} : ((idx % 2 == 0) ? SDL_Color{22, 28, 42, 240} : SDL_Color{18, 23, 35, 240});
        SDL_Color br = isCurrent ? SDL_Color{56, 189, 248, 220} : SDL_Color{35, 45, 65, 180};
        GameRenderer::drawRoundedRect(renderer, mx + 20.0f, iy, mw - 40.0f, itemH - 3.0f, 4.0f, bg, br, isCurrent ? 1.4f : 0.0f);

        GameRenderer::drawRoundedRect(renderer, mx + 28.0f, iy + 4.0f, 44.0f, 22.0f, 3.0f, {30, 42, 64, 255}, {60, 85, 125, 200}, 1.0f);
        fontSmall.drawText(renderer, c.code, mx + 50.0f, iy + 8.0f, {220, 240, 255, 255}, TextAlign::Center);

        font.drawTextShadow(renderer, c.name, mx + 85.0f, iy + 7.0f, {245, 248, 255, 255});

        if (isCurrent) {
            fontSmall.drawText(renderer, "✔ Selected", mx + mw - 45.0f, iy + 8.0f, {34, 197, 94, 255}, TextAlign::Right);
        }
    }
}

void RoomBrowser::renderToast(SDL_Renderer* renderer, int screenW, int) {
    if (toastTimer <= 0.0f || toastMessage.empty()) return;

    float tw = 440.0f;
    float th = 34.0f;
    float tx = (screenW - tw) * 0.5f;
    float ty = 14.0f;

    SDL_Color bg = toastIsError ? SDL_Color{127, 29, 29, 245} : SDL_Color{15, 60, 35, 245};
    SDL_Color br = toastIsError ? SDL_Color{239, 68, 68, 230} : SDL_Color{34, 197, 94, 230};
    GameRenderer::drawRoundedRect(renderer, tx, ty, tw, th, 6.0f, bg, br, 1.4f);
    font.drawTextShadow(renderer, toastMessage, tx + tw * 0.5f, ty + 7.0f, {255, 255, 255, 255}, TextAlign::Center);
}

void RoomBrowser::render(SDL_Renderer* renderer, int screenW, int screenH) {
    SDL_SetRenderDrawColor(renderer, 12, 16, 24, 255);
    SDL_RenderClear(renderer);

    renderHeader(renderer, screenW, screenH);
    renderProfileBar(renderer, screenW, screenH);

    if (currentTab == LauncherTab::Rooms) {
        renderRoomsTab(renderer, screenW, screenH);
    } else if (currentTab == LauncherTab::Auth) {
        renderAuthTab(renderer, screenW, screenH);
    } else if (currentTab == LauncherTab::Settings) {
        renderSettingsTab(renderer, screenW, screenH);
    } else if (currentTab == LauncherTab::Shortcuts) {
        renderShortcutsTab(renderer, screenW, screenH);
    }

    if (showFlagPicker) {
        renderFlagModal(renderer, screenW, screenH);
    }

    renderToast(renderer, screenW, screenH);
}

void RoomBrowser::renderShortcutsTab(SDL_Renderer* renderer, int screenW, int screenH) {
    auto& cfg = AppConfig::get();
    float setTop = 72.0f;
    float setH = screenH - setTop - 24.0f;

    GameRenderer::drawRoundedRect(renderer, 24.0f, setTop, screenW - 48.0f, setH, 8.0f, {15, 20, 30, 240}, {38, 50, 72, 220}, 1.2f);
    fontLarge.drawTextShadow(renderer, "⌨️ Keyboard Shortcuts & Superpowers", 44.0f, setTop + 16.0f, {56, 189, 248, 255});
    fontSmall.drawText(renderer, "Click on any key binding to rebind it. The client listens for the next keypress.", 44.0f, setTop + 42.0f, {150, 175, 205, 240});

    struct KeyItem {
        std::string label;
        std::string desc;
        int keycode;
        int idx;
    };
    KeyItem items[] = {
        {"Slide Superpower (RS)", "Automatically executes 👟 slide feint upon server timing", cfg.keySlide, 0},
        {"Sprint Superpower (RS)", "Automatically executes ⚡ sprint boost upon server timing", cfg.keySprint, 1},
        {"Sprint Superpower Alt", "Alternative key for ⚡ sprint boost", cfg.keySprintAlt, 2},
        {"Hold Auto-Click Mode", "Repeatedly shoots everywhere (even directly on the ball)", cfg.keyRealShot, 3},
        {"Hold Slow Movement", "Slows down movement via tick duty-cycle for micro positioning", cfg.keySlowMove, 4},
        {"Toggle Auto-Fake Shot", "Toggles auto-fake shot assistant on/off", cfg.keyFakeToggle, 5}
    };

    float startY = setTop + 72.0f;
    float rowH = 46.0f;

    for (int i = 0; i < 6; ++i) {
        float ry = startY + i * rowH;
        bool isRebinding = (rebindingKey == items[i].idx);

        GameRenderer::drawRoundedRect(renderer, 40.0f, ry, screenW - 80.0f, 38.0f, 6.0f,
                                      isRebinding ? SDL_Color{35, 75, 135, 255} : ((i % 2 == 0) ? SDL_Color{20, 27, 40, 255} : SDL_Color{16, 22, 33, 255}),
                                      isRebinding ? SDL_Color{56, 189, 248, 240} : SDL_Color{35, 48, 68, 180},
                                      isRebinding ? 1.5f : 1.0f);

        font.drawTextShadow(renderer, items[i].label, 56.0f, ry + 10.0f, {245, 248, 255, 255});
        fontSmall.drawText(renderer, items[i].desc, 320.0f, ry + 11.0f, {140, 165, 195, 220});

        float btnW = 110.0f;
        float btnX = screenW - 200.0f;
        std::string keyName = isRebinding ? "[Press Key...]" : (items[i].keycode == 0 ? "[None]" : ("[" + std::string(SDL_GetKeyName(items[i].keycode)) + "]"));
        SDL_Color bgCol = isRebinding ? SDL_Color{234, 88, 12, 255} : (items[i].keycode == 0 ? SDL_Color{30, 36, 48, 255} : SDL_Color{30, 48, 76, 255});
        SDL_Color brCol = isRebinding ? SDL_Color{251, 146, 60, 255} : (items[i].keycode == 0 ? SDL_Color{55, 68, 88, 200} : SDL_Color{56, 189, 248, 220});
        GameRenderer::drawRoundedRect(renderer, btnX, ry + 4.0f, btnW, 30.0f, 5.0f, bgCol, brCol, 1.2f);
        fontSmall.drawTextShadow(renderer, keyName, btnX + btnW * 0.5f, ry + 10.0f, {255, 255, 255, 255}, TextAlign::Center);

        float clrX = screenW - 82.0f;
        GameRenderer::drawRoundedRect(renderer, clrX, ry + 4.0f, 38.0f, 30.0f, 5.0f, {32, 20, 26, 255}, {185, 28, 28, 180}, 1.0f);
        fontSmall.drawText(renderer, "✕", clrX + 19.0f, ry + 10.0f, {248, 113, 113, 255}, TextAlign::Center);
    }

    float boxY = startY + 6 * rowH + 16.0f;
    GameRenderer::drawRoundedRect(renderer, 40.0f, boxY, screenW - 80.0f, 80.0f, 6.0f, {14, 18, 28, 220}, {40, 52, 72, 180}, 1.0f);
    fontSmall.drawText(renderer, "Standard Match Controls:", 56.0f, boxY + 12.0f, {251, 191, 36, 255});
    fontSmall.drawText(renderer, "• Move: [W] [A] [S] [D] / Arrow Keys", 56.0f, boxY + 34.0f, {200, 215, 235, 255});
    fontSmall.drawText(renderer, "• Kick / Shoot: [Space] / [X] / [Shift] / [Ctrl]", 56.0f, boxY + 54.0f, {200, 215, 235, 255});
    fontSmall.drawText(renderer, "• Match Overlays: [F1] Esports  |  [F2] Tactical Assist  |  [F3] Visuals & Themes  |  [R] Rotate Pitch", 360.0f, boxY + 34.0f, {200, 215, 235, 255});
    fontSmall.drawText(renderer, "• Chat: [Enter] / [T]  |  Menu: [Esc] / [Tab]", 360.0f, boxY + 54.0f, {200, 215, 235, 255});

    GameRenderer::drawRoundedRect(renderer, screenW - 240.0f, screenH - 65.0f, 200.0f, 32.0f, 5.0f, {127, 29, 29, 255}, {239, 68, 68, 220}, 1.2f);
    fontSmall.drawTextShadow(renderer, "↺ Reset Shortcuts", screenW - 140.0f, screenH - 57.0f, {255, 255, 255, 255}, TextAlign::Center);
}

}
