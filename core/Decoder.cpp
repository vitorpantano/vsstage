#include "core/Decoder.h"

#include <vector>

#include "engine/Constants.h"
#include "miniaudio/miniaudio.h"

namespace vss {

std::unique_ptr<vs::TrackBuffer> decodeFile(const std::filesystem::path& path, std::string* err) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, vs::kEngineSampleRate);
    ma_decoder dec;
#if defined(_WIN32)
    ma_result r = ma_decoder_init_file_w(path.c_str(), &cfg, &dec);
#else
    ma_result r = ma_decoder_init_file(path.c_str(), &cfg, &dec);
#endif
    if (r != MA_SUCCESS) {
        if (err) *err = "Formato não suportado ou arquivo inválido (use WAV, MP3 ou FLAC): " + path.filename().string();
        return nullptr;
    }
    auto buf = std::make_unique<vs::TrackBuffer>();
    ma_uint64 len = 0;
    if (ma_decoder_get_length_in_pcm_frames(&dec, &len) == MA_SUCCESS && len > 0 && len < (1ull << 31)) buf->pcm.reserve(static_cast<size_t>(len) * 2);
    std::vector<float> tmp(65536 * 2);
    for (;;) {
        ma_uint64 got = 0;
        r = ma_decoder_read_pcm_frames(&dec, tmp.data(), 65536, &got);
        if (got > 0) buf->pcm.insert(buf->pcm.end(), tmp.begin(), tmp.begin() + static_cast<size_t>(got) * 2);
        if (r != MA_SUCCESS || got == 0) break;
    }
    ma_decoder_uninit(&dec);
    buf->frames = static_cast<int64_t>(buf->pcm.size() / 2);
    if (buf->frames < 1024) {
        if (err) *err = "Arquivo vazio ou muito curto: " + path.filename().string();
        return nullptr;
    }
    return buf;
}

}  // namespace vss
