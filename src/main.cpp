#include "app.hpp"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << " Starting Haxball C++ Client            " << std::endl;
    std::cout << " Zero JavaScript - 100% Native WebRTC   " << std::endl;
    std::cout << "========================================" << std::endl;

    std::string autoRoom;
    bool autoSandbox = false;
    bool testMode = false;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--sandbox" || arg == "-s") {
            autoSandbox = true;
        } else if (arg == "--test" || arg == "-t") {
            testMode = true;
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (arg == "--room" || arg == "-r") {
            if (i + 1 < argc) autoRoom = argv[++i];
        } else if (arg.rfind("-", 0) != 0) {
            autoRoom = arg;
        }
    }

    if (verbose) {
        std::cout << "[Info] Verbose WebRTC connection telemetry enabled." << std::endl;
    }

    haxball::App app;
    if (!app.init(autoRoom, autoSandbox, testMode, verbose)) {
        std::cerr << "Failed to initialize application." << std::endl;
        return 1;
    }

    app.run();
    return 0;
}
