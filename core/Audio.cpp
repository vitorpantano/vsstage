#include "core/Audio.h"

#include <chrono>
#include <cstdlib>
#include <cstring>

#include "miniaudio/miniaudio.h"

namespace vss {

AudioDevice::AudioDevice(vs::MultitrackEngine& engine) : engine_(engine) { idSize_ = sizeof(ma_device_id); }

AudioDevice::~AudioDevice() {
    stop();
    if (ctx_) { ma_context_uninit(ctx_); delete ctx_; ctx_ = nullptr; }
}

bool AudioDevice::ensureContext() {
    if (ctx_) return true;
    ctx_ = new ma_context();
    ma_backend backends[2];
    ma_uint32 n = 0;
    const char* forced = std::getenv("VSS_AUDIO_BACKEND");
    if (forced && std::string(forced) == "null") backends[n++] = ma_backend_null;   // testes sem placa de som
    else {
#if defined(_WIN32)
        backends[n++] = ma_backend_wasapi;
#else
        backends[n++] = ma_backend_pulseaudio; backends[n++] = ma_backend_alsa;
#endif
    }
    if (ma_context_init(backends, n, nullptr, ctx_) != MA_SUCCESS) {
        // último recurso: qualquer backend disponível
        if (ma_context_init(nullptr, 0, nullptr, ctx_) != MA_SUCCESS) {
            err_ = "Não foi possível iniciar o sistema de áudio";
            delete ctx_; ctx_ = nullptr; return false;
        }
    }
    return true;
}

std::vector<OutputDeviceInfo> AudioDevice::listDevices() {
    std::vector<OutputDeviceInfo> out;
    if (!ensureContext()) return out;
    ma_device_info* infos = nullptr; ma_uint32 count = 0;
    if (ma_context_get_devices(ctx_, &infos, &count, nullptr, nullptr) != MA_SUCCESS) return out;
    ids_.assign(static_cast<size_t>(count) * idSize_, 0);
    for (ma_uint32 i = 0; i < count; ++i) {
        std::memcpy(ids_.data() + static_cast<size_t>(i) * idSize_, &infos[i].id, idSize_);
        OutputDeviceInfo d;
        d.name = infos[i].name;
        d.isDefault = infos[i].isDefault != 0;
        ma_device_info full;
        int maxCh = 2;
        if (ma_context_get_device_info(ctx_, ma_device_type_playback, &infos[i].id, &full) == MA_SUCCESS) {
            for (ma_uint32 k = 0; k < full.nativeDataFormatCount; ++k) maxCh = std::max<int>(maxCh, static_cast<int>(full.nativeDataFormats[k].channels));
        }
        d.maxChannels = std::min(maxCh, vs::kMaxOutChannels);
        out.push_back(d);
    }
    return out;
}

void AudioDevice::dataCallback(ma_device* dev, void* out, const void*, unsigned int frames) {
    auto* self = static_cast<AudioDevice*>(dev->pUserData);
    const auto t0 = std::chrono::steady_clock::now();
    self->engine_.render(static_cast<float*>(out), static_cast<int32_t>(frames), static_cast<int32_t>(dev->playback.channels));
    const float us = std::chrono::duration<float, std::micro>(std::chrono::steady_clock::now() - t0).count();
    const float budget = static_cast<float>(frames) * 1.0e6f / static_cast<float>(vs::kEngineSampleRate);
    const float load = us / budget;
    self->cpuAvg_.store(self->cpuAvg_.load(std::memory_order_relaxed) * 0.98f + load * 0.02f, std::memory_order_relaxed);
    float pk = self->cpuPeak_.load(std::memory_order_relaxed);
    while (load > pk && !self->cpuPeak_.compare_exchange_weak(pk, load, std::memory_order_relaxed)) {}
    if (load > 1.0f) self->overruns_.fetch_add(1, std::memory_order_relaxed);
}

bool AudioDevice::start(int deviceIndex, int channels, int periodFrames, bool exclusive) {
    stop();
    if (!ensureContext()) return false;
    if (ids_.empty()) listDevices();
    channels = std::clamp(channels, 1, static_cast<int>(vs::kMaxOutChannels));

    auto tryOpen = [&](int ch, bool excl) -> bool {
        ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
        cfg.playback.format = ma_format_f32;
        cfg.playback.channels = static_cast<ma_uint32>(ch);
        cfg.sampleRate = vs::kEngineSampleRate;
        cfg.periodSizeInFrames = static_cast<ma_uint32>(std::max(32, periodFrames));
        cfg.periods = 2;
        cfg.performanceProfile = ma_performance_profile_low_latency;
        cfg.noClip = MA_FALSE;
        cfg.dataCallback = &AudioDevice::dataCallback;
        cfg.pUserData = this;
        cfg.playback.shareMode = excl ? ma_share_mode_exclusive : ma_share_mode_shared;
        std::vector<unsigned char> idCopy(idSize_);
        if (deviceIndex >= 0 && static_cast<size_t>(deviceIndex + 1) * idSize_ <= ids_.size()) {
            std::memcpy(idCopy.data(), ids_.data() + static_cast<size_t>(deviceIndex) * idSize_, idSize_);
            cfg.playback.pDeviceID = reinterpret_cast<ma_device_id*>(idCopy.data());
        }
        auto* d = new ma_device();
        if (ma_device_init(ctx_, &cfg, d) != MA_SUCCESS) { delete d; return false; }
        dev_ = d;
        return true;
    };

    bool ok = tryOpen(channels, exclusive);
    if (!ok && exclusive) ok = tryOpen(channels, false);                 // exclusivo indisponível -> compartilhado
    if (!ok && channels > 2) { ok = tryOpen(2, false); }                  // N canais indisponível -> estéreo
    if (!ok) { err_ = "Não foi possível abrir o dispositivo de saída"; return false; }

    channels_ = static_cast<int>(dev_->playback.channels);
    exclusive_ = dev_->playback.shareMode == ma_share_mode_exclusive;
    cpuAvg_ = 0; cpuPeak_ = 0; overruns_ = 0;
    engine_.setStreamRunning(true);
    if (ma_device_start(dev_) != MA_SUCCESS) {
        err_ = "Falha ao iniciar a reprodução";
        ma_device_uninit(dev_); delete dev_; dev_ = nullptr;
        engine_.setStreamRunning(false);
        return false;
    }
    return true;
}

void AudioDevice::stop() {
    if (!dev_) return;
    ma_device_uninit(dev_);   // para o callback e aguarda a thread de áudio
    delete dev_;
    dev_ = nullptr;
    engine_.setStreamRunning(false);
}

StreamInfo AudioDevice::info() {
    StreamInfo i;
    if (!dev_) return i;
    i.open = true;
    i.backend = ma_get_backend_name(dev_->pContext->backend);
    i.sampleRate = static_cast<int>(dev_->sampleRate);
    i.channels = static_cast<int>(dev_->playback.channels);
    i.periodFrames = static_cast<int>(dev_->playback.internalPeriodSizeInFrames);
    const double rate = dev_->playback.internalSampleRate ? dev_->playback.internalSampleRate : 48000;
    i.latencyMs = 1000.0 * dev_->playback.internalPeriodSizeInFrames * dev_->playback.internalPeriods / rate;
    i.exclusive = exclusive_;
    i.cpuAvg = cpuAvg_.load();
    i.cpuPeak = cpuPeak_.exchange(0.f);
    i.overruns = overruns_.load();
    return i;
}

}  // namespace vss
