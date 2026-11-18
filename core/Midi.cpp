#include "core/Midi.h"

#include <chrono>
#include <map>

#include "rtmidi/RtMidi.h"

namespace vss {

static const struct { Action a; const char* id; const char* label; } kActions[] = {
    {Action::PlayPause, "PLAY_PAUSE", "Play / Pause"}, {Action::Stop, "STOP", "Stop"},
    {Action::NextSection, "NEXT_SECTION", "Próxima seção"}, {Action::PrevSection, "PREV_SECTION", "Seção anterior"},
    {Action::LoopSection, "LOOP", "Loop da seção"}, {Action::TransposeUp, "TRANSPOSE_UP", "Tom +1"},
    {Action::TransposeDown, "TRANSPOSE_DOWN", "Tom −1"}, {Action::BpmUp, "BPM_UP", "BPM +1"}, {Action::BpmDown, "BPM_DOWN", "BPM −1"},
    {Action::PadNext, "PAD_NEXT", "Pad: próximo tom"}, {Action::PadOff, "PAD_OFF", "Pad: desligar"},
    {Action::NextSong, "NEXT_SONG", "Próxima música"}, {Action::PrevSong, "PREV_SONG", "Música anterior"},
};
const char* actionId(Action a) { return kActions[static_cast<int>(a)].id; }
const char* actionLabel(Action a) { return kActions[static_cast<int>(a)].label; }
bool actionFromId(const std::string& id, Action* out) {
    for (auto& k : kActions) if (id == k.id) { *out = k.a; return true; }
    return false;
}

struct MidiInput::Impl {
    std::unique_ptr<rt::midi::RtMidiIn> in;
    std::mutex m;
    std::vector<MidiTrigger> q;
    std::map<std::string, std::chrono::steady_clock::time_point> last;

    static void cb(double, std::vector<unsigned char>* msg, void* user) {
        auto* self = static_cast<Impl*>(user);
        if (!msg || msg->size() < 2) return;
        const unsigned char st = (*msg)[0], d1 = (*msg)[1], d2 = msg->size() > 2 ? (*msg)[2] : 0;
        const int type = st & 0xF0, ch = (st & 0x0F) + 1;
        std::string key, label;
        if (type == 0x90 && d2 > 0) { key = "N:" + std::to_string(ch) + ":" + std::to_string(d1); label = "Nota " + std::to_string(d1) + " (canal " + std::to_string(ch) + ")"; }
        else if (type == 0xB0 && d2 >= 64) { key = "C:" + std::to_string(ch) + ":" + std::to_string(d1); label = "CC " + std::to_string(d1) + " (canal " + std::to_string(ch) + ")"; }
        else if (type == 0xC0) { key = "P:" + std::to_string(ch) + ":" + std::to_string(d1); label = "Program " + std::to_string(d1) + " (canal " + std::to_string(ch) + ")"; }
        else return;
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> g(self->m);
        auto it = self->last.find(key);
        if (it != self->last.end() && now - it->second < std::chrono::milliseconds(150)) return;   // antirrepique do pedal
        self->last[key] = now;
        self->q.push_back({key, label});
    }
};

MidiInput::MidiInput() : impl_(new Impl) {}
MidiInput::~MidiInput() { open(""); }

std::vector<std::string> MidiInput::ports() {
    std::vector<std::string> out;
    try {
        rt::midi::RtMidiIn probe;
        for (unsigned i = 0; i < probe.getPortCount(); ++i) out.push_back(probe.getPortName(i));
    } catch (...) {}
    return out;
}

bool MidiInput::open(const std::string& portName) {
    if (impl_->in) { try { impl_->in->closePort(); } catch (...) {} impl_->in.reset(); }
    name_.clear();
    if (portName.empty()) return true;
    try {
        auto in = std::make_unique<rt::midi::RtMidiIn>();
        for (unsigned i = 0; i < in->getPortCount(); ++i) {
            if (in->getPortName(i) == portName) {
                in->ignoreTypes(true, true, true);
                in->setCallback(&Impl::cb, impl_.get());
                in->openPort(i, "VS Stage");
                impl_->in = std::move(in);
                name_ = portName;
                return true;
            }
        }
    } catch (...) {}
    return false;
}

bool MidiInput::isOpen() const { return impl_->in != nullptr; }

std::vector<MidiTrigger> MidiInput::poll() {
    std::lock_guard<std::mutex> g(impl_->m);
    std::vector<MidiTrigger> out;
    out.swap(impl_->q);
    return out;
}

}  // namespace vss
