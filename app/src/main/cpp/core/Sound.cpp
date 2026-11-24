#include "Sound.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "Fs.h"

namespace wd {

namespace {

const float kTau = 6.28318530718f;
const char* kVersion = "4";

float midi(float n) { return 440.f * std::pow(2.f, (n - 69.f) / 12.f); }

struct Note {
    float time;
    float pitch;
    float gain;
    float decay;
    int voice;
};

void bell(std::vector<float>& buf, int rate, const Note& n) {
    static const float ratios[4][5] = {{1.f, 2.0f, 3.0f, 4.16f, 5.43f},
                                       {1.f, 2.01f, 2.76f, 5.4f, 8.93f},
                                       {1.f, 3.0f, 5.0f, 7.0f, 9.0f},
                                       {1.f, 1.5f, 2.0f, 3.0f, 4.0f}};
    static const float amps[4][5] = {{1.f, 0.42f, 0.16f, 0.09f, 0.04f},
                                     {1.f, 0.36f, 0.22f, 0.08f, 0.03f},
                                     {1.f, 0.18f, 0.07f, 0.03f, 0.01f},
                                     {1.f, 0.25f, 0.3f, 0.12f, 0.05f}};
    int v = std::max(0, std::min(3, n.voice));
    float f0 = midi(n.pitch);
    size_t start = (size_t)(n.time * rate);
    size_t len = (size_t)(n.decay * 6.f * rate);
    for (int p = 0; p < 5; p++) {
        float f = f0 * ratios[v][p];
        if (f > rate * 0.45f) continue;
        float a = amps[v][p] * n.gain;
        float d = n.decay / (1.f + p * 0.9f);
        float phase = 0, step = kTau * f / rate;
        for (size_t i = 0; i < len && start + i < buf.size(); i++) {
            float t = (float)i / rate;
            float env = (1.f - std::exp(-t / 0.004f)) * std::exp(-t / d);
            buf[start + i] += a * env * std::sin(phase);
            phase += step;
            if (phase > kTau) phase -= kTau;
        }
    }
}

void pad(std::vector<float>& buf, int rate, float time, float pitch, float gain, float length) {
    size_t start = (size_t)(time * rate);
    size_t len = (size_t)(length * rate);
    float f = midi(pitch);
    float ph[3] = {0, 0, 0};
    float det[3] = {1.f, 1.003f, 0.997f};
    for (size_t i = 0; i < len && start + i < buf.size(); i++) {
        float t = (float)i / rate;
        float env = std::min(1.f, t / (length * 0.35f)) * std::exp(-std::max(0.f, t - length * 0.35f) / (length * 0.25f));
        env *= std::min(1.f, (length - t) / (length * 0.3f));
        float s = 0;
        for (int k = 0; k < 3; k++) {
            s += std::sin(ph[k]) + 0.25f * std::sin(ph[k] * 2.f);
            ph[k] += kTau * f * det[k] / rate;
            if (ph[k] > kTau * 4) ph[k] -= kTau * 4;
        }
        buf[start + i] += gain * env * s / 3.f;
    }
}

void reverb(std::vector<float>& buf, int rate, float mix) {
    static const float delays[4] = {0.0297f, 0.0371f, 0.0411f, 0.0437f};
    std::vector<float> wet(buf.size(), 0.f);
    for (float d : delays) {
        size_t n = (size_t)(d * rate);
        std::vector<float> line(n, 0.f);
        size_t idx = 0;
        float fb = 0.78f, lp = 0;
        for (size_t i = 0; i < buf.size(); i++) {
            float out = line[idx];
            lp = lp * 0.35f + out * 0.65f;
            line[idx] = buf[i] + lp * fb;
            idx = (idx + 1) % n;
            wet[i] += out * 0.25f;
        }
    }
    float ap = 0;
    for (size_t i = 0; i < buf.size(); i++) {
        ap = ap * 0.5f + wet[i] * 0.5f;
        buf[i] = buf[i] * (1.f - mix * 0.5f) + ap * mix;
    }
}

void finish(std::vector<float>& buf, float peak) {
    float m = 0;
    for (float v : buf) m = std::max(m, std::fabs(v));
    if (m <= 0) return;
    float k = peak / m;
    for (float& v : buf) v *= k;
    size_t fade = std::min<size_t>(buf.size(), 2000);
    for (size_t i = 0; i < fade; i++) buf[buf.size() - 1 - i] *= (float)i / fade;
}

std::vector<float> compose(const std::vector<Note>& notes, int rate, float length, float verb, float peak) {
    std::vector<float> buf((size_t)(length * rate), 0.f);
    for (auto& n : notes) bell(buf, rate, n);
    reverb(buf, rate, verb);
    finish(buf, peak);
    return buf;
}

bool writeWav(const std::string& path, const std::vector<float>& data, int rate) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    uint32_t bytes = (uint32_t)data.size() * 2;
    auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f);
    w32(36 + bytes);
    fwrite("WAVEfmt ", 1, 8, f);
    w32(16);
    w16(1);
    w16(1);
    w32((uint32_t)rate);
    w32((uint32_t)rate * 2);
    w16(2);
    w16(16);
    fwrite("data", 1, 4, f);
    w32(bytes);
    std::vector<int16_t> pcm(data.size());
    for (size_t i = 0; i < data.size(); i++) pcm[i] = (int16_t)std::lround(std::max(-1.f, std::min(1.f, data[i])) * 32767.f);
    fwrite(pcm.data(), 2, pcm.size(), f);
    return fclose(f) == 0;
}

}

