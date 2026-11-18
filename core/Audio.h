#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "engine/MultitrackEngine.h"

typedef struct ma_device ma_device;
typedef struct ma_context ma_context;

namespace vss {

struct OutputDeviceInfo {
    std::string name;
    int maxChannels = 2;
    bool isDefault = false;
};

struct StreamInfo {
    bool open = false;
    std::string backend;
    int sampleRate = 0;
    int channels = 0;
    int periodFrames = 0;
    double latencyMs = -1;
    bool exclusive = false;
    float cpuAvg = 0, cpuPeak = 0;
    long overruns = 0;   // callbacks que passaram do orçamento de tempo (≈ xruns)
};

/// Saída de áudio (miniaudio: WASAPI no Windows). O engine é chamado direto do callback de áudio.
class AudioDevice {
public:
    explicit AudioDevice(vs::MultitrackEngine& engine);
    ~AudioDevice();

    std::vector<OutputDeviceInfo> listDevices();
    /// deviceIndex = índice em listDevices() (-1 = padrão). periodFrames: 128/256/512...
    bool start(int deviceIndex, int channels, int periodFrames, bool exclusive);
    void stop();
    StreamInfo info();
    const std::string& lastError() const { return err_; }

    static void dataCallback(ma_device* dev, void* out, const void* in, unsigned int frames);

private:
    bool ensureContext();
    vs::MultitrackEngine& engine_;
    ma_context* ctx_ = nullptr;
    ma_device* dev_ = nullptr;
    std::vector<unsigned char> ids_;      // ma_device_id[] opaco (preenchido em listDevices)
    size_t idSize_ = 0;
    std::atomic<float> cpuAvg_{0}, cpuPeak_{0};
    std::atomic<long> overruns_{0};
    std::string err_;
    int channels_ = 2;
    bool exclusive_ = false;
};

}  // namespace vss
