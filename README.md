# NativeUfs

Native UFS storage driver. Owns the host controller, SMMU, queues and the root read path.

Current state: the native UFS root works on sheng.

Linked into XNU by `unixnu build kernel`.

## Amethyst split-device safety

When GPT contains `unixnu`, root reads use it, but persistent Data and
log/export opens are disabled: Android `userdata` contains F2FS and is not
a raw storage allocation. `rootStoreGeometry` returns invalid, zeroed geometry
for split devices; both open paths refuse before opening a write window.
Unsplit layouts retain the existing same-partition root/persistence policy.
Do not enable split-device persistence until a separate region's ownership,
bounds, and recovery procedure are validated. A Partitioner `reserve_mib` tail
gap is not a reservation inside Android F2FS and grants no write permission.

Host check (no hardware validation implied):

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/RootLayout.cpp -lcrypto -o /path/to/owned-scratch/rootlayout
/path/to/owned-scratch/rootlayout
python3 tests/check_split_persistence.py /path/to/owned-scratch
```
