# Kona Cryptographic Primitive Audit

Scope: read-only audit of `Machines/kona.cpp` plus the local reference materials under `references/`. This document does not infer behavior from comments alone when code says otherwise. Any point not derivable from code is marked `无法确认`.

## Sources read

- `Machines/kona.cpp:46-184`
- `Machines/kona.cpp:221-559`
- `Machines/kona.cpp:561-1263`
- `Machines/kona.cpp:1345-1794`
- `Machines/knn-party-offline.cpp:18-123`
- `Machines/knn-party-offline.cpp:126-220`
- `references/cong/cong_topk_no_wrap_simple.py:1-260`
- `references/pcrreduce/pcr_final_carry_reduce_uint64.py:1-220`
- `references/pcrreduce/并行进位加法.md:180-260`

## Share model used in `kona.cpp`

- Arithmetic shares use `Z2<K>` over ring `Z_{2^64}` with additive reconstruction by addition. Evidence:
  - type alias `typedef Z2<K> additive_share` at `Machines/kona.cpp:49`
  - `reveal_one_num_to(Z2<K>)` reconstructs by receiving peer share and returning `tmp + x` at `Machines/kona.cpp:561-576`
- A two-component ABY2-style share is represented as `FixedVec<Z2<K>,2>` at `Machines/kona.cpp:151`.
- The compare outputs are consumed by arithmetic multiplication `mul_vector_additive()` in `SS_scalar()` and `SS_vec()`, so compare outputs are treated as arithmetic ring shares, not XOR shares (`Machines/kona.cpp:1042-1053`, `1059-1063`, `1088-1105`).

## Function-by-function audit

### `KNN_party_base::secure_compare`

- Location: `Machines/kona.cpp:1218-1261`
- Inputs:
  - `x1`, `x2`: arithmetic additive shares in `Z_{2^64}`
  - `greater_than`: public Boolean flag
- Output:
  - one `Z2<K>` arithmetic additive share of a comparison bit
- Exact opened masked value:
  - if `greater_than == true`, code sets
    - `revealed = x2 - x1 + alpha_share` at `Machines/kona.cpp:1227`
  - else
    - `revealed = x1 - x2 + alpha_share` at `Machines/kona.cpp:1228-1230`
  - then both sides exchange and sum shares, so public masked value is
    - `R = (x2 - x1) + alpha` or `R = (x1 - x2) + alpha`
    - from `Machines/kona.cpp:1232-1238`
- DCF post-processing:
  - `u = evaluate(R)`
  - `v = evaluate(R + 2^{K-1})`
  - result share is `b - (v - u + msb(R) ? b : 0)` where `b = m_playerno`
  - from `Machines/kona.cpp:1242-1259`
- Accurate predicate direction:
  - Call sites and swap formulas show this function returns a share of `1` exactly when the first argument should move right in a min-then-max swap.
  - For `greater_than == true`, `SS_scalar()` comment and update rule imply:
    - `u = [x1 > x2]`
    - equality gives `0`
  - Evidence:
    - function comment `x1>x2-->1 ... x1==x2-->0` at `Machines/kona.cpp:1218`
    - swap formula in `SS_scalar`: `x1' = x1 - u*x1 + u*x2`, `x2' = x2 + u*x1 - u*x2` at `Machines/kona.cpp:1042-1048`, `1059-1063`
  - Therefore:
    - `secure_compare(x1, x2, true)` implements `[x1 > x2]`
    - `secure_compare(x1, x2, false)` implements `[x1 < x2]`
    - equal case outputs `0`

### `KNN_party_base::compare_in_vec` for `vector<Z2<K>>`

- Location: `Machines/kona.cpp:839-909`
- Inputs:
  - `shares`: arithmetic additive shares in `Z_{2^64}`
  - `compare_idx_vec`: public even-length index list; pair `2i,2i+1` is one comparison
  - `greater_than`: public Boolean flag
- Output:
  - `compare_res`: arithmetic additive shares of comparison bits, duplicated per pair:
    - `compare_res[2i+1] = compare_res[2i]` at `Machines/kona.cpp:903-906`
- Opened masked values:
  - if `greater_than == true`:
    - `R_i = shares[idx_{2i+1}] - shares[idx_{2i}] + alpha`
    - from `Machines/kona.cpp:852-858`
  - else:
    - `R_i = shares[idx_{2i}] - shares[idx_{2i+1}] + alpha`
    - from `Machines/kona.cpp:860-865`
