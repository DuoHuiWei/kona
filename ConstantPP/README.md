# ConstantPP (Kona unified-framework reimplementation)

This directory is an isolated reimplementation of the core protocols from:

**Constant-Round Privacy-Preserving KNN Classification Based on Function Secret Sharing**

The goal is not to copy the upstream single-process functional simulation.
The goal is to preserve the paper protocol while using Kona's real
two-party networking and `Z2<64>` ring implementation.

## Current scope

Implemented in this first slice:

- `constantpp-types.hpp`
  - `Z2<64>` aliases
  - SecED and SecShuffle preprocessing material structures
  - lightweight component-local communication counters
- `constantpp-comm.hpp`
  - batched `octetStream` pack/send/receive helpers over
    `RealTwoPartyPlayer`
- `sec-ed.hpp/.cpp`
  - real two-party SecED
  - one batched online communication stage
- `sec-shuffle.hpp/.cpp`
  - real two-party Algorithm 5 SecShuffle
  - two dependent online communication stages
  - aligned multi-column shuffle so distance and label keep the same hidden
    permutation

Not implemented yet:

- SecBCom
- SecKMin
- SecEqTest / SecFre / SecMax
- end-to-end SecKC benchmark

## SecED compatibility choice

The paper's Algorithm 4 describes a vector `a=(a_1,...,a_m)` and
`c_i=a_i^2`, suggesting per-coordinate preprocessing.

The upstream `constantPP-KNN/src/secED.cpp`, however, calls `secED_1Dim`
with the same `triple_shares` for every coordinate.  For this baseline
version we intentionally preserve that reuse behavior: one `(a,c=a^2)`
share pair is reused across dimensions.

This was an explicit compatibility decision for the Kona reimplementation.
It should be documented in any final experiment report because it is weaker
than independent per-coordinate masks.

Unlike the upstream functional simulation, this implementation never holds
both online parties' shares in the same SecED call.  Each process computes
its own `[e]_p`, packs all dimensions, exchanges them with the peer, opens
only the masked `e`, and locally computes its output distance share.

## SecShuffle

SecShuffle follows Algorithm 5:

Offline dealer:

1. sample independent permutations `pi0`, `pi1`;
2. sample masking shares `[a]_0`, `[a]_1`;
3. prepare correction shares satisfying

   `b0 + b1 = pi0(pi1(a0) + a1)`.

Online:

1. `S0 -> S1`: `[x']_0 = [x]_0 + [a]_0`;
2. `S1 -> S0`: `x'' = pi1([x']_0 + [x]_1) + [a]_1`;
3. S0 outputs `[y]_0 = pi0(x'') - [b]_0`;
4. S1 outputs `[y]_1 = -[b]_1`.

The reconstructed result is `pi0(pi1(x))`.

The implementation supports multiple aligned columns with the same hidden
permutation.  This is needed by SecKMin so a distance and its label cannot
be separated by shuffling.

## Communication accounting

`ProtocolStats` is deliberately component-local:

- `payload_bytes_sent`
- `send_calls`
- `receive_calls`
- `logical_rounds`

For formal benchmark numbers, continue to use Kona's existing player-wide
communication statistics/timers.  The local counters are primarily for
component correctness and protocol-stage sanity checks.

Expected logical rounds:

- SecED: 1
- SecShuffle: 2

Logical rounds are counted at the protocol layer rather than inside generic
send/receive helpers. This keeps protocol dependency rounds separate from
transport chunking or future buffer-size limits.

## Build integration

These files are intentionally self-contained under `ConstantPP/` and do
not modify existing Kona protocol code.

A later benchmark runner can compile them together with the existing Kona
networking/runtime, for example by adding:

- `ConstantPP/sec-ed.cpp`
- `ConstantPP/sec-shuffle.cpp`

to a dedicated ConstantPP executable.

Do not move these implementations into existing Kona/Cong protocol files.


## Correctness tests

Two real-network, two-process correctness tests are provided under
`ConstantPP/tests/`:

- `test-sec-ed.cpp`
  - public fixture `x=[10,20,30,40]`, `y=[7,25,28,35]`
  - expected squared distance: `63`
  - uses the same reused `(a,c)` compatibility model as `sec_ed_n_dim`
  - opens the output only after the protocol call
  - expects `sec_ed_logical_rounds=1`

- `test-sec-shuffle.cpp`
  - shuffles aligned value/label columns
  - checks the reconstructed output equals `pi0(pi1(input))`
  - explicitly checks value-label pairing after shuffle
  - opens outputs only after the protocol call
  - expects `sec_shuffle_logical_rounds=2`

The tests deliberately use deterministic public fixtures and deterministic
preprocessing shares. They are correctness tests, not security benchmarks.
The online protocol functions still receive only one party's local shares.
