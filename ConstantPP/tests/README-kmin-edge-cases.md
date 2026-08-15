# SecKMin edge cases

The formal benchmark path is `1 <= k < n` and should report 5 logical rounds.

The implementation also handles:
- `k == 0`: returns an empty result without protocol work.
- `k == n`: performs the secure shuffle and returns all shuffled labels,
  avoiding unsigned underflow in `n-k-1`.

These edge cases are functional guards, not benchmark configurations.
