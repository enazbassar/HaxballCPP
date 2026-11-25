#include "sound.hpp"
#include <iostream>
#include <algorithm>
#include <cstdlib>

extern "C" {
#include <stb/stb_vorbis.c>
}

namespace haxball {

SoundManager::SoundManager() {}

SoundManager::~SoundManager() {
    if (deviceId > 0) {
        SDL_CloseAudioDevice(deviceId);
        deviceId = 0;
    }
}

bool SoundManager::init() {
    SDL_AudioSpec desired, obtained;
    SDL_zero(desired);
    desired.freq = 44100;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 1024;
    desired.callback = audioCallback;
    desired.userdata = this;

    deviceId = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (deviceId == 0) {
        std::cerr << "[SoundManager] Failed to open SDL audio: " << SDL_GetError() << std::endl;
        return false;
    }

    loadOgg("assets/sounds/kick.ogg", kickSound);
    loadOgg("assets/sounds/goal.ogg", goalSound);
    loadOgg("assets/sounds/chat.ogg", chatSound);
    loadOgg("assets/sounds/join.ogg", joinSound);
    loadOgg("assets/sounds/leave.ogg", leaveSound);

    SDL_PauseAudioDevice(deviceId, 0);
    return true;
}

bool SoundManager::loadOgg(const std::string& path, DecodedAudio& outAudio) {
    int channels = 0;
    int sampleRate = 0;
    short* rawSamples = nullptr;

    int numSamples = stb_vorbis_decode_filename(path.c_str(), &channels, &sampleRate, &rawSamples);
    if (numSamples <= 0 || !rawSamples) {
        std::cerr << "[SoundManager] Failed to decode OGG: " << path << std::endl;
        return false;
    }

    outAudio.channels = 2;
    outAudio.sampleRate = 44100;

    if (channels == 1) {

        outAudio.samples.resize(numSamples * 2);
        for (int i = 0; i < numSamples; ++i) {
            outAudio.samples[i * 2] = rawSamples[i];
            outAudio.samples[i * 2 + 1] = rawSamples[i];
        }
    } else {
        outAudio.samples.assign(rawSamples, rawSamples + (numSamples * channels));
    }

    free(rawSamples);
    return true;
}

void SoundManager::play(SoundEffect effect, float volume) {
    if (deviceId == 0) return;

    const DecodedAudio* target = nullptr;
    switch (effect) {
        case SoundEffect::Kick:  target = &kickSound; break;
        case SoundEffect::Goal:  target = &goalSound; break;
        case SoundEffect::Chat:  target = &chatSound; break;
        case SoundEffect::Join:  target = &joinSound; break;
        case SoundEffect::Leave: target = &leaveSound; break;
    }

    if (!target || target->samples.empty()) return;

    std::lock_guard<std::mutex> lock(audioMutex);
    if (activeChannels.size() >= 32) {
        activeChannels.erase(activeChannels.begin());
    }
    ActiveChannel ch;
    ch.samples = target->samples.data();
    ch.totalSamples = target->samples.size();
    ch.cursor = 0;
    ch.volume = volume * masterVolume;
    activeChannels.push_back(ch);
}

void SoundManager::audioCallback(void* userdata, Uint8* stream, int len) {
    auto* self = static_cast<SoundManager*>(userdata);
    int16_t* out = reinterpret_cast<int16_t*>(stream);
    int sampleCount = len / sizeof(int16_t);

    std::fill(out, out + sampleCount, 0);

    std::lock_guard<std::mutex> lock(self->audioMutex);

    for (auto it = self->activeChannels.begin(); it != self->activeChannels.end();) {
        size_t remaining = it->totalSamples - it->cursor;
        size_t count = std::min((size_t)sampleCount, remaining);

        for (size_t i = 0; i < count; ++i) {
            int32_t val = out[i] + static_cast<int32_t>(it->samples[it->cursor + i] * it->volume);
            out[i] = static_cast<int16_t>(std::clamp(val, -32768, 32767));
        }

        it->cursor += count;
        if (it->cursor >= it->totalSamples) {
            it = self->activeChannels.erase(it);
        } else {
            ++it;
        }
    }
}

}
