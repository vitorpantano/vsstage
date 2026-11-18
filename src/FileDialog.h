#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace vss::dlg {
/// Seleciona vários arquivos de áudio (WAV/MP3/FLAC). Vazio = cancelado.
std::vector<std::filesystem::path> openAudio(void* ownerWindow);
/// Seleciona um pacote .vsstage.
std::filesystem::path openPackage(void* ownerWindow);
/// Escolhe onde salvar um pacote .vsstage.
std::filesystem::path savePackage(void* ownerWindow, const std::string& suggestedName);
/// Seleciona uma pasta.
std::filesystem::path pickFolder(void* ownerWindow);
/// Lista arquivos de áudio de uma pasta (ordenados).
std::vector<std::filesystem::path> audioFilesIn(const std::filesystem::path& dir);
bool isAudioFile(const std::filesystem::path& p);
}  // namespace vss::dlg
