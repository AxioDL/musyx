#include "musyx/platform.h"

#if MUSY_TARGET == MUSY_TARGET_PC

#include "musyx/debugger_core.hpp"
#include <algorithm>
#include <cstring>

namespace musyx::debugger {

namespace {
const char* macroOpcodeName(uint16_t op) {
    switch (op) {
    case 0x00: return "END";
    case 0x01: return "STOP";
    case 0x02: return "IF_KEY";
    case 0x03: return "IF_VELOCITY";
    case 0x04: return "WAIT";
    case 0x05: return "LOOP";
    case 0x06: return "GOTO";
    case 0x07: return "WAIT_MS";
    case 0x08: return "PLAY_MACRO";
    case 0x09: return "SEND_KEY_OFF";
    case 0x0A: return "IF_MODULATION";
    case 0x0B: return "SET_PIANO_PANNING";
    case 0x0C: return "SET_ADSR";
    case 0x0D: return "SCALE_VOLUME";
    case 0x0E: return "SET_PANNING";
    case 0x0F: return "ENVELOPE";
    case 0x10: return "START_SAMPLE";
    case 0x11: return "STOP_SAMPLE";
    case 0x12: return "KEY_OFF";
    case 0x13: return "IF_RANDOM";
    case 0x14: return "FADE_IN";
    case 0x15: return "SET_SURROUND_PANNING";
    case 0x16: return "SET_ADSR_FROM_CTRL";
    case 0x17: return "RANDOM_KEY";
    case 0x18: return "ADD_KEY";
    case 0x19: return "SET_KEY";
    case 0x1A: return "LAST_KEY";
    case 0x1B: return "PORTAMENTO";
    case 0x1C: return "VIBRATO";
    case 0x1D: return "PITCH_SWEEP_0";
    case 0x1E: return "PITCH_SWEEP_1";
    case 0x1F: return "SET_PITCH";
    case 0x20: return "SET_PITCH_ADSR";
    case 0x21: return "SCALE_VOLUME_DLS";
    case 0x22: return "SET_MOD2_VIBRATO";
    case 0x23: return "SETUP_TREMOLO";
    case 0x24: return "RETURN";
    case 0x25: return "GOSUB";
    case 0x28: return "TRAP_EVENT";
    case 0x29: return "UNTRAP_EVENT";
    case 0x2A: return "SEND_MESSAGE";
    case 0x2B: return "GET_MESSAGE";
    case 0x2C: return "GET_VID";
    case 0x30: return "ADD_AGE_COUNTER";
    case 0x31: return "SET_AGE_COUNTER";
    case 0x32: return "SEND_FLAG";
    case 0x33: return "SET_PITCH_WHEEL_RANGE";
    case 0x34: return "SCALE_REVERB";
    case 0x35: return "SET_PITCHBEND_AFTER_KEYOFF";
    case 0x36: return "SET_PRIORITY";
    case 0x37: return "ADD_PRIORITY";
    case 0x38: return "SET_AGE_COUNTER_SPEED";
    case 0x39: return "SET_AGE_COUNTER_BY_VOLUME";
    case 0x40: return "VOLUME_SELECT";
    case 0x41: return "PANNING_SELECT";
    case 0x42: return "PITCH_WHEEL_SELECT";
    case 0x43: return "MOD_WHEEL_SELECT";
    case 0x44: return "PEDAL_SELECT";
    case 0x45: return "PORTAMENTO_SELECT";
    case 0x46: return "REVERB_SELECT";
    case 0x47: return "SURROUND_PANNING_SELECT";
    case 0x48: return "DOPPLER_SELECT";
    case 0x49: return "TREMOLO_SELECT";
    case 0x4A: return "PRE_AUX_A_SELECT";
    case 0x4B: return "PRE_AUX_B_SELECT";
    case 0x4C: return "POST_AUX_B_SELECT";
    case 0x4D: return "AUX_A_FX_SELECT";
    case 0x4E: return "AUX_B_FX_SELECT";
    case 0x50: return "SETUP_LFO";
    case 0x58: return "MODE_SELECT";
    case 0x59: return "SET_KEY_GROUP";
    case 0x5A: return "SRC_MODE_SELECT";
    case 0x5E: return "FILTER_SELECT";
    case 0x5F: return "FILTER_SELECT";
    case 0x60: return "VAR_ADD";
    case 0x61: return "VAR_SUB";
    case 0x62: return "VAR_MUL";
    case 0x63: return "VAR_DIV";
    case 0x64: return "VAR_MOD";
    case 0x65: return "SET_VAR_IMMEDIATE";
    case 0x70: return "IF_VAR_COMPARE_0";
    case 0x71: return "IF_VAR_COMPARE_1";
    default: return "UNKNOWN";
    }
}
}

Debugger::Debugger() = default;
Debugger::~Debugger() { detach(); }

void Debugger::attach() {
    if (attached_) return;
    MUSYX_DEBUG_HOOKS hooks{};
    hooks.onEvent = &Debugger::onRuntimeEvent;
    hooks.user = this;
    musyxDebuggerSetHooks(&hooks);
    attached_ = true;
}

void Debugger::detach() {
    if (!attached_) return;
    musyxDebuggerSetHooks(nullptr);
    attached_ = false;
}

void Debugger::onRuntimeEvent(const MUSYX_DEBUG_EVENT* e, void* user) {
    auto* self = static_cast<Debugger*>(user);
    if (!self || !e) return;

    // Single producer (MusyX synth thread), single consumer (UI thread).
    // No mutex or heap allocation is performed on the audio thread.
    const uint64_t w = self->eventWrite_.fetch_add(1, std::memory_order_relaxed);
    const uint64_t r = self->eventRead_.load(std::memory_order_acquire);
    if (w - r >= EventRingSize) {
        // Drop the oldest event by advancing the consumer cursor. This is safe for
        // a debugger trace because the ring is explicitly a bounded history.
        self->eventRead_.store(w - EventRingSize + 1, std::memory_order_release);
    }
    auto& slot = self->eventRing_[w % EventRingSize];
    slot.raw = *e;
    slot.wall = std::chrono::steady_clock::now();
    std::atomic_thread_fence(std::memory_order_release);
}

void Debugger::handleEvent(const MUSYX_DEBUG_EVENT& e) {
    // Macro breakpoints are evaluated inside the runtime hook so a hit pauses
    // at the instruction boundary. The UI thread only records the event.
    if (e.kind == MUSYX_DEBUG_EVENT_MESSAGE && state_.breakOnMessage) {
        musyxDebuggerSetPaused(1);
        state_.log.emplace_back("Debugger stopped on message");
    }
}

void Debugger::update() {
    // Refresh the runtime snapshot first; this is a UI-thread copy.
    musyxDebuggerCapture(&state_.snapshot);

    uint64_t r = eventRead_.load(std::memory_order_relaxed);
    const uint64_t w = eventWrite_.load(std::memory_order_acquire);
    while (r < w) {
        const auto& slot = eventRing_[r % EventRingSize];
        std::atomic_thread_fence(std::memory_order_acquire);
        state_.events.push_back(slot);
        handleEvent(slot.raw);
        ++r;
    }
    eventRead_.store(r, std::memory_order_release);
    while (state_.events.size() > 4096) state_.events.pop_front();
}

void Debugger::pause() { musyxDebuggerSetPaused(1); }
void Debugger::resume() { musyxDebuggerSetPaused(0); }
void Debugger::stepFrame() { musyxDebuggerStepFrame(); }
bool Debugger::paused() const { return musyxDebuggerIsPaused() != 0; }

std::vector<MacroCommand> Debugger::macroCommandsForVoice(uint32_t voiceIndex,
                                                           uint32_t maxCommands) const {
    std::vector<MacroCommand> result;
    if (voiceIndex >= state_.snapshot.voiceCount || maxCommands == 0) return result;

    const SYNTH_VOICE& voice = state_.snapshot.voices[voiceIndex];
    std::vector<MUSYX_DEBUG_MACRO_COMMAND> raw(std::min<uint32_t>(maxCommands, 4096u));
    const u32 count = musyxDebuggerGetVoiceMacroCommands(&voice, raw.data(),
                                                          static_cast<u32>(raw.size()));
    result.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        MacroCommand command;
        command.index = raw[i].index;
        command.address = static_cast<uintptr_t>(raw[i].address);
        command.opcode = raw[i].opcode;
        command.arg0 = raw[i].arg0;
        command.arg1 = raw[i].arg1;
        command.current = raw[i].current != 0;
        command.name = macroOpcodeName(command.opcode);
        result.push_back(std::move(command));
    }
    return result;
}