- Output share formula:
  - same DCF post-processing as `secure_compare()`
  - from `Machines/kona.cpp:882-906`
- Meaning:
  - `compare_res[2i]` is a share of `[shares[idx_{2i}] > shares[idx_{2i+1}]]` when `greater_than == true`
  - equality gives `0`
  - duplication is for later elementwise multiplication alignment in `SS_vec()`

### `KNN_party_base::compare_in_vec` for `vector<array<Z2<K>,2>>`

- Location: `Machines/kona.cpp:914-986`
- Inputs:
  - compares only the first field `shares[*][0]`
- Output:
  - arithmetic additive share of comparison bit, duplicated
- Difference from scalar version:
  - uses `shares[idx][0]` only at `Machines/kona.cpp:930-940`
  - output logic is otherwise identical at `Machines/kona.cpp:959-983`

### `KNN_party_base::mul_additive`

- Location: `Machines/kona.cpp:1109-1127`
- Inputs:
  - `x1`, `x2`: arithmetic additive shares in `Z_{2^64}`
- Output:
  - `res`: arithmetic additive share in `Z_{2^64}`
- Actual formula implemented:
  - code hardcodes `a=b=c=0` at `Machines/kona.cpp:1111`
  - each party sends `(x1-a, x2-b) = (x1, x2)` at `Machines/kona.cpp:1113-1116`
  - then reconstructs `e = x`, `f = y` via `tmp + local` at `Machines/kona.cpp:1117-1122`
  - computes `r = f*a + e*b + c = 0`
  - party 1 adds `e*f = xy` at `Machines/kona.cpp:1123-1125`
  - final shares are therefore:
    - party 0 gets `0`
    - party 1 gets `xy`
- Security consequence:
  - this is not a secure Beaver multiplication as written
  - it publicly reconstructs both multiplicands to both parties during the exchange

### `KNN_party_base::mul_vector_additive`

- Location: `Machines/kona.cpp:1129-1216`
- Inputs:
  - `v1`, `v2`: arithmetic additive shares
  - `double_res == false`: pairwise multiply `v1[i] * v2[i]`
  - `double_res == true`: multiply both halves of `v1` by the same `v2`
- Output:
  - arithmetic additive shares in `res`
- Actual protocol:
  - same as `mul_additive()`: `a=b=c=0` at `Machines/kona.cpp:1134`, `1188`
  - exchanges all masked values, which are actually unmasked local shares because masks are zero
  - reconstructs each operand pair and places full product on party 1 only
  - from `Machines/kona.cpp:1137-1181`, `1190-1212`
- Security consequence:
  - not secure Beaver multiplication as written
  - reveals multiplicands to both parties

### `KNN_party_base::SS_scalar` for `vector<array<Z2<K>,2>>`

- Location: `Machines/kona.cpp:1040-1054`
- Inputs:
  - `shares[first_idx] = (distance_1, label_1)`
  - `shares[second_idx] = (distance_2, label_2)`
  - all four values are arithmetic additive shares
- Comparison bit:
  - `u = secure_compare(distance_1, distance_2, min_then_max)` at `Machines/kona.cpp:1042`
- Output/update type:
  - in-place arithmetic additive shares
- Mathematical formula:
  - using `Y = (u*d1, u*d2, u*l1, u*l2)` from `mul_vector_additive()` at `Machines/kona.cpp:1046`
  - updates are:
    - `d1' = d1 - u*d1 + u*d2`
    - `d2' = d2 + u*d1 - u*d2`
    - `l1' = l1 - u*l1 + u*l2`
    - `l2' = l2 + u*l1 - u*l2`
  - from `Machines/kona.cpp:1047-1053`
- Interpretation:
  - if `u=1`, pair is swapped
  - if `u=0`, pair is unchanged

### `KNN_party_base::SS_scalar` for `vector<Z2<K>>`

- Location: `Machines/kona.cpp:1057-1064`
- Inputs/output:
  - arithmetic additive shares only
- Mathematical formula:
  - `u = secure_compare(x1, x2, min_then_max)` at `Machines/kona.cpp:1059`
  - `x1' = x1 - u*x1 + u*x2`
  - `x2' = x2 + u*x1 - u*x2`
  - from `Machines/kona.cpp:1061-1063`

### `KNN_party_base::SS_vec`