const std::vector<std::string>& soundNames() {
    static const std::vector<std::string> names = {"startup", "logon", "shutdown", "notify", "background", "error",
                                                   "device-in", "device-out", "volume"};
    return names;
}

std::vector<float> renderSound(const std::string& name, int rate) {
    if (name == "startup") {
        std::vector<float> buf((size_t)(4.2f * rate), 0.f);
        pad(buf, rate, 0.0f, 52, 0.10f, 3.6f);
        pad(buf, rate, 0.0f, 59, 0.07f, 3.6f);
        Note ns[] = {{0.00f, 64, 0.55f, 0.9f, 0}, {0.16f, 71, 0.5f, 0.9f, 0}, {0.32f, 76, 0.5f, 1.0f, 0},
                     {0.48f, 80, 0.42f, 1.1f, 1}, {0.70f, 83, 0.36f, 1.3f, 1}, {0.70f, 76, 0.22f, 1.4f, 3}};
        for (auto& n : ns) bell(buf, rate, n);
        reverb(buf, rate, 0.55f);
        finish(buf, 0.8f);
        return buf;
    }
    if (name == "logon")
        return compose({{0.0f, 72, 0.6f, 0.55f, 0}, {0.12f, 76, 0.5f, 0.6f, 0}, {0.24f, 79, 0.5f, 0.75f, 1}, {0.24f, 84, 0.2f, 0.9f, 3}},
                       rate, 2.4f, 0.45f, 0.75f);
    if (name == "shutdown")
        return compose({{0.0f, 79, 0.5f, 0.6f, 0}, {0.16f, 76, 0.5f, 0.6f, 0}, {0.32f, 72, 0.5f, 0.8f, 1}, {0.48f, 67, 0.45f, 1.1f, 1}},
                       rate, 2.8f, 0.5f, 0.75f);
    if (name == "notify") return compose({{0.0f, 81, 0.6f, 0.35f, 1}, {0.09f, 88, 0.55f, 0.55f, 1}}, rate, 1.5f, 0.35f, 0.7f);
    if (name == "background") return compose({{0.0f, 76, 0.7f, 0.4f, 0}, {0.0f, 83, 0.15f, 0.3f, 3}}, rate, 1.2f, 0.3f, 0.6f);
    if (name == "error") return compose({{0.0f, 69, 0.6f, 0.35f, 2}, {0.13f, 64, 0.6f, 0.55f, 2}}, rate, 1.4f, 0.3f, 0.7f);
    if (name == "device-in") return compose({{0.0f, 72, 0.55f, 0.3f, 1}, {0.1f, 79, 0.55f, 0.5f, 1}}, rate, 1.2f, 0.3f, 0.65f);
    if (name == "device-out") return compose({{0.0f, 79, 0.55f, 0.3f, 1}, {0.1f, 72, 0.55f, 0.5f, 1}}, rate, 1.2f, 0.3f, 0.65f);
    if (name == "volume") return compose({{0.0f, 84, 0.6f, 0.12f, 1}}, rate, 0.5f, 0.15f, 0.55f);
    return {};
}

bool writeSounds(const std::string& dir) {
    std::string stamp = fs::join(dir, "version");
    std::string v;
    if (fs::readText(stamp, v) && v == kVersion) {
        bool all = true;
        for (auto& n : soundNames())
            if (!fs::exists(fs::join(dir, n + ".wav"))) all = false;
        if (all) return true;
    }
    fs::makeDirs(dir);
    const int rate = 44100;
    for (auto& n : soundNames())
        if (!writeWav(fs::join(dir, n + ".wav"), renderSound(n, rate), rate)) return false;
    return fs::writeText(stamp, kVersion);
}

}
