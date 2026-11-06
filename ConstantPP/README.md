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


## Second slice: SecMul and SecBCom material/API skeleton

### SecMul

`sec-mul.hpp/.cpp` implements real two-party Beaver multiplication over
`Z2<64>`. The online stage opens `e=x-a` and `f=y-b` in one batched
communication stage and computes additive output shares locally.

The upstream ConstantPP source reuses one triple across vector elements.
This reimplementation exposes that behavior explicitly as
`sec_mul_vector_reuse()`, while also providing `sec_mul_vector_fresh()` for
one-triple-per-element execution.

### SecBCom

`sec-bcom-material.hpp/.cpp` and `sec-bcom.hpp/.cpp` establish the
Algorithm-7 boundary without pretending that Kona's current `Compare64`
already provides pair-specific ConstantPP material.

The first faithful plan allocates one material slot for every ordered pair
`(i,j), i!=j`, with threshold:

`r_ij = r_i - r_j`.

The online shell already implements the paper's one communication stage:

1. `[Delta_i]_p = [x_i]_p + [r_i]_p`;
2. batch-open all `Delta_i`;
3. evaluate pair-specific DCF locally on `Delta_i-Delta_j`;
4. sum local output shares into `[su_i]_p`.

`SecBComDcfBackend` is deliberately opaque. 
The upper-triangular reverse-share shortcut used by the upstream GitHub
source is intentionally not enabled yet. It can be added only after its
complement relation is proved against the chosen DCF backend.


## SecBCom engineering decision: upper triangle + Kona DCF

The finalized first SecBCom implementation follows the ConstantPP algorithmic
structure but deliberately reuses Kona's existing secure DCF comparison
primitive.

What remains ConstantPP:

- all-pairs rank/count semantics;
- one comparison stage independent of `n`;
- only the upper triangle `i<j` is evaluated;
- each unordered pair updates both rank counters;
- total secure comparison pairs are `n(n-1)/2`.

What is reused from Kona:

- `KonaDcfCompare::Compare64<64>`;
- `Z2<64>`;
- `RealTwoPartyPlayer`;
- Kona's masked-opening communication;
- Kona's DCF/FSS evaluation path;
- Kona's batching and `MAX_COMPARE_CHUNK` transport behavior.

No private `x_i` or `x_j` is reconstructed in SecBCom.

The public ConstantPP source uses an upper-triangle complement trick. We
retain that engineering behavior. For one secret comparison bit `[b]`,
the reverse direction is represented as `[1-b]`, with public one shared as
`(1,0)`. This also gives a deterministic source-like total ordering for
equal values.

`MAX_COMPARE_CHUNK` affects transport chunking only. The ConstantPP logical
round count for SecBCom remains one comparison stage.

The Kona comparator internally performs the DCF work required by its signed
64-bit comparison construction; `SecBComStats` exposes the resulting Kona
DCF evaluate-call count rather than pretending it equals the number of
unordered pairs.


## SecKMin

`sec-kmin.hpp/.cpp` implements the ConstantPP k-minimum label stage using:

1. real two-party SecShuffle on aligned `(distance,label)` shares;
2. upper-triangle ConstantPP SecBCom with Kona DCF;
3. secure Kona-DCF threshold comparison for
   `u_i = 1{su_i > n-k-1}`;
4. real opening of only the final indicator bits `u_i`;
5. local selection of the corresponding shuffled label shares.

Expected ConstantPP logical rounds:

- SecShuffle: 2
- SecBCom: 1
- threshold comparison: 1
- indicator opening: 1
- total SecKMin: 5

Engineering note: the paper describes masking `su_i` with a random `r`
and evaluating a threshold-shifted DCF. In the unified Kona implementation
the same secure predicate is realized directly with Kona Compare64 on
secret-shared `su_i` against a secret sharing of the public threshold.
`su_i` itself is never reconstructed before the indicator stage. This keeps
the comparison secure and preserves the one-stage round structure while
using the exact same DCF implementation as the Kona baseline.


## Baseline policy: repository logic + Kona secure engineering

This reimplementation uses the following rule consistently:

- **Algorithm/control-flow semantics follow the public `constantPP-KNN`
  repository**, including upper-triangular all-pairs rank counting and
  reuse of one comparator/FSS key object across a batch of comparisons.
- **Security, networking, batching, and benchmark engineering follow Kona**:
  `Z2<64>`, `RealTwoPartyPlayer`, `octetStream`, Kona DCF, real two-process
  communication, and Kona's communication/timing counters.
- The repository's single-process shortcuts that reconstruct both parties'
  shares or perform plaintext `<`, `>`, or `==` are never copied.
- The paper's pair-specific DCF-key description is therefore not implemented
  literally in this unified-framework baseline; the public repository's
  key-reuse engineering behavior is the baseline target.

For SecBCom this means:

- evaluate only `i < j`, so there are `n(n-1)/2` secure pair comparisons;
- one secret comparison share updates both rank counters;
- one Kona `Compare64<64>` instance/key-cache is reused over the comparison
  batch;