- Location: `Machines/kona.cpp:1084-1106`
- Inputs:
  - `shares[idx][0]`: arithmetic additive share of distance/key
  - `shares[idx][1]`: arithmetic additive share of label/payload
  - `compare_res`: arithmetic additive shares of comparison bits, duplicated per compared pair
- Output:
  - in-place arithmetic additive shares
- Construction:
  - concatenates all distances then all labels into
    - `tmp_ss = [d_0,...,d_{m-1}, l_0,...,l_{m-1}]`
    - from `Machines/kona.cpp:1088-1090`
  - computes
    - `tmp_res = [u_0*d_0, u_1*d_1, ..., u_{m-1}*d_{m-1}, u_0*l_0, ..., u_{m-1}*l_{m-1}]`
    - via `mul_vector_additive(tmp_ss, compare_res, tmp_res, true)` at `Machines/kona.cpp:1091-1092`
- Full mathematical formula for one compared pair `(a,b)` with comparison bit `u`:
  - distance part:
    - `d_a' = d_a - u*d_a + u*d_b`
    - `d_b' = d_b + u*d_a - u*d_b`
    - from `Machines/kona.cpp:1093-1098`
  - label part:
    - `l_a' = l_a - u*l_a + u*l_b`
    - `l_b' = l_b + u*l_a - u*l_b`
    - from `Machines/kona.cpp:1100-1105`
- Therefore `SS_vec` swaps both distance and label under the same secret comparison bit.

### `KNN_party_optimized::fake_load_triples`

- Location: `Machines/kona.cpp:301-374`
- Inputs:
  - no explicit input parameters; uses `num_train_data`, `num_features`, `playerno`
- Output/state:
  - only resizes internal vectors
  - all file reads are commented out
- Effective share contents:
  - vectors are resized and default-initialized; no random/triple material is loaded
  - therefore the subsequent code uses zero-initialized triple material unless some other code writes these arrays
  - no such writes occur before `aby2_share_data_and_additive_share_label_list()` in `run()` (`Machines/kona.cpp:696-698`)

### `KNN_party_optimized::load_triples`

- Location: `Machines/kona.cpp:377-450`
- Inputs:
  - binary files under `./Player-Data/Knn-Data/<dir><dataset>-data/`
- Output/state:
  - party 0 loads:
    - `m_Train_Triples_0`
    - `m_Train_Triples_1`
    - `m_Test_Triples_0`
    - `m_Test_Triples[*][*]`
    - from `Machines/kona.cpp:379-409`
  - party 1 loads:
    - `m_Test_Triples_0`
    - `m_Test_Triples_1`
    - `m_Train_Triples_1`
    - `m_Test_Triples[*][*]`
    - from `Machines/kona.cpp:411-448`

### `KNN_party_optimized::compute_ESD_two_sample`

- Location: `Machines/kona.cpp:532-559`
- Inputs:
  - `m_train_aby2_share_vec[train_idx][j] = (x_j + r^x_{0,j} + r^x_{1,j}, r^x_{b,j})`
  - `m_test_aby2_share_vec[query_idx][j] = (y_j + r^y_{0,j} + r^y_{1,j}, r^y_{b,j})`
  - custom preprocessed share `m_Test_Triples[train_idx][j]`
- Output:
  - `Z2<K>` arithmetic additive share of squared Euclidean distance
- Per-feature formula:
  - let `s0_j = (x_j - y_j) + (r^x_{0,j}+r^x_{1,j}) - (r^y_{0,j}+r^y_{1,j})`
  - let `s1_j = r^x_{b,j} - r^y_{b,j}`
  - code computes:
    - accumulate `tmp_1 += s0_j^2` at `Machines/kona.cpp:538-539`
    - accumulate `res += -2*s0_j*s1_j + t_j` at `Machines/kona.cpp:540`
    - party 1 only adds `tmp_1` at `Machines/kona.cpp:542-545`
  - so overall two-party shares are:
    - party 0 share: `sum_j (-2*s0_j*s1_j + t_{0,j})`
    - party 1 share: `sum_j (s0_j^2 - 2*s0_j*s1_j + t_{1,j})`
  - with preprocessing relation
    - `t_{0,j} + t_{1,j} = (r^y_{0,j}+r^y_{1,j}-r^x_{0,j}-r^x_{1,j})^2`
    - from triple generation at `Machines/kona.cpp:481-487`
  - therefore reconstruction is
    - `(s0_j - s1_j)^2 = (x_j - y_j)^2`
    - summed over `j`

