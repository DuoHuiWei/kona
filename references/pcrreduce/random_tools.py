"""CuPy random helpers shared by the GPU MPC prototypes."""

import cupy as cp


def make_rng(seed=None):
    """Return a CuPy RNG. Passing an integer seed makes runs reproducible."""
    return cp.random.RandomState(seed)


def random_u64(shape, rng):
    """Generate full-width uint64 random words on GPU."""
    low = rng.randint(0, 2**32, size=shape, dtype=cp.uint64)
    high = rng.randint(0, 2**32, size=shape, dtype=cp.uint64)
    return (high << cp.uint64(32)) | low


def random_u32(shape, rng):
    """Generate full-width uint32 random words on GPU."""
    return rng.randint(0, 2**32, size=shape, dtype=cp.uint64).astype(cp.uint32)


def random_bit(shape, rng, dtype=cp.uint64):
    """Generate random 0/1 bits using the requested CuPy integer dtype."""
    return rng.randint(0, 2, size=shape, dtype=dtype)

