cmake_minimum_required(VERSION 3.16)
project(haxball_client C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/SDL2
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rtc
    ${CMAKE_CURRENT_SOURCE_DIR}/include/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

# On macOS with Homebrew:
if(APPLE)
    if(EXISTS "/opt/homebrew") # Apple Silicon (M1/M2/M3/M4)
        include_directories("/opt/homebrew/include" "/opt/homebrew/opt/openssl@3/include" "/opt/homebrew/opt/sdl2/include")
        link_directories("/opt/homebrew/lib" "/opt/homebrew/opt/openssl@3/lib" "/opt/homebrew/opt/sdl2/lib")
    elseif(EXISTS "/usr/local") # Intel Mac
        include_directories("/usr/local/include" "/usr/local/opt/openssl@3/include" "/usr/local/opt/sdl2/include")
        link_directories("/usr/local/lib" "/usr/local/opt/openssl@3/lib" "/usr/local/opt/sdl2/lib")
    endif()
else()
    link_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/lib
    )
endif()

set(SOURCES
    src/main.cpp
    src/app.cpp
    src/ui/room_browser.cpp
    src/ui/cockpit_menu.cpp
    src/render/font.cpp
    src/render/renderer.cpp
    src/audio/sound.cpp
    src/physics/stadium.cpp
    src/physics/world.cpp
    src/assist/tactical_assistant.cpp
    src/net/room_list.cpp
    src/net/haxball_client.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)

add_executable(haxball_client ${SOURCES})

if(APPLE)
    find_package(OpenSSL REQUIRED)

    # Check for local mac_deps or Homebrew
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mac_deps/SDL2.framework")
        target_include_directories(haxball_client PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/mac_deps/SDL2.framework/Headers
        )
        target_link_libraries(haxball_client PRIVATE
            "-F${CMAKE_CURRENT_SOURCE_DIR}/mac_deps -framework SDL2"
        )
    else()
        find_package(SDL2 REQUIRED)
        target_include_directories(haxball_client PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(haxball_client PRIVATE SDL2::SDL2)
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mac_deps/libdatachannel.dylib")
        target_link_libraries(haxball_client PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/mac_deps/libdatachannel.dylib
        )
    else()
        find_package(LibDataChannel REQUIRED)
        target_link_libraries(haxball_client PRIVATE datachannel)
    endif()

    target_include_directories(haxball_client PRIVATE ${OPENSSL_INCLUDE_DIR})
    target_link_libraries(haxball_client PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
        z
        pthread
    )

    set_target_properties(haxball_client PROPERTIES
        BUILD_RPATH "${CMAKE_CURRENT_SOURCE_DIR}/mac_deps"
        INSTALL_RPATH "${CMAKE_CURRENT_SOURCE_DIR}/mac_deps"
    )
elseif(WIN32)
    find_package(SDL2 REQUIRED)
    find_package(OpenSSL REQUIRED)
    find_package(ZLIB REQUIRED)
    find_package(LibDataChannel QUIET)

    if(NOT LibDataChannel_FOUND)
        set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
        set(NO_MEDIA ON CACHE BOOL "" FORCE)
        set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
        set(NO_TESTS ON CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            libdatachannel
            GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
            GIT_TAG v0.22.4
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(libdatachannel)
    endif()

    target_compile_definitions(haxball_client PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _USE_MATH_DEFINES
        _WIN32_WINNT=0x0601
    )

    if(TARGET SDL2::SDL2main)
        target_link_libraries(haxball_client PRIVATE SDL2::SDL2main)
    endif()
    if(TARGET SDL2::SDL2)
        target_link_libraries(haxball_client PRIVATE SDL2::SDL2)
    else()
        target_include_directories(haxball_client PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(haxball_client PRIVATE ${SDL2_LIBRARIES})
    endif()

    if(TARGET LibDataChannel::LibDataChannelStatic)
        target_link_libraries(haxball_client PRIVATE LibDataChannel::LibDataChannelStatic)
    elseif(TARGET LibDataChannel::LibDataChannel)
        target_link_libraries(haxball_client PRIVATE LibDataChannel::LibDataChannel)
    else()
        target_link_libraries(haxball_client PRIVATE datachannel)
    endif()

    target_link_libraries(haxball_client PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
        ZLIB::ZLIB
        ws2_32
        crypt32
        iphlpapi
        bcrypt
        winmm
        imm32
        version
        setupapi
    )

    if(MINGW)
        target_link_options(haxball_client PRIVATE -static-libgcc -static-libstdc++)
    endif()
else()
    target_link_libraries(haxball_client PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libdatachannel-static.a
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libusrsctp.a
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libjuice-static.a
        SDL2
        ssl
        crypto
        z
        pthread
        dl
        m
    )
    # Set rpath so it can find lib/libSDL2.so at runtime on Linux
    set_target_properties(haxball_client PROPERTIES
        BUILD_RPATH "${CMAKE_CURRENT_SOURCE_DIR}/lib;/usr/lib/x86_64-linux-gnu"
        INSTALL_RPATH "${CMAKE_CURRENT_SOURCE_DIR}/lib;/usr/lib/x86_64-linux-gnu"
    )
endif()