## Answers to the 12 required questions

### 1. Each function’s input, output, and share type

See the function-by-function section above. In short:

- `secure_compare`: arithmetic additive shares in, arithmetic additive share bit out (`Machines/kona.cpp:1218-1261`)
- `compare_in_vec` both overloads: arithmetic additive shares in, arithmetic additive share bits out, duplicated per pair (`Machines/kona.cpp:839-909`, `914-986`)
- `mul_additive`: arithmetic additive shares in/out, but protocol opens multiplicands because `a=b=c=0` (`Machines/kona.cpp:1109-1127`)
- `mul_vector_additive`: arithmetic additive shares in/out, same issue (`Machines/kona.cpp:1129-1216`)
- `SS_scalar`: arithmetic additive shares in-place (`Machines/kona.cpp:1040-1064`)
- `SS_vec`: arithmetic additive shares in-place on both distance and label fields (`Machines/kona.cpp:1084-1106`)
- `fake_load_triples`: state resize only; no actual preprocessing load (`Machines/kona.cpp:301-374`)
- `load_triples`: loads offline binary preprocessing from `Player-Data/Knn-Data/...` (`Machines/kona.cpp:377-450`)
- `compute_ESD_two_sample`: ABY2-style two-component shares plus custom preprocessing in, arithmetic additive distance share out (`Machines/kona.cpp:532-559`)

### 2. Exact predicate direction of `secure_compare`, including equality

- `secure_compare(x1, x2, true)` returns a share of `[x1 > x2]`
- `secure_compare(x1, x2, false)` returns a share of `[x1 < x2]`
- equality returns `0`
- Evidence:
  - code sets `revealed = x2 - x1 + alpha` in the `true` branch (`Machines/kona.cpp:1227`)
  - call sites use the result as a swap bit in `x1' = x1 - u*x1 + u*x2` (`Machines/kona.cpp:1047-1048`, `1062-1063`)
  - function comment also states `x1==x2-->0` (`Machines/kona.cpp:1218`)

### 3. Is `compare_in_vec` output XOR shares or arithmetic shares?

- Arithmetic additive shares over `Z_{2^64}`, not XOR shares.
- Evidence:
  - storage type is `vector<Z2<K>>` (`Machines/kona.cpp:81-82`, `839`, `914`)
  - output reconstructs by addition elsewhere via `reveal_one_num_to()` semantics (`Machines/kona.cpp:561-576`)
  - outputs are fed into `mul_vector_additive()` as arithmetic multiplicands in `SS_vec()` (`Machines/kona.cpp:1091-1092`)

### 4. Does `mul_additive` use Beaver triple, OT, ABY2, or another protocol?

- As implemented, it does not use a real Beaver triple, OT, or ABY2 multiplication.
- It has Beaver-like algebraic shape, but with `a=b=c=0` hardcoded (`Machines/kona.cpp:1111`).
- Therefore it simply exchanges local shares, reconstructs both operands to both parties, and assigns the whole product to party 1 (`Machines/kona.cpp:1113-1125`).
- The same is true of `mul_vector_additive()` (`Machines/kona.cpp:1134`, `1188`).

### 5. Where are triples generated, is a fresh triple consumed per multiplication, and can they be reused?

- For `mul_additive` / `mul_vector_additive`:
  - no triple is generated or loaded at all
  - no fresh multiplication triple is consumed
  - “reuse” is inapplicable because the code uses fixed zero placeholders `a=b=c=0`
- For optimized ESD preprocessing:
  - generated offline by `generate_triples_save_file()` in `Machines/kona.cpp:453-501`
  - also by the standalone generator in `Machines/knn-party-offline.cpp:80-123`
  - files written:
    - `P0-Train-Triples`
    - `P1-Train-Triples`
    - `P0-Test-Triples`
    - `P1-Test-Triples`
  - each train-feature slot `(i,j)` has one precomputed `m_Test_Triples[i][j]` used in one `compute_ESD_two_sample(train_idx=i, query_idx=*)` evaluation at `Machines/kona.cpp:540`
- Reuse of `m_Test_Triples` across different queries:
  - yes, the same `m_Test_Triples[train_idx][j]` is reused for every `query_idx`
  - because `load_triples()/fake_load_triples()` runs once before the query loop (`Machines/kona.cpp:696-698`), while `compute_ESD_for_one_query()` reuses the stored arrays for every test sample (`Machines/kona.cpp:707-713`, `1345-1358`)
  - this reuse is visible in code; whether it is cryptographically intended/safe is `无法确认`