- no private `x_i` or `x_j` is reconstructed.

For SecKMin this means:

- reuse the same Kona GT-comparator object across all `n` threshold tests;
- securely evaluate `u_i = 1{su_i > n-k-1}`;
- open only the final indicator bits `u_i`;
- normal benchmark cases `1 <= k < n` execute the full five-round path.


## v0.4: SecEqTest, SecFre, SecMax

### SecEqTest

The public repository uses one Eq-FSS key pair repeatedly over a batch.
Kona currently exposes the validated DCF comparator rather than the
repository's DPF equality primitive, so this unified implementation computes

`1{x=y} = 1 - 1{x<y} - 1{y<x}`

with two strict Kona comparisons packed into one `Compare64` call.

Consequences:

- no plaintext equality test;
- real two-process communication;
- one ConstantPP logical equality round;
- two Kona secure-comparison pairs per equality predicate;
- therefore equality is somewhat heavier than the repository's native
  Eq-FSS evaluator. This must be reported when interpreting absolute
  performance.

### SecFre

The repository's upper-triangle frequency logic is preserved exactly:

- compare labels only for `i<j`;
- one equality result increments both frequency shares;
- each position gets a public self-count of one.

All pair equalities are batched into one SecEqTest stage, so normal `k>=2`
SecFre contributes one logical round.

### SecMax

The repository control flow is preserved:

1. `SecBCom(frequency, Greater)`;
2. `SecEqTest(su_i, k-1)`;
3. `SecMul(label_i, indicator_i)`;
4. local sum.

The same Beaver triple is reused for the vector multiplication, matching the
repository engineering behavior accepted by this baseline. For normal
`k>=2`, SecMax contributes exactly three logical rounds.


## v0.4.1 static-review fixes

- `sec-eq.cpp` now uses the correct nested Kona stats type:
  `KonaDcfCompare::Compare64<RING_BITS>::Stats`.
- `test-sec-max.cpp` now uses different labels for tied maximum-frequency
  entries, so the test validates that the source-like upper-triangle
  complement rule selects exactly one deterministic winner instead of
  passing merely because all tied entries happened to share the same label.


## v0.5: SecKC end-to-end pipeline

SecKC combines all ConstantPP components:

1. SecED
2. SecKMin
3. SecFre
4. SecMax

The expected normal execution path is:

```
SecED   : 1 logical round
SecKMin : 5 logical rounds
SecFre  : 1 logical round
SecMax  : 3 logical rounds

Total   : 10 logical rounds
```

The implementation keeps ConstantPP algorithm structure while using Kona
two-party communication and DCF primitives.

## SecMax local-FSS performance mode (2026-11-04)

Only the SecMax rank-comparison stage has a new optional path. SecFre,
distance, Top-k, the shared Kona/PCR components, the later maximum-indicator
equality and label multiplication remain unchanged. The output remains the
winning label's additive share; the data bench reveals it only for validation
after online timing and communication snapshots.

`constantpp-test-sec-kc-data.x` defaults to
`--secmax-mode paper-local-l2`. Select `--secmax-mode legacy` for the previous
path. Existing component API callers retain the legacy default unless they
explicitly supply the new `SecMaxLocalFssL2` adapter.

The adapter is confined to `secmax-local-fss-l2.hpp`. It exchanges the k
frequency shares once (8k sent bytes per party), then performs actual local
DCF evaluations for the unordered pairs. It preserves the existing reverse
complement and later-index tie rule. There are k(k-1) real local FSS evaluations;
local batching at 16384 pairs introduces no further communication. Parsing,
key-cache initialization and pair planning occur before the online timer.

This is an explicitly authorized **performance-only, insecure L2 mode**:
frequency masks are zero, both test r shares are read offline and one test
key cache is reused. It reconstructs frequencies and provides no privacy.
The local evaluator was extracted from the existing private Kona evaluator
without modifying the shared component. This is not a security implementation
of the paper's pair-specific key preprocessing, nor an exact reproduction of
its computation cost. In particular, SecFre and maximum-indicator equality
still use the existing Kona-based equality, rather than native paper DPF.

Raw output records `secmax_mode`, `secmax_security`, rank-stage sent bytes,
transport actions, exchange count and local FSS calls/time. Local calls are
also included in vote/online evaluation totals. Rank byte/action/call counts
and full stage accounting are checked after the online timer.

Small two-party regressions cover k=1,2,3,5,20,100 and compare legacy/new
labels, accounting and unchanged distance/Top-k counters. For k>=2, total
vote bytes per party change from `12k²+20k` to `8k²+32k`; k=1 retains its
zero-communication shortcut. Thus k=2 uses more bytes and k=3 is equal;
the reduction starts at k=4. The normal SecFre/SecMax logical rounds remain
1+3. Results: `KNN-experiment-res/constantpp-secmax-local-l2-20261104/`.
Historical results collected with the legacy path must remain distinguished
from this new mode.
