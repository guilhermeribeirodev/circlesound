#pragma once
// Tiny procedural drum/bass sounds, so there are no sample files to manage.
// Add a new instrument = add a function that returns synth::make(...).
#include "raylib.h"
#include <cmath>
#include <cstdint>
#include <functional>

namespace synth {

constexpr float kTau = 6.28318530718f;
constexpr int   kRate = 44100;

inline float noise() {
    static uint32_t s = 22222;
    s = s * 1664525u + 1013904223u;
    return (s >> 8) / 8388608.0f - 1.0f;
}

// gen(t) returns a sample in [-1, 1] for time t (seconds). It may keep state (mutable lambda).
inline Sound make(float seconds, std::function<float(float)> gen) {
    int n = (int)(seconds * kRate);
    short* data = (short*)MemAlloc(n * sizeof(short));
    for (int i = 0; i < n; i++) {
        float t = (float)i / kRate;
        float fade = fminf(1.0f, (seconds - t) * 200.0f);   // avoid end click
        float s = fmaxf(-1.0f, fminf(1.0f, gen(t))) * fade;
        data[i] = (short)(s * 30000.0f);
    }
    Wave w{};
    w.frameCount = n; w.sampleRate = kRate; w.sampleSize = 16; w.channels = 1; w.data = data;
    Sound snd = LoadSoundFromWave(w);
    UnloadWave(w);
    return snd;
}

inline Sound kick() {
    float ph = 0;
    return make(0.45f, [ph](float t) mutable {
        ph += (45.0f + 130.0f * std::exp(-t * 35.0f)) / kRate;
        return std::sin(kTau * ph) * std::exp(-t * 7.0f) * 0.95f;
    });
}

inline Sound snare() {
    float ph = 0;
    return make(0.25f, [ph](float t) mutable {
        ph += 190.0f / kRate;
        return noise() * std::exp(-t * 20.0f) * 0.6f + std::sin(kTau * ph) * std::exp(-t * 25.0f) * 0.5f;
    });
}

inline Sound hat() {
    float prev = 0;
    return make(0.08f, [prev](float t) mutable {
        float n = noise();
        float hp = n - prev;  // crude high-pass
        prev = n;
        return hp * std::exp(-t * 60.0f) * 0.5f;
    });
}

inline Sound bass() {
    float ph = 0;
    return make(0.35f, [ph](float t) mutable {
        ph += 55.0f / kRate;
        float saw = 2.0f * (ph - std::floor(ph)) - 1.0f;
        float env = fminf(1.0f, t * 500.0f) * std::exp(-t * 5.0f);
        return (0.6f * std::sin(kTau * ph) + 0.3f * saw) * env * 0.8f;
    });
}

} // namespace synth
