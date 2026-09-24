#pragma once

#if MUSY_TARGET == MUSY_TARGET_PC

#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>
#include <unordered_set>
#include <functional>
#include <chrono>
#include <array>
#include <filesystem>
#include "musyx/debugger.h"

namespace musyx::debugger {

struct Breakpoint {
    uint32_t macroId = UINT32_MAX;
    uint32_t opcode = UINT32_MAX;
    uint32_t voiceIndex = UINT32_MAX;
    bool enabled = true;
};

struct MacroCommand {
    uint32_t index = 0;
    uintptr_t address = 0;
    uint16_t opcode = 0;
    uint32_t arg0 = 0;
    uint32_t arg1 = 0;
    bool current = false;
    std::string name;
};

struct Watch {
    std::string name;
    enum Kind { VoiceField, Global, Memory } kind = VoiceField;
    uint32_t index = 0;
    std::string expression;
    bool enabled = true;
};

struct Event {
    MUSYX_DEBUG_EVENT raw{};
    std::chrono::steady_clock::time_point wall{};
};

struct State {
    MUSYX_DEBUG_SNAPSHOT snapshot{};
    std::deque<Event> events;
    std::vector<Breakpoint> breakpoints;
    std::vector<Watch> watches;
    std::vector<std::string> log;
    uint32_t selectedVoice = 0;
    uint32_t selectedSequence = 0;
    uint32_t selectedStream = 0;
    bool followSelectedVoice = true;
    bool breakOnMessage = false;
    bool showInactive = false;
};

class Debugger {
public:
    Debugger();
    ~Debugger();

    void attach();
    void detach();
    void update();

    State& state() { return state_; }
    const State& state() const { return state_; }

    void pause();
    void resume();
    void stepFrame();
    bool paused() const;

    /* Returns a decoded copy of the selected voice's macro. This is UI-toolkit
       independent; an ImGui/Qt/etc. client can render the returned records. */
    std::vector<MacroCommand> macroCommandsForVoice(uint32_t voiceIndex,
                                                     uint32_t maxCommands = 4096) const;

    bool addBreakpoint(const Breakpoint&);
    void clearBreakpoints();
    void addWatch(Watch);
    void clearWatches();

    bool inspectMemory(uintptr_t address, size_t bytes, std::vector<uint8_t>& out) const;
    std::vector<std::filesystem::path> discoverSources(const std::filesystem::path& root) const;

private:
    static void onRuntimeEvent(const MUSYX_DEBUG_EVENT*, void*);
    void handleEvent(const MUSYX_DEBUG_EVENT&);

    State state_;
    std::mutex mutex_;
    bool attached_ = false;
    static constexpr size_t EventRingSize = 4096;
    std::array<Event, EventRingSize> eventRing_{};
    std::atomic<uint64_t> eventWrite_{0};
    std::atomic<uint64_t> eventRead_{0};

};

} // namespace musyx::debugger


#endif /* MUSY_TARGET == MUSY_TARGET_PC */
