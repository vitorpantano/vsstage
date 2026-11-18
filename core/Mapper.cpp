#include "core/Mapper.h"

#include <map>
#include <regex>
#include <set>
#include <unordered_map>

namespace vss::mapper {

namespace {
struct Rule { Role role; std::vector<std::string> kws; };
const std::vector<Rule>& rules() {
    static const std::vector<Rule> r = {
        {Role::Click, {"click", "clique", "metronomo", "metronome", "clk", "metro"}},
        {Role::Guide, {"guia", "guide", "cue", "cues", "locucao", "locutor", "count", "contagem", "gd"}},
        {Role::Drums, {"bateria", "batera", "drum", "drums", "kit", "dr"}},
        {Role::Perc, {"perc", "percussao", "percussion", "shaker", "pandeiro", "cajon", "tamb", "conga"}},
        {Role::Bass, {"baixo", "bass", "contrabaixo", "bx"}},
        {Role::Acoustic, {"violao", "acoustic", "acustico", "acustica", "nylon", "aco", "ag", "vl"}},
        {Role::Electric, {"guitarra", "guitar", "gtr", "electric", "eletrica", "eg", "gt"}},
        {Role::Keys, {"teclado", "keys", "piano", "rhodes", "organ", "orgao", "synth", "sintetizador", "tecl", "kb"}},
        {Role::Pad, {"pad", "pads", "ambient", "atmos", "atmosfera"}},
        {Role::Strings, {"cordas", "strings", "violino", "cello", "orquestra", "orchestra", "violoncelo", "viola"}},
        {Role::Backing, {"backing", "back", "bv", "bvs", "coro", "vocais", "choir", "vozes", "harmonia"}},
        {Role::Vocal, {"vocal", "voz", "vox", "lead", "cantor", "vocals"}},
        {Role::LoopFx, {"loop", "loops", "fx", "efeito", "efeitos", "sfx", "sample", "samples", "seq", "sequencia", "sequence"}},
    };
    return r;
}

std::string trimDigits(std::string s) {
    while (!s.empty() && std::isdigit(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}
bool startsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }

std::set<std::string> roleWords() {
    std::set<std::string> w = {"stem", "stems", "mix", "wav", "mp3"};
    for (auto& r : rules()) for (auto& k : r.kws) w.insert(k);
    return w;
}

std::string clean(const std::string& name) {
    std::string s = stripExtension(name);
    s = std::regex_replace(s, std::regex("\\d{2,3}\\s*bpm|bpm\\s*[-_ ]?\\d{2,3}", std::regex::icase), " ");
    s = std::regex_replace(s, std::regex("\\b(tom|key)[ _-]*[A-G][#b]?m?\\b", std::regex::icase), " ");
    std::vector<std::string> parts;
    {
        std::string cur;
        for (char c : s) {
            if (c == '_' || c == '-' || c == ' ' || c == '\t') { if (!cur.empty()) parts.push_back(cur); cur.clear(); }
            else cur.push_back(c);
        }
        if (!cur.empty()) parts.push_back(cur);
    }
    static const std::set<std::string> rw = roleWords();
    std::string out;
    for (auto& p : parts) {
        const std::string n = trimDigits(fold(p));
        if (n.empty() || rw.count(n)) continue;
        if (!out.empty()) out += ' ';
        out += p;
    }
    size_t i = 0;
    while (i < out.size() && (std::isdigit(static_cast<unsigned char>(out[i])) || out[i] == '.' || out[i] == ' ')) ++i;
    return out.substr(i);
}
}  // namespace

std::string fold(const std::string& s) {
    // Acentos comuns do português (UTF-8) -> letra base; o resto em minúsculas ASCII.
    static const std::unordered_map<std::string, char> acc = {
        {"á", 'a'}, {"à", 'a'}, {"â", 'a'}, {"ã", 'a'}, {"ä", 'a'}, {"Á", 'a'}, {"À", 'a'}, {"Â", 'a'}, {"Ã", 'a'},
        {"é", 'e'}, {"è", 'e'}, {"ê", 'e'}, {"ë", 'e'}, {"É", 'e'}, {"È", 'e'}, {"Ê", 'e'},
        {"í", 'i'}, {"ì", 'i'}, {"î", 'i'}, {"ï", 'i'}, {"Í", 'i'}, {"Î", 'i'},
        {"ó", 'o'}, {"ò", 'o'}, {"ô", 'o'}, {"õ", 'o'}, {"ö", 'o'}, {"Ó", 'o'}, {"Ô", 'o'}, {"Õ", 'o'},
        {"ú", 'u'}, {"ù", 'u'}, {"û", 'u'}, {"ü", 'u'}, {"Ú", 'u'}, {"Û", 'u'}, {"ç", 'c'}, {"Ç", 'c'}, {"ñ", 'n'}};
    std::string out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) { out.push_back(static_cast<char>(std::tolower(c))); ++i; continue; }
        const size_t len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
        auto it = acc.find(s.substr(i, len));
        out.push_back(it != acc.end() ? it->second : '?');
        i += len;
    }
    return out;
}

