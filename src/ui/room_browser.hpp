#pragma once

#include <SDL2/SDL.h>
#include "../net/room_list.hpp"
#include "../render/font.hpp"
#include "../config/app_config.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

namespace haxball {

enum class BrowserAction {
    None,
    JoinSelectedRoom,
    JoinDirectId,
    StartSandbox,
    StartPenaltyPractice,
    RefreshRooms,
    SettingsChanged
};

enum class LauncherTab {
    Rooms,
    Auth,
    Settings,
    Shortcuts
};

enum class QuickFilter {
    All,
    LDC,
    OpenOnly,
    HasSlots
};

struct CountryEntry {
    std::string code;
    std::string name;
};

class RoomBrowser {
public:
    RoomBrowser();
    ~RoomBrowser();

    bool init(SDL_Renderer* renderer, const std::string& fontPath);
    void refresh();
    void update(float dt);
    void render(SDL_Renderer* renderer, int screenW, int screenH);
    BrowserAction handleEvent(const SDL_Event& event, int screenW, int screenH);

    std::string getSelectedRoomId() const;
    std::string getDirectRoomId() const { return directIdInput; }
    std::string getRoomPassword() const { return passwordInput; }
    std::string getPlayerName() const { return playerNameInput; }
    std::string getPlayerAvatar() const { return playerAvatarInput; }
    void setPlayerAvatar(const std::string& av) { playerAvatarInput = av; }
    std::string getPlayerFlag() const { return playerFlagInput; }
    std::string getActiveAuthKey() const { return AppConfig::get().getActiveAuthKey(); }

    void syncConfigToUI();
    void syncUIToConfig();

private:
    void rebuildFilteredList();
    void rebuildFilteredCountries();
    void showToast(const std::string& msg, bool isError = false, float duration = 2.5f);

    std::vector<RoomInfo> allRooms;
    std::vector<RoomInfo> filteredRooms;
    mutable std::mutex roomsMutex;

    int selectedIndex = -1;
    int scrollOffset = 0;

    LauncherTab currentTab = LauncherTab::Rooms;
    QuickFilter quickFilter = QuickFilter::All;

    std::string playerNameInput = "ProPlayer";
    std::string playerAvatarInput = "⚽";
    std::string playerFlagInput = "nl";
    std::string searchQueryInput = "";
    std::string directIdInput = "";
    std::string passwordInput = "";

    std::string newKeyLabelInput = "";
    std::string newKeyStringInput = "";
    bool showFullActiveKey = false;
    int authScrollOffset = 0;

    bool showFlagPicker = false;
    std::string flagSearchInput = "";
    int flagScrollOffset = 0;
    std::vector<CountryEntry> allCountries;
    std::vector<CountryEntry> filteredCountries;

    std::string toastMessage = "";
    float toastTimer = 0.0f;
    bool toastIsError = false;

    int nameStyleIndex = 0;
    void cycleNameStyle();

    int draggingSlider = -1;

    int rebindingKey = -1;

    enum ActiveField {
        Field_None,
        Field_Name,
        Field_Avatar,
        Field_Flag,
        Field_Search,
        Field_DirectId,
        Field_Password,
        Field_NewKeyLabel,
        Field_NewKeyString,
        Field_FlagSearch
    };
    ActiveField activeField = Field_None;

    FontRenderer fontSmall;
    FontRenderer font;
    FontRenderer fontLarge;
    FontRenderer fontTitle;
    std::atomic<bool> isLoading{false};

    void renderHeader(SDL_Renderer* renderer, int screenW, int screenH);
    void renderProfileBar(SDL_Renderer* renderer, int screenW, int screenH);
    void renderRoomsTab(SDL_Renderer* renderer, int screenW, int screenH);
    void renderAuthTab(SDL_Renderer* renderer, int screenW, int screenH);
    void renderSettingsTab(SDL_Renderer* renderer, int screenW, int screenH);
    void renderShortcutsTab(SDL_Renderer* renderer, int screenW, int screenH);
    void renderFlagModal(SDL_Renderer* renderer, int screenW, int screenH);
    void renderToast(SDL_Renderer* renderer, int screenW, int screenH);
};

}
