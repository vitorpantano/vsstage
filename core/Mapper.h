#pragma once
#include <optional>
#include <string>
#include <vector>

#include "core/Types.h"

namespace vss {

struct MappedFile { std::string fileName; Role role; std::string displayName; };
struct MappedSong { std::string title; std::optional<double> bpm; std::optional<std::string> key; std::vector<MappedFile> files; };

/// Importação inteligente: papel (Click, Guia, Bateria...), título, BPM e tom a partir dos NOMES dos arquivos.
namespace mapper {
std::string fold(const std::string& utf8);          // minúsculas, sem acentos
std::string stripExtension(const std::string& name);
std::vector<std::string> tokens(const std::string& name);
Role roleOf(const std::string& fileName);
std::optional<double> bpmFromName(const std::string& name);
std::optional<std::string> keyFromName(const std::string& name);
std::string titleFrom(const std::vector<std::string>& names, const std::string& fallback = "Nova música");
MappedSong analyze(const std::vector<std::string>& fileNames, const std::string& fallbackTitle = "Nova música");
}  // namespace mapper

}  // namespace vss
