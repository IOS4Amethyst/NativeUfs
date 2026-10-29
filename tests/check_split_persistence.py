#!/usr/bin/env python3
"""Exercise production store/log opens with split geometry; writes are stubs."""
from pathlib import Path
import subprocess
import sys

source = Path(__file__).resolve().parent.parent
out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
text = (source / 'NativeUfs.cpp').read_text()


def function(name):
    start = text.index('extern "C" int ' + name + ' (')
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


code = r'''
#define main parser_selftest
#include "ROOT_TEST"
#undef main
struct { bool ready; } gHost = {true};
static StoreGeometry gStoreGeometry;
static uint64_t gStoreRegionBlocks, gStoreRegionFirst, gLogRegionBlocks, gLogRegionFirst;
static constexpr uint64_t kLogReserveBlocks=8192, kExportBlocks=65536;
static unsigned mode, opens;
static unsigned storeMode() { return mode; }
static unsigned logMode() { return 1; }
static uint64_t reserveBlocks() { return 1024*256; }
static void IOSleep(unsigned) {}
static void IOLog(const char *, ...) {}
static bool hostStoreOpen(uint64_t, uint64_t) { ++opens; return true; }
static bool hostLogOpen(uint64_t, uint64_t) { ++opens; return true; }
'''.replace('ROOT_TEST', str(source / 'tests/RootLayout.cpp'))
code += function('native_ufs_store_open') + '\n' + function('native_ufs_log_open')
code += r'''
int main() {
  splitDevice(); checksums();
  RootLayout layout; const char *why=nullptr;
  assert(findRootLayout(&layout,&why) && layout.manifestStatus==0);
  gStoreGeometry=rootStoreGeometry(layout);
  assert(!gStoreGeometry.valid);
  for (mode=1; mode<=2; ++mode) {
    uint64_t first=123, blocks=456;
    assert(!native_ufs_store_open(1024,&first,&why));
    assert(!strcmp(why,"no root layout") && first==123);
    assert(!native_ufs_log_open(&first,&blocks,&why));
    assert(!strcmp(why,"no root layout") && first==123 && blocks==456);
    assert(opens==0 && !gStoreRegionBlocks && !gLogRegionBlocks);
  }
  puts("PASS: split Data modes 1/2 and log/export refuse; zero host write-window opens");
}
'''
test = out / 'split-persistence.cpp'
test.write_text(code)
binary = out / 'split-persistence'
subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-g',
                str(test), '-lcrypto', '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
