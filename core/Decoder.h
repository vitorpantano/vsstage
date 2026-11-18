#pragma once
#include <filesystem>
#include <memory>
#include <string>

#include "engine/TrackBuffer.h"

namespace vss {
/// Decodifica wav/mp3/flac para float estéreo @48 kHz (reamostra se preciso). Retorna nullptr e preenche `err` em falha.
std::unique_ptr<vs::TrackBuffer> decodeFile(const std::filesystem::path& path, std::string* err = nullptr);
}  // namespace vss
