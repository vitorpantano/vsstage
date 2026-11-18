#pragma once
// Entrada MIDI (pedais/controladores via RtMidi). A thread do driver só enfileira; a UI lê com poll().
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace vss {

enum class Action {
    PlayPause, Stop, NextSection, PrevSection, LoopSection, TransposeUp, TransposeDown, BpmUp, BpmDown,
    PadNext, PadOff, NextSong, PrevSong, Count
};
const char* actionId(Action a);
const char* actionLabel(Action a);
bool actionFromId(const std::string& id, Action* out);

struct MidiTrigger {
    std::string key;      // "N:canal:número" (nota), "C:canal:número" (CC), "P:canal:número" (program change)
    std::string label;    // legível: "Nota 60 (canal 1)"
};

class MidiInput {
public:
    MidiInput();
    ~MidiInput();
    std::vector<std::string> ports();
    bool open(const std::string& portName);   // "" fecha
    bool isOpen() const;
    const std::string& openName() const { return name_; }
    /// Disparos (apertos de botão) acumulados desde a última chamada.
    std::vector<MidiTrigger> poll();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string name_;
};

}  // namespace vss
