// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <cassert>
#include <cstdint>
#include <cstdio>
struct { uintptr_t hc=0; uint64_t phys=0x800000000ULL; bool poisoned=false, ready=true; } gHost;
struct { uint32_t isBefore; } gRootDiagnostic;
constexpr unsigned kMillisecondScale=1000;
constexpr unsigned kIOReturnNotReady=1, kIOReturnBusy=2, kIOReturnIOError=3, kIOReturnTimeout=4;
constexpr uint32_t kUfsFatalStatus=0x800;
static uint32_t regs[64];
static unsigned writes, scenario;
static uint64_t ticks;
static uint32_t read32(uintptr_t,unsigned off) { return regs[off/4]; }
static void hostWrite(unsigned off,uint32_t value) {
  ++writes;
  if ((scenario==1 && off==0x60) || (scenario==2 && off==0x54) ||
      (scenario==3 && off==0x60)) return;
  regs[off/4]=value;
}
static void clock_interval_to_deadline(unsigned n,unsigned scale,uint64_t *d) { *d=ticks+n*scale; }
static uint64_t mach_absolute_time() { return ticks; }
static void IODelay(unsigned n) { ticks+=n; }
static void recordTransport(unsigned,unsigned) {}
#include "../RootQueue.h"
int main() {
  for(scenario=0;scenario<4;++scenario) {
    for(auto &r:regs) r=0;
    gHost.poisoned=false; gHost.ready=true; ticks=0; writes=0;
    regs[0x34/4]=1; regs[0x30/4]=0xf;
    const char *why="";
    if(scenario==0) {
      regs[0x58/4]=1;
      assert(!hostStopQueue(&why) && writes==0 && !gHost.poisoned);
      continue;
    }
    if(scenario==1) {
      regs[0x60/4]=1;
      assert(!hostStopQueue(&why));
    } else {
      assert(hostStopQueue(&why));
      assert(!hostStartQueue(&why));
    }
    assert(gHost.poisoned && !gHost.ready);
    unsigned before=writes;
    assert(!hostStartQueue(&why) && writes==before);
    assert(!hostStopQueue(&why) && writes==before);
  }
  puts("PASS: busy firmware untouched; failed stop/address/start quarantines transport; retry writes nothing");
}
