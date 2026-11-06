# Reduced-depth Cong v1 — smoke-test drop-in

Files:

- `Machines/kona-reduced-depth-cong-topk.hpp`
- `Machines/kona-reduced-depth-cong-topk-smoke.cpp`

This implements the frozen **standard power-of-two partition + matrix finalization**
construction only. It deliberately does **not** alter:

- `kona-cong-topk.hpp`
- `kona-dcf-compare.hpp`
- `kona-pcr-compare.hpp`
- `Networking/Player.h`
- `Math/Z2k.hpp`

## Important execution distinction

The per-block phase reuses the existing `KonaCongTopK::build_cong_network()`.

The final matrix is stored as all unordered candidate pairs, but it is **not**
a `CongLevel`: matrix comparisons share candidate wires and must be evaluated
from one immutable snapshot, then converted to ranks / Top-k selection.

Therefore this smoke test validates construction and semantics, not the final
secure DCF/PCR rank-selection backend yet.

## Suggested repository placement

Copy:

```text
kona-reduced-depth-cong-topk.hpp
    -> Machines/kona-reduced-depth-cong-topk.hpp

kona-reduced-depth-cong-topk-smoke.cpp
    -> Machines/kona-reduced-depth-cong-topk-smoke.cpp
```

Then add a Makefile target using the same C++ flags/include paths as the existing
Cong network tests.

A direct compile may work if your repository's existing include environment is
already configured:

```bash
g++ -std=c++17 -O2 -I. \
    Machines/kona-reduced-depth-cong-topk-smoke.cpp \
    -o kona-reduced-depth-cong-topk-smoke.x
./kona-reduced-depth-cong-topk-smoke.x
```

Expected final line:

```text
SMOKE_TEST=PASS
```
