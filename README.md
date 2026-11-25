# HaxballCPP — Native C++ Haxball Client

A **100% native C++17** Haxball client built with **SDL2 (`SDL_RenderGeometry` batched vector & font rendering)**, **Dear ImGui**, **libdatachannel (WebRTC + SCTP)**, **OpenSSL**, **zlib**, and **stb_truetype**. Zero JavaScript or Node runtime involved.

---

## Key Features

### 1. Authoritative Lockstep Netcode & Extrapolation
- **Full 27-Action Haxball Wire Protocol (`0..26`)**: Supports all standard and headless host actions (`SetDiscProperties`, `SendAnnouncement`, `SetTeamColors`, `SetHeadlessAvatar`, `ReorderPlayers`, `SetKickRateLimit`, `Ping`, etc.).
- **Lock-Free Client-Side Extrapolation (`-200 ms` to `+500 ms`)**: Reconstructs both the predicted render world and a `0 ms` authoritative real-state world for accurate timing and visuals.
- **Anti-OPMode `[2]`**: Renders opponents smoothly at `0 ms` while keeping your own and the ball's full extrapolation.
- **Anti-FakeShot `[3]`**: Cross-checks speculative kicks against the `0 ms` real-state horizon to suppress opponent fake-shot flicker.
- **Fake Ping (`/fakeping`)**: Configurable ping reporter modes (`Off`, `Static`, `Flash`, `Random`, `Wave`, `Count`) with adjustable cycle speed and zero real added latency.
- **Ping Delay / Handicap (`0 ms` to `250 ms`)**, **FPS Limit (`60` to `Uncapped`)**, **VSync**, **NetGraph `[F8]`**, and **Desync Ghost**.

### 2. 4-Tab In-Game Menu (`F1` / `F2` / `F3` / `Insert`)
- **Network `[F1]`**: Extrapolation slider + presets, Ping Delay, FPS Limit, VSync, NetGraph, Desync Ghost, and Fake Ping controls.
- **Assist `[F2]`**:
  - **Goal & GK**: Stealth **Auto-Goal `[0]`** (with Accuracy slider `30–100%`), **10 Men Futsal (OFF IN RS) `[1]`** with **My Goal (Exclude)** color picker for multi-goal/FFA stadiums, **Auto-Save `[6]`**, **Smart GK `[7]`**, **Penalty GK `[8]`**, **GK Danger Cone**, and **Other Cones**.
  - **Feints & Filters**: **Fake Shot `[F]`** (with cooldown slider), **Extend Fake `[5]`** (`Auto Safe` / `+1..+15 Ticks`), **Anti-OPMode `[2]`**, **Anti-FakeShot `[3]`**, and **1st-Touch Soft `[4]`**.
  - **Hold-to-Use Tech** *(always ready when held)*: **Fast-Fake `[P]`** (`1..10 Ticks`), **Auto-Click `[N]`** (`0..1000 ms`), **Auto-Shaker `[B]`** (`1..12 Ticks`), **Moon Orbit `[O]`** (`Surface` / `+1..+15 Gap`), and **Slow Speed `[M]`** (`10–90%`).
  - **Lines & Extras**: **Live Aim Line** (distance & opacity sliders), **Ball Roll Line** (lookahead slider), **Real Soccer Helper** (`Slide [C]` / `Sprint [V]`), **Fake Typing `[J]`**, and **Type Strobe `[U]`** (`5–500 ms`).
- **Camera `[F3]`**: Camera modes (`Adaptive`, `Player`, `Ball`, `Full Pitch`), **Field Rotation `[R]`** (`0°`, `90° Up`, `180°`, `270° Dn`, `Auto Up` with automatic `WASD` remapping), Zoom, Smoothness, 6 Pitch Themes, 6 Ball Trail styles, disc/halo toggles, and a **14-emoji full-color Quick Avatar bar**.
- **Menu**: Dark/Light theme, 5 accent colors, menu opacity, and `x88` HUD settings (enable, shortcuts, active-only, corner, opacity, and HUD size `70–160%`).