bool Debugger::addBreakpoint(const Breakpoint& bp) {
    if (state_.breakpoints.size() >= MUSYX_DEBUG_MAX_BREAKPOINTS) return false;
    const uint32_t index = static_cast<uint32_t>(state_.breakpoints.size());
    state_.breakpoints.push_back(bp);
    MUSYX_DEBUG_BREAKPOINT rb{};
    rb.macroId = bp.macroId;
    rb.opcode = bp.opcode;
    rb.voiceIndex = bp.voiceIndex;
    rb.enabled = bp.enabled ? 1 : 0;
    musyxDebuggerSetBreakpoint(index, &rb);
    return true;
}

void Debugger::clearBreakpoints() {
    state_.breakpoints.clear();
    musyxDebuggerClearBreakpoints();
}

void Debugger::addWatch(Watch w) { state_.watches.push_back(std::move(w)); }
void Debugger::clearWatches() { state_.watches.clear(); }

bool Debugger::inspectMemory(uintptr_t address, size_t bytes, std::vector<uint8_t>& out) const {
    if (!address || bytes == 0 || bytes > 4096) return false;

    const auto& s = state_.snapshot;
    auto inRange = [&](uintptr_t base, size_t size) {
        return base != 0 && address >= base && bytes <= size && address - base <= size - bytes;
    };

    if (s.voiceCount && synthVoice &&
        inRange(reinterpret_cast<uintptr_t>(synthVoice),
                sizeof(SYNTH_VOICE) * s.voiceCount)) {
        out.resize(bytes);
        std::memcpy(out.data(), reinterpret_cast<const void*>(address), bytes);
        return true;
    }
    if (inRange(reinterpret_cast<uintptr_t>(seqInstance), sizeof(seqInstance))) {
        out.resize(bytes);
        std::memcpy(out.data(), reinterpret_cast<const void*>(address), bytes);
        return true;
    }
    return false;
}

std::vector<std::filesystem::path>
Debugger::discoverSources(const std::filesystem::path& root) const {
    std::vector<std::filesystem::path> result;
    if (!std::filesystem::exists(root)) return result;
    for (auto it = std::filesystem::recursive_directory_iterator(root);
         it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (!it->is_regular_file()) continue;
        auto ext = it->path().extension().string();
        if (ext == ".c" || ext == ".h" || ext == ".cpp" || ext == ".hpp")
            result.push_back(it->path());
    }
    std::sort(result.begin(), result.end());
    return result;
}

} // namespace musyx::debugger


#endif /* MUSY_TARGET == MUSY_TARGET_PC */
