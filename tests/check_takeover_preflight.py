#!/usr/bin/env python3
"""Execute the production preflight slice with failed-map/conflict stubs."""
from pathlib import Path
import subprocess
import sys

source = Path(__file__).resolve().parent.parent
out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
text = (source / 'NativeUfs.cpp').read_text()
start = text.index('  uintptr_t smmu = mapPhys', text.index('static bool hostBringUp'))
end = text.index('  smmuBypassStream', start)
body = text[start:end]
assert body.index('planUfsSmmu') < body.index('hostStopQueue')
code = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
struct { int smmuMap; uint64_t phys; } gHost;
struct UfsSmmuPlan { const char *error; };
constexpr uint64_t kDmaBytes=0x22000;
static unsigned scenario, queueCalls;
static uintptr_t mapPhys(uint64_t base, uint64_t bytes, int &) {
  assert(base==0x15000000 && bytes==0xd0000);
  return scenario==0 ? 0 : 1;
}
static uint32_t read32(uintptr_t, unsigned) { return 0; }
template<class Read> UfsSmmuPlan planUfsSmmu(unsigned sid, uint64_t, uint64_t, Read) {
  assert(sid==0x60);
  return {scenario==1 ? "context conflict" : nullptr};
}
static bool hostStopQueue(const char **why) {
  ++queueCalls;
  if (scenario==3) { *why="queue busy"; return false; }
  return true;
}
static bool preflight(const char **why) {
''' + body + r'''
  return true;
}
int main() {
  for (scenario=0;scenario<4;++scenario) {
    queueCalls=0; const char *why="";
    bool ok=preflight(&why);
    assert(ok==(scenario==2));
    assert(queueCalls==(scenario>=2 ? 1u : 0u));
    if (scenario==0) assert(!strcmp(why,"SMMU preflight map"));
    if (scenario==1) assert(!strcmp(why,"context conflict"));
    if (scenario==3) assert(!strcmp(why,"queue busy"));
  }
  puts("PASS: production SMMU preflight refuses map/conflict before queue access; queue failure propagates");
}
'''
test = out / 'takeover-preflight.cpp'
test.write_text(code)
binary = out / 'takeover-preflight'
subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', str(test), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
