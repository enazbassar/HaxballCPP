cmake_minimum_required(VERSION 3.16)
project(haxball_stress CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

include_directories(
  ${ROOT}/include
  ${ROOT}/include/rtc
  ${ROOT}/include/nlohmann
  ${ROOT}/src
  ${ROOT}/src/net
)
if(APPLE)
  if(EXISTS "/opt/homebrew")
    include_directories("/opt/homebrew/include" "/opt/homebrew/opt/openssl@3/include")
    link_directories("/opt/homebrew/lib" "/opt/homebrew/opt/openssl@3/lib")
  elseif(EXISTS "/usr/local")
    include_directories("/usr/local/include" "/usr/local/opt/openssl@3/include")
    link_directories("/usr/local/lib" "/usr/local/opt/openssl@3/lib")
  endif()
else()
  link_directories(${ROOT}/lib)
endif()

add_executable(haxball_stress
  stress.cpp
  ${ROOT}/src/net/haxball_client.cpp
  ${ROOT}/src/physics/world.cpp
  ${ROOT}/src/physics/stadium.cpp
)

if(APPLE)
  find_package(OpenSSL REQUIRED)

  if(EXISTS "${ROOT}/mac_deps/libdatachannel.dylib")
    target_link_libraries(haxball_stress PRIVATE
      ${ROOT}/mac_deps/libdatachannel.dylib
    )
  else()
    find_package(LibDataChannel REQUIRED)
    target_link_libraries(haxball_stress PRIVATE datachannel)
  endif()

  target_include_directories(haxball_stress PRIVATE ${OPENSSL_INCLUDE_DIR})
  target_link_libraries(haxball_stress PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    z
    pthread
  )

  set_target_properties(haxball_stress PROPERTIES
    BUILD_RPATH "${ROOT}/mac_deps"
    INSTALL_RPATH "${ROOT}/mac_deps"
  )
else()
  target_link_libraries(haxball_stress PRIVATE
    ${ROOT}/lib/libdatachannel-static.a
    ${ROOT}/lib/libusrsctp.a
    ${ROOT}/lib/libjuice-static.a
    ssl crypto z pthread dl m
  )

  set_target_properties(haxball_stress PROPERTIES
    BUILD_RPATH "${ROOT}/lib"
    INSTALL_RPATH "${ROOT}/lib"
  )
endif()
