// SPDX-License-Identifier: BSD-2-Clause-Patent
#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/sha.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <functional>
#include <fstream>
#include "../RootDiagnostics.h"
using IOReturn=uint32_t;
constexpr IOReturn kIOReturnSuccess=0;
constexpr unsigned kDataMax=131072;
static UfsRootDiagnostics gRootDiagnostic;
static uint8_t header[4096],entries[16384],manifest[4096];
// userdata is Android's partition and may hold its own (different) block 0 when a
// separate unixnu partition carries the root image.
static uint64_t userdata=8, unixnu=0;
static uint8_t userdataBlock0[4096];
static bool haveUnixnu;
static unsigned reads;
static bool failRead;
static IOReturn hostRead4k(uint8_t,uint64_t lba,uint32_t blocks,void *out) {
  ++reads; if(failRead) return 1;
  if(lba==1 && blocks==1) memcpy(out,header,4096);
  else if(lba==2 && blocks<=4) memcpy(out,entries,blocks*4096);
  else if(haveUnixnu && lba==unixnu && blocks==1) memcpy(out,manifest,4096);
  else if(lba==userdata && blocks==1) memcpy(out,haveUnixnu?userdataBlock0:manifest,4096);
  else { assert(false); }
  return 0;
}
#include "../RootLayout.h"
static void put32(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(i*8)); }
static void put64(uint8_t *p,uint64_t v) { for(unsigned i=0;i<8;++i)p[i]=uint8_t(v>>(i*8)); }
static void checksums() {
  SHA256(manifest,4064,manifest+4064);
  put32(header+88,rootCrc32(entries,32*128));
  put32(header+16,rootCrc32(header,92,true));
}
static void fixture() {
  memset(header,0,sizeof(header)); memset(entries,0,sizeof(entries)); memset(manifest,0,sizeof(manifest));
  memset(userdataBlock0,0,sizeof(userdataBlock0));
  reads=0; failRead=false; userdata=8; unixnu=0; haveUnixnu=false;
  memcpy(header,"EFI PART",8); put32(header+8,0x10000);put32(header+12,92);
  put64(header+24,1);put64(header+32,511);put64(header+40,6);put64(header+48,506);
  put64(header+72,2);put32(header+80,32);put32(header+84,128);
  entries[0]=1; entries[16]=2;put64(entries+32,8);put64(entries+40,506);
  for(unsigned i=0;i<8;++i)entries[56+2*i]="userdata"[i];
  memcpy(manifest,"SHNGROOT",8);put32(manifest+8,1);put32(manifest+12,2);put64(manifest+16,65536);
  for(unsigned i=0;i<2;++i) {
    uint8_t *e=manifest+24+i*128;strcpy(reinterpret_cast<char *>(e),i?"SystemOS":"OS");
    put64(e+32,4096+i*16384);put64(e+40,8192+512);put32(e+48,512);put32(e+52,1);
  }
  checksums();
}
static void expect(const std::function<void()> &change,bool gpt,uint32_t status,bool rehash=true) {
  fixture();change();if(rehash)checksums();RootLayout layout;const char *why=nullptr;
  bool result=findRootLayout(&layout,&why);
  assert(result==gpt);if(gpt)assert(layout.manifestStatus==status);
  if(!result || status)assert(layout.windows==0);
}
// Turn the fixture into a dual-boot device: userdata shrinks to entry 0 and a
// separate "unixnu" partition (entry 1) carries the root manifest. userdata's
// own first block holds something else, so the test proves the manifest was
// read from unixnu and not from userdata.
static void splitDevice() {
  fixture();                                      // every case starts from a clean device
  unixnu=300; haveUnixnu=true;
  memset(userdataBlock0,0,sizeof(userdataBlock0));
  memcpy(userdataBlock0,"ANDROID!",8);            // no root manifest here
  put64(entries+40,299);                          // userdata 8..299
  entries[128]=1; entries[128+16]=2;
  put64(entries+128+32,300); put64(entries+128+40,506);   // unixnu 300..506
  for(unsigned i=0;i<7;++i)entries[128+56+2*i]="unixnu"[i];
}
int main(int argc,char **argv) {
  assert(rootCrc32(reinterpret_cast<const uint8_t *>("123456789"),9)==0xcbf43926);
  expect([]{},true,0);
  expect([]{header[16]^=1;},false,0,false);
  expect([]{entries[16]^=1;},false,0,false);
  expect([]{put32(header+12,4097);},false,0);
  expect([]{put64(header+24,2);},false,0);
  expect([]{put32(header+20,1);},false,0);
  expect([]{put32(header+80,129);},false,0);
  expect([]{put64(header+32,UINT64_MAX);},false,0);
  expect([]{put64(header+40,2);},false,0);
  expect([]{put64(header+48,511);},false,0);
  expect([]{put64(entries+32,1);},false,0);
  expect([]{put64(entries+40,UINT64_MAX);},false,0);
  expect([]{entries[0]=0;},false,0);
  expect([]{memcpy(entries+128,entries,128);},false,0);
  expect([]{memcpy(entries+128,entries,128);put64(entries+40,100);put64(entries+128+32,101);},false,0);
  expect([]{failRead=true;},false,0);
  expect([]{manifest[0]^=1;},true,1);
  expect([]{put32(manifest+12,5);},true,2);
  expect([]{manifest[4064]^=1;},true,3,false);
  expect([]{put64(manifest+16,UINT64_MAX);},true,4);
  expect([]{put64(manifest+24+32,0);},true,4);
  expect([]{put64(manifest+24+40,UINT64_MAX-511);},true,4);
  expect([]{put64(manifest+24+40,512+1);},true,4);
  expect([]{put64(manifest+24+128+32,8192);},true,6);
  expect([]{strcpy(reinterpret_cast<char *>(manifest+24+128),"OS");},true,5);
  expect([]{memset(manifest+24,'A',32);},true,5);
  expect([]{manifest[24]='\n';},true,5);
  expect([]{manifest[24]=0;},true,5);
  expect([]{put32(manifest+24+48,4096);},true,7);
  expect([]{put32(manifest+24+52,0);},true,7);
  // A final physical 4K read may include padding, but cannot enter another window.
  expect([]{put64(manifest+24+40,4096+512);put64(manifest+24+128+32,8192);},true,6);
  // ---- unixnu root with userdata fallback (dual boot) ---------------------
  // Unsplit device: the manifest lives in userdata and userdata is the root.
  {
    fixture();
    RootLayout layout;const char *why=nullptr;
    assert(findRootLayout(&layout,&why) && layout.manifestStatus==0);
    assert(!layout.rootIsUnixnu);
    assert(layout.rootFirst==layout.userdataFirst && layout.rootLast==layout.userdataLast);
  }
  // Split device: unixnu carries the root image, userdata keeps its own block 0.
  // sheng's store anchor must stay on userdata, never move to unixnu.
  {
    splitDevice(); checksums();
    RootLayout layout;const char *why=nullptr;
    assert(findRootLayout(&layout,&why));
    assert(layout.manifestStatus==0 && layout.windows==2);
    assert(layout.rootIsUnixnu);
    assert(layout.rootFirst==300 && layout.rootLast==506);
    assert(layout.userdataFirst==8 && layout.userdataLast==299);
    assert(layout.rootFirst!=layout.userdataFirst);
    // The manifest decoded from unixnu, not the "ANDROID!" block in userdata.
    assert(layout.windows==2 && !strcmp(layout.window[0].name,"OS"));
  }
  // Windows are read relative to the root partition: userdataFirst + offset
  // would land inside userdata and miss the image.
  {
    splitDevice(); checksums();
    RootLayout layout;const char *why=nullptr;
    assert(findRootLayout(&layout,&why) && layout.manifestStatus==0);
    uint64_t absolute = layout.rootFirst*4096ULL + layout.window[0].offset;
    assert(absolute >= layout.rootFirst*4096ULL);
    assert(absolute < (layout.rootLast+1)*4096ULL);
    assert(absolute != layout.userdataFirst*4096ULL + layout.window[0].offset);
  }
  // A split device whose unixnu block 0 is not a manifest still reports the
  // partition choice, so a flashed-but-empty unixnu is distinguishable.
  {
    splitDevice(); memset(manifest,0,4096); checksums();
    RootLayout layout;const char *why=nullptr;
    assert(findRootLayout(&layout,&why));
    assert(layout.rootIsUnixnu && layout.manifestStatus==1);
  }
  // Manifest bounds are checked against the root partition's own size: a
  // unixnu smaller than userdata must not admit an oversized image.
  {
    splitDevice(); put64(manifest+16,(506-300+1)*4096ULL+4096); checksums();
    RootLayout layout;const char *why=nullptr;
    assert(findRootLayout(&layout,&why));
    assert(layout.rootIsUnixnu && layout.manifestStatus==4);
    assert(layout.windows==0);
  }
  // Duplicate unixnu entries are refused rather than silently picked.
  {
    splitDevice(); entries[256]=1; entries[256+16]=3;
    put64(entries+256+32,507); put64(entries+256+40,510);
    for(unsigned i=0;i<7;++i)entries[256+56+2*i]="unixnu"[i];
    // Widen the disk so the third entry fits below the backup header:
    // last_usable and backup must keep backup - last_usable > table blocks.
    put64(header+48,520); put64(header+32,525);
    checksums();
    RootLayout layout;const char *why=nullptr;
    assert(!findRootLayout(&layout,&why) && !strcmp(why,"duplicate unixnu partition"));
  }
  // Bounded mutation pass checks parser memory safety, not semantic correctness.
  for(unsigned i=0;i<2048;++i) {
    fixture(); unsigned at=(i*313)%4064;manifest[at]^=uint8_t(1+(i%255));checksums();
    RootLayout layout;const char *why=nullptr;(void)findRootLayout(&layout,&why);
  }
  if(argc==2) {
    fixture();std::ifstream f(argv[1],std::ios::binary);f.read(reinterpret_cast<char *>(manifest),4096);assert(f.gcount()==4096);
    userdata=0x2f3840;put64(entries+32,userdata);put64(entries+40,0x3d0abfa);
    put64(header+32,0x3d0abff);put64(header+48,0x3d0abfa);
    // Only the synthetic GPT is rechecksummed. Preserve the actual on-host manifest hash.
    put32(header+88,rootCrc32(entries,4096));put32(header+16,rootCrc32(header,92,true));
    RootLayout layout;const char *why=nullptr;
    assert(findRootLayout(&layout,&why) && layout.manifestStatus==0 && layout.windows==2);
    printf("PASS: existing root image's 4096-byte manifest, OS=%llu SystemOS=%llu; synthetic GPT uses recorded geometry, no full-image rehash\n",
           (unsigned long long)layout.window[0].size,(unsigned long long)layout.window[1].size);
  }
  puts("PASS: GPT CRC/geometry/partition faults, manifest hashes/names/attributes/overlap/overflow, "
       "unixnu root with userdata fallback and store anchor, 2048 bounded mutations");
}
