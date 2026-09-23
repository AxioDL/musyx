#include "musyx/hardware.h"
#include "musyx/pc.h"
#include "hw_pc_internal.h"

/* Headless builds are explicitly caller-serialized, with no hidden threads. */
void hwInitIrq(void) { hwDisableIrq(); }
void hwExitIrq(void) {}
void hwEnableIrq(void) { salPCExternalLeave(); }
void hwDisableIrq(void) { salPCExternalEnter(); }
void hwIRQEnterCritical(void) { hwDisableIrq(); }
void hwIRQLeaveCritical(void) { hwEnableIrq(); }
bool sndPCStartAudio(void) { return false; }
bool sndPCOpenAudio(const SND_PC_CONFIG* preferred, SND_PC_CONFIG* obtained) {
  (void)preferred; (void)obtained; return false;
}
void sndPCStopAudio(void) {}
void sndPCPauseAudio(bool paused) { (void)paused; }
