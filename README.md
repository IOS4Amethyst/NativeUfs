# NativeUfs

Native UFS storage driver. Owns the host controller, SMMU, queues and the root read path.

Current state: the native UFS root works on sheng.

Linked into XNU by `unixnu build kernel`.

Amethyst controller adoption is not hardware-qualified. Its board defaults keep
root mode and probes off. A read-only SMMU plan now precedes firmware queue stop;
failed mapping or an incompatible plan returns before queue writes. A second
validation still occurs before SMMU configuration; failures after stopping the
queue do not yet have a validated recovery/rollback path.

Queue-stop timeout, descriptor-address readback failure and queue-start timeout
quarantine the transport (`poisoned=true`, `ready=false`). Subsequent queue
stop/start calls refuse before MMIO. This is fail-closed handling, not rollback.
Host fault-injection check:

```sh
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined tests/RootQueueFailure.cpp -o /path/to/owned-scratch/queue-failure
/path/to/owned-scratch/queue-failure
```

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
python3 tests/check_takeover_preflight.py /path/to/owned-scratch
```
