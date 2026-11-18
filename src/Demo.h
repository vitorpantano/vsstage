#pragma once
#include <filesystem>
#include <vector>
namespace vss {
/// Gera uma música de demonstração (click, bateria, baixo, teclado, pad) em `dir` e devolve os arquivos criados.
std::vector<std::filesystem::path> generateDemoSong(const std::filesystem::path& dir);
}
