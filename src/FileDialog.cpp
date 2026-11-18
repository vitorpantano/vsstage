#include "FileDialog.h"

#include <algorithm>
#include <cctype>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#endif

namespace fs = std::filesystem;

namespace vss::dlg {

bool isAudioFile(const fs::path& p) {
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return std::tolower(c); });
    return e == ".wav" || e == ".mp3" || e == ".flac";
}

std::vector<fs::path> audioFilesIn(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec)) if (e.is_regular_file(ec) && isAudioFile(e.path())) out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

#if defined(_WIN32)

static std::vector<fs::path> openFiles(void* owner, const wchar_t* filter, bool multi, const wchar_t* title) {
    std::vector<wchar_t> buf(65536, 0);
    OPENFILENAMEW o{};
    o.lStructSize = sizeof(o);
    o.hwndOwner = static_cast<HWND>(owner);
    o.lpstrFilter = filter;
    o.lpstrFile = buf.data();
    o.nMaxFile = static_cast<DWORD>(buf.size());
    o.lpstrTitle = title;
    o.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (multi ? OFN_ALLOWMULTISELECT : 0);
    std::vector<fs::path> out;
    if (!GetOpenFileNameW(&o)) return out;
    std::wstring first = buf.data();
    const wchar_t* next = buf.data() + first.size() + 1;
    if (*next == 0) { out.emplace_back(first); return out; }   // um único arquivo: caminho completo
    while (*next) { out.emplace_back(fs::path(first) / next); next += wcslen(next) + 1; }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<fs::path> openAudio(void* owner) {
    static const wchar_t f[] = L"Áudio (WAV, MP3, FLAC)\0*.wav;*.mp3;*.flac\0Todos os arquivos\0*.*\0\0";
    return openFiles(owner, f, true, L"Selecione os arquivos da música");
}
fs::path openPackage(void* owner) {
    static const wchar_t f[] = L"Pacote VS Stage (*.vsstage)\0*.vsstage\0\0";
    auto v = openFiles(owner, f, false, L"Importar pacote");
    return v.empty() ? fs::path() : v[0];
}
fs::path savePackage(void* owner, const std::string& suggested) {
    std::vector<wchar_t> buf(1024, 0);
    std::wstring w;
    for (unsigned char c : suggested) w.push_back(c < 128 ? static_cast<wchar_t>(c) : L'_');
    wcsncpy(buf.data(), w.c_str(), buf.size() - 1);
    static const wchar_t f[] = L"Pacote VS Stage (*.vsstage)\0*.vsstage\0\0";
    OPENFILENAMEW o{};
    o.lStructSize = sizeof(o);
    o.hwndOwner = static_cast<HWND>(owner);
    o.lpstrFilter = f;
    o.lpstrFile = buf.data();
    o.nMaxFile = static_cast<DWORD>(buf.size());
    o.lpstrDefExt = L"vsstage";
    o.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetSaveFileNameW(&o) ? fs::path(buf.data()) : fs::path();
}
fs::path pickFolder(void* owner) {
    wchar_t name[MAX_PATH] = {0};
    BROWSEINFOW b{};
    b.hwndOwner = static_cast<HWND>(owner);
    b.pszDisplayName = name;
    b.lpszTitle = L"Selecione a pasta da música";
    b.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&b);
    if (!pidl) return {};
    wchar_t path[MAX_PATH] = {0};
    fs::path r;
    if (SHGetPathFromIDListW(pidl, path)) r = path;
    CoTaskMemFree(pidl);
    return r;
}

#else  // Linux: apenas stubs (testes)

std::vector<fs::path> openAudio(void*) { return {}; }
fs::path openPackage(void*) { return {}; }
fs::path savePackage(void*, const std::string&) { return {}; }
fs::path pickFolder(void*) { return {}; }

#endif

}  // namespace vss::dlg