### 3. Classic `x88 haxball` Plain-Text HUD & Inline Editing
- Always-on plain-text status overlay displaying `x88 haxball | <Room Name> | <FPS> FPS | <Ping> ms` and live status of active toggles, hold keys, and extrapolation.
- **Inline Keyboard Editing**: Press **`Ctrl` or `Alt` + a feature's shortcut** (e.g. `Alt+N` for Auto-Click, `Alt+P` for Fast-Fake, `Alt+0` for Auto-Goal Accuracy, `Alt+5` for Extend Fake, `Alt+E` for Extrap) to highlight it on `x88`, then adjust its value live with **`Left` / `Right` Arrow Keys** (hold **`Shift`** for larger steps). Use **`Up` / `Down`** to move between rows and **`Esc`** to finish.

### 4. Native Physics, Emoji Engine & Room Browser
- **1:1 Haxball Physics Engine**: All 10 built-in stadiums plus full custom `.hbs` stadium support (curved arcs, collision bias, rigid/spring joints, custom player physics, and spawn points).
- **Full-Color Emoji Engine**: Decodes embedded PNG glyphs directly from `NotoColorEmoji.ttf` (`cmap`/`CBLC`/`CBDT`) into hardware-accelerated textures across player discs, chat, and the Room Browser.
- **Room Browser & Sandbox**: Search/filter live rooms by name or country, manage ECDSA auth profiles, join by Room ID/URL, or launch Offline Sandbox / Penalty Practice.

---

## Controls & Hotkeys

| Key | Action |
|---|---|
| **W / A / S / D** | Move (auto-remaps with Field Rotation, SOCD last-input priority) |
| **Space / X / Shift / Ctrl** | Kick |
| **F1 / F2 / F3 / Insert** | Toggle Network / Assist / Camera / Client Menu |
| **Tab / Esc** | Toggle Room Lobby (or exit `x88` edit mode / close menu) |
| **Enter / T** | Open Chat Input / Send Message |
| **0 / 1** | Toggle Auto-Goal / 10 Men Futsal (OFF IN RS) |
| **2 / 3 / 4 / 5** | Toggle Anti-OPMode / Anti-FakeShot / 1st-Touch Soft / Extend Fake |
| **6 / 7 / 8** | Toggle Auto-Save / Smart GK / Penalty GK |
| **F** | Toggle Fake Shot |
| **P** *(hold)* / **N** *(hold)* | Fast-Fake / Auto-Click |
| **B** *(hold)* / **O** *(hold)* / **M** *(hold)* | Auto-Shaker / Moon Orbit / Slow Move |
| **J / U** | Toggle Fake Typing / Type Strobe |
| **Ctrl / Alt + Shortcut** + **Arrows** | Select & adjust `x88` HUD slider values (`Shift` = larger step, `Esc` = done) |
| **R** | Cycle Field Rotation |
| **`,` / `.` or `[` / `]`** | Adjust Extrapolation (`±5 ms`, or `±25 ms` with `Shift`) |
| **Mouse Wheel / `=` / `-`** | Adjust Camera Zoom |
| **Shift + P** | Pause / Resume Match (Admin / Sandbox) |
| **F4..F9** | Toggle Team Colors (`F4`), IDs (`F5`), Avatars (`F6`), Halo (`F7`), NetGraph (`F8`), Invisible Walls (`F9`) |

### Chat Commands
- `/extrap <ms>` (or `/extrapolation <ms>`) — Set extrapolation (`-200..500 ms`)
- `/handicap <ms>` — Set ping delay (`0..300 ms`)
- `/avatar <emoji>` — Set live player avatar
- `/antiop` — Toggle Anti-OPMode
- `/fakeping <ms | off | flash | rand | wave | count | speed <ms>>` — Configure Fake Ping
- `/rq [msg]` (or `/ragequit [msg]`) — Send farewell message and leave room

---

## Build & Run

### Linux (CMake)
```bash
cd HaxballCPP
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Windows (MSYS2 MINGW64)
1. Install [MSYS2](https://www.msys2.org/) and open the **MSYS2 MINGW64** shell.
2. Install dependencies and build:
```bash
pacman -S --needed git mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja \
  mingw-w64-x86_64-SDL2 mingw-w64-x86_64-openssl mingw-w64-x86_64-zlib

cd HaxballCPP
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/haxball_client.exe
```

### macOS
```bash
./build_mac.sh
```

### Run
```bash
# Launch graphical client with Room Browser
./build/haxball_client

# Join a room directly by ID
./build/haxball_client --room <ROOM_ID>

# Launch directly into Offline Sandbox
./build/haxball_client --sandbox
```