std::string stripExtension(const std::string& name) {
    const auto i = name.rfind('.');
    return (i != std::string::npos && i > 0 && name.size() - i <= 6) ? name.substr(0, i) : name;
}

std::vector<std::string> tokens(const std::string& name) {
    const std::string n = fold(stripExtension(name));
    std::vector<std::string> out;
    std::string cur;
    for (char c : n) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '#') cur.push_back(c);
        else { if (!cur.empty()) out.push_back(cur); cur.clear(); }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

Role roleOf(const std::string& fileName) {
    const auto toks = tokens(fileName);
    for (auto& rule : rules())
        for (auto& t : toks) {
            const std::string tt = trimDigits(t);
            for (auto& k : rule.kws)
                if (tt == k || (k.size() >= 5 && startsWith(tt, k))) return rule.role;
        }
    return Role::Other;
}

std::optional<double> bpmFromName(const std::string& name) {
    const std::string n = fold(stripExtension(name));
    static const std::regex r1("(\\d{2,3})\\s*bpm"), r2("bpm\\s*[-_ ]?(\\d{2,3})");
    std::smatch m;
    for (const std::regex* r : {&r1, &r2})
        if (std::regex_search(n, m, *r)) {
            const double v = std::stod(m[1].str());
            if (v >= 40 && v <= 220) return v;
        }
    return std::nullopt;
}

std::optional<std::string> keyFromName(const std::string& name) {
    const std::string n = fold(stripExtension(name));
    static const std::regex r("(?:tom|key)[ _-]*([a-g][#b]?m?)(?![a-z])");
    std::smatch m;
    if (!std::regex_search(n, m, r)) return std::nullopt;
    std::string k = m[1].str();
    k[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(k[0])));
    return k;
}

std::string titleFrom(const std::vector<std::string>& names, const std::string& fallback) {
    if (names.empty()) return fallback;
    std::vector<std::string> cleaned;
    for (auto& n : names) { auto c = clean(n); if (!c.empty()) cleaned.push_back(c); }
    if (cleaned.empty()) return fallback;
    std::map<std::string, int> counts;
    for (auto& c : cleaned) counts[c]++;
    auto best = counts.begin();
    for (auto it = counts.begin(); it != counts.end(); ++it) if (it->second > best->second) best = it;
    if (best->second >= 2 || cleaned.size() == 1) return best->first;
    // prefixo comum por palavras
    auto words = [](const std::string& s) { std::vector<std::string> w; std::string cur; for (char c : s) { if (c == ' ') { if (!cur.empty()) w.push_back(cur); cur.clear(); } else cur.push_back(c); } if (!cur.empty()) w.push_back(cur); return w; };
    auto first = words(cleaned[0]);
    size_t n = 0;
    for (; n < first.size(); ++n) {
        bool all = true;
        for (auto& c : cleaned) { auto w = words(c); if (n >= w.size() || fold(w[n]) != fold(first[n])) { all = false; break; } }
        if (!all) break;
    }
    if (n == 0) return cleaned[0];
    std::string out;
    for (size_t i = 0; i < n; ++i) { if (i) out += ' '; out += first[i]; }
    return out;
}

MappedSong analyze(const std::vector<std::string>& fileNames, const std::string& fallbackTitle) {
    std::vector<Role> roles;
    for (auto& f : fileNames) roles.push_back(roleOf(f));
    std::map<Role, int> total, seen;
    for (auto r : roles) total[r]++;
    MappedSong out;
    for (size_t i = 0; i < fileNames.size(); ++i) {
        const Role r = roles[i];
        const int k = ++seen[r];
        std::string display = r == Role::Other ? stripExtension(fileNames[i])
                              : (total[r] > 1 ? std::string(roleInfo(r).label) + " " + std::to_string(k) : std::string(roleInfo(r).label));
        out.files.push_back({fileNames[i], r, display});
    }
    out.title = titleFrom(fileNames, fallbackTitle);
    for (auto& f : fileNames) if (auto b = bpmFromName(f)) { out.bpm = b; break; }
    for (auto& f : fileNames) if (auto k = keyFromName(f)) { out.key = k; break; }
    return out;
}

}  // namespace vss::mapper
