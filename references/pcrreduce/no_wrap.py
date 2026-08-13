"""No-wrap secret sharing helpers for integer GPU experiments.

Subtractive shares use x = x1 - x0 over ordinary int64 arithmetic. Callers are
responsible for keeping values inside a range that cannot overflow int64.
"""

import cupy as cp


def additive_share(x, rng, share_bound=2**30):
    """Create additive shares satisfying x = x0 + x1."""
    x = cp.asarray(x, dtype=cp.int64)
    x0 = rng.randint(0, share_bound, size=x.shape, dtype=cp.int64)
    x1 = x - x0
    return x0, x1


def additive_reconstruct(x0, x1):
    """Reconstruct additive shares."""
    return x0 + x1


def additive_add(x0, x1, y0, y1):
    """Add two additive-shared values."""
    return x0 + y0, x1 + y1


def additive_sub(x0, x1, y0, y1):
    """Subtract two additive-shared values."""
    return x0 - y0, x1 - y1


def subtractive_share_no_wrap(x, rng, share_bound=2**30):
    """Create subtractive shares satisfying x = x1 - x0."""
    x = cp.asarray(x, dtype=cp.int64)
    x0 = rng.randint(0, share_bound, size=x.shape, dtype=cp.int64)
    x1 = x0 + x
    return x0, x1


def subtractive_reconstruct_no_wrap(x0, x1):
    """Reconstruct no-wrap subtractive shares."""
    return x1 - x0


def subtractive_add(x0, x1, y0, y1):
    """Add two no-wrap subtractive-shared values."""
    return x0 + y0, x1 + y1


def subtractive_sub(x0, x1, y0, y1):
    """Subtract two no-wrap subtractive-shared values."""
    return x0 - y0, x1 - y1


def share_public_constant(value, count, rng):
    """Create independent subtractive shares of the same public constant."""
    plain = cp.full(count, value, dtype=cp.int64)
    return subtractive_share_no_wrap(plain, rng)


subtractive_share = subtractive_share_no_wrap
subtractive_reconstruct = subtractive_reconstruct_no_wrap
