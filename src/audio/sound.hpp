#pragma once

#include <SDL2/SDL.h>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <mutex>

namespace haxball {

enum class SoundEffect {
    Kick,
    Goal,
    Chat,
    Join,
    Leave
};

struct DecodedAudio {
    std::vector<int16_t> samples;
    int channels = 2;
    int sampleRate = 44100;
};

class SoundManager {
public:
    SoundManager();
    ~SoundManager();

    bool init();
    void play(SoundEffect effect, float volume = 0.8f);
    void setMasterVolume(float vol) { masterVolume = vol; }

private:
    struct ActiveChannel {
        const int16_t* samples = nullptr;
        size_t totalSamples = 0;
        size_t cursor = 0;
        float volume = 1.0f;
    };

    static void audioCallback(void* userdata, Uint8* stream, int len);

    SDL_AudioDeviceID deviceId = 0;
    float masterVolume = 1.0f;
    std::mutex audioMutex;
    std::vector<ActiveChannel> activeChannels;

    DecodedAudio kickSound;
    DecodedAudio goalSound;
    DecodedAudio chatSound;
    DecodedAudio joinSound;
    DecodedAudio leaveSound;

    bool loadOgg(const std::string& path, DecodedAudio& outAudio);
};

}