### 6. Full mathematical formula for `SS_vec`

For one public pair `(a,b)` with secret arithmetic comparison bit `u`:

- distance swap:
  - `d_a' = d_a - u*d_a + u*d_b`
  - `d_b' = d_b + u*d_a - u*d_b`
- label swap:
  - `l_a' = l_a - u*l_a + u*l_b`
  - `l_b' = l_b + u*l_a - u*l_b`

Evidence:

- distance update loop at `Machines/kona.cpp:1093-1098`
- label update loop at `Machines/kona.cpp:1100-1105`

Equivalent compact form:

- `d_a' = (1-u)d_a + u d_b`
- `d_b' = u d_a + (1-u)d_b`
- `l_a' = (1-u)l_a + u l_b`
- `l_b' = u l_a + (1-u)l_b`

### 7. Does `SS_vec` swap both distance and label?

- Yes.
- It first packs distances, then appends labels into `tmp_ss` (`Machines/kona.cpp:1088-1090`).
- It multiplies both halves by the same `compare_res` bits with `double_res=true` (`Machines/kona.cpp:1091-1092`).
- It then updates `shares[*][0]` and `shares[*][1]` in two separate loops (`Machines/kona.cpp:1093-1105`).

### 8. Is there any local `if` controlled by a secret bit?

- No explicit branch is directly controlled by a secret-shared bit in the audited code.
- The relevant `if` statements are on:
  - `greater_than` / `min_then_max`: public function arguments (`Machines/kona.cpp:852`, `860`, `928`, `936`, `1228`)
  - `playerno` / `player->my_num()`: public role bits (`Machines/kona.cpp:542`, `1124`, `1162`, `1179`, `1210`, `1352`)
  - `revealed.get_bit(K-1)` or `tmp_res[i].get_bit(K-1)`: bits of the opened masked value after both parties exchange shares (`Machines/kona.cpp:897`, `974`, `1253`)
  - public loop/bookkeeping conditions (`Machines/kona.cpp:1003-1020`)
- Therefore there is no obvious `if(secret_share_bit)` pattern in `kona.cpp`.

### 9. Is there an explicit B2A? If not, why does current comparison not need B2A?

- No explicit B2A, A2B, `daBit`, or `edaBit` exists in the audited code.
- Searches for `B2A`, `A2B`, `daBit`, `edaBit` found no implementation in `Machines/kona.cpp`.
- Current comparison avoids B2A because it never produces XOR/Boolean secret shares as its interface result.
- Instead:
  - inputs are arithmetic shares
  - code opens a masked arithmetic difference `R = delta + alpha`
  - runs `evaluate()` on public `R` using DCF keys
  - directly returns an arithmetic `Z2<K>` share bit `res = m_playerno - r_tmp`
  - from `Machines/kona.cpp:1232-1259`, `1741-1794`
- So the compare primitive is “arithmetic-share in, arithmetic-share out” without a Boolean-share intermediate API.

### 10. Does `fake_load_triples` cause raw data to be sent in plaintext?

- Yes, for the optimized path currently exercised by `run()`.
- `KNN_party_optimized::run()` calls `fake_load_triples()` and then `aby2_share_data_and_additive_share_label_list()` (`Machines/kona.cpp:696-698`).
- Since `fake_load_triples()` only resizes and does not load randomness (`Machines/kona.cpp:301-374`), the triple arrays remain zero-initialized.
- Then:
  - on party 0, training share component sent is
    - `m_sample[i]->features[j] + m_Train_Triples_0[i][j] + m_Train_Triples_1[i][j]`
    - with zero triples this is the plaintext feature
    - `Machines/kona.cpp:240-245`
  - on party 1, test share component sent is
    - `m_test[i]->features[j] + m_Test_Triples_0[j] + m_Test_Triples_1[j]`
    - with zero triples this is the plaintext query feature
    - `Machines/kona.cpp:276-285`
- Therefore `fake_load_triples()` collapses the optimized ABY2-share data exchange into plaintext transmission of original features.

### 11. Is DCF alpha reused across a batch or across multiple calls?

- Yes.
- Every `secure_compare()` and `compare_in_vec()` call reopens the same file `Player-Data/2-fss/r<playerno>` and reads a single value from the start:
  - `Machines/kona.cpp:844-847`
  - `Machines/kona.cpp:919-922`
  - `Machines/kona.cpp:1223-1226`
- `gen_fake_dcf()` writes exactly one share per player file `r0`, `r1`, `r2`:
  - `Machines/kona.cpp:1654-1673`
  - standalone offline generator mirrors this at `Machines/knn-party-offline.cpp:132-154`
- No code advances per-comparison alpha or stores a pool of alphas.
- Therefore the same alpha is reused:
  - within one `compare_in_vec()` batch
  - across repeated `secure_compare()` calls
  - across the whole run unless files are regenerated externally

### 12. Which existing functions can be safely reused for Cong + PCR, and which must be rewritten?

- Can be reused with relatively low risk as structural helpers:
  - `top_1()` network-driving pattern only as a control-flow reference, not as Cong itself (`Machines/kona.cpp:997-1035`)
  - `SS_vec()` swap algebra, because it already implements an arithmetic-share oblivious swap of `(key,payload)` pairs once given an arithmetic comparison bit (`Machines/kona.cpp:1084-1106`)
  - `secure_compare()` / `compare_in_vec()` only if Cong + PCR still wants the same high-level contract “arithmetic-share compare bit from arithmetic-share inputs”, but not if it needs packed Boolean outputs
- Must be rewritten for a real Cong + PCR implementation:
  - `mul_additive()` and `mul_vector_additive()`
    - current code is not secure multiplication; it opens operands (`Machines/kona.cpp:1109-1216`)
    - PCR references explicitly expect real Boolean AND / Beaver or OT style gates:
      - `references/pcrreduce/pcr_final_carry_reduce_uint64.py:52`
      - `references/pcrreduce/pcr_final_carry_reduce_uint64.py:166-186`
      - `references/pcrreduce/并行进位加法.md:233-236`
  - `compare_in_vec()` if Cong wants per-layer GPU/packed batch interfaces like `prepare / exchange / finalize`
    - Cong reference isolates compare-and-swap as one replaceable layer function at `references/cong/cong_topk_no_wrap_simple.py:220-239`
    - PCR reference says real deployment should be split by layers into `prepare / exchange / finalize` at `references/pcrreduce/pcr_final_carry_reduce_uint64.py:51-53`
  - `secure_compare()` if PCR is adopted as the new comparator
    - current implementation is DCF-based arithmetic compare, not PCR/no-wrap/carry-reduction compare
  - Any preprocessing loader:
    - `fake_load_triples()` is unusable for security (`Machines/kona.cpp:301-374`)
    - `load_triples()` / `generate_triples_save_file()` are tailored to the current optimized ESD formula, not to packed Boolean Beaver triples needed by PCR
- Reuse boundary with sources:
  - Cong reference expects a secure `compare_and_swap_level()` plug-in point (`references/cong/cong_topk_no_wrap_simple.py:220-239`)
  - PCR reference expects packed Boolean Beaver triples and layered prepare/exchange/finalize (`references/pcrreduce/pcr_final_carry_reduce_uint64.py:107-157`, `166-186`)
- Bottom line:
  - safe to reuse: swap algebra and some public control scaffolding
  - must rewrite: secure multiplication, compare backend, and preprocessing

## Additional findings

- `fake_load_triples()` is not merely “fake preprocessing”; under the current `run()` path it converts the feature-sharing step into plaintext disclosure of training and query features (`Machines/kona.cpp:696-698`, `221-288`, `301-374`).
- The optimized ESD preprocessing is custom and query-reused; it is not a generic multiplication-triple pool (`Machines/kona.cpp:453-487`, `532-559`).
- The compare path depends on DCF/FSS files `k0/k1/r0/r1/r2` under `Player-Data/2-fss/` and repeatedly reuses the same `r` value (`Machines/kona.cpp:1223-1259`, `1645-1794`).

## Explicit unknowns

- Whether the DCF construction in `evaluate()` is formally correct for the intended predicate from protocol theory alone: `无法确认` from the local code without external proof.
- Whether reuse of the same optimized ESD preprocessing across multiple queries is intended and secure by protocol design: code shows reuse, but security intent is `无法确认`.
- Whether any external setup process rewrites `fake_load_triples()` buffers before `aby2_share_data_and_additive_share_label_list()`: no such path exists in the audited file, so within `kona.cpp` this is `无法确认` and should be treated as absent.
