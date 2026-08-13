"""Shared bit and packed-bit helpers for CuPy arrays."""

import cupy as cp


def int_to_bits(x, bit_length=64):
    """Convert uint64 values to little-endian bit rows."""
    x = cp.asarray(x, dtype=cp.uint64)
    shifts = cp.arange(bit_length, dtype=cp.uint64)
    return ((x[:, None] >> shifts[None, :]) & cp.uint64(1)).astype(cp.uint8)


def bits_to_int(bits):
    """Convert little-endian bit rows back to uint64 values."""
    bits = cp.asarray(bits, dtype=cp.uint64)
    bit_length = bits.shape[1]
    weights = cp.uint64(1) << cp.arange(bit_length, dtype=cp.uint64)
    return cp.sum(bits * weights[None, :], axis=1, dtype=cp.uint64)


def bits_xor(a, b):
    return cp.bitwise_xor(a, b)


def bits_and(a, b):
    return cp.bitwise_and(a, b)


def bits_or(a, b):
    return cp.bitwise_or(a, b)


def bits_not(a):
    return cp.bitwise_xor(a, 1)


def get_bit(x, bit_index):
    """Extract one bit from uint64 values and return uint8 0/1 values."""
    x = cp.asarray(x, dtype=cp.uint64)
    return ((x >> cp.uint64(bit_index)) & cp.uint64(1)).astype(cp.uint8)


def get_bit_u64(x, bit_index):
    """Extract one bit from uint64 values and keep uint64 0/1 values."""
    x = cp.asarray(x, dtype=cp.uint64)
    return (x >> cp.uint64(bit_index)) & cp.uint64(1)


def left_shift(x, shift):
    return cp.asarray(x, dtype=cp.uint64) << cp.uint64(shift)


def right_shift(x, shift):
    return cp.asarray(x, dtype=cp.uint64) >> cp.uint64(shift)


def group_count(n, word_bits=32):
    """Return ceil(n / word_bits), used for packed bit-sliced words."""
    if n < 0:
        raise ValueError("n must be non-negative")
    return (n + word_bits - 1) // word_bits


def last_group_mask(n, word_bits=32, dtype=cp.uint32):
    """Mask valid lanes in the final packed word."""
    remainder = n % word_bits
    if remainder == 0:
        return dtype((1 << word_bits) - 1)
    return dtype((1 << remainder) - 1)


def unpack_packed_bits(words, n, word_bits=32):
    """Unpack packed uint32 bit lanes to a length-n uint64 0/1 array."""
    words = cp.asarray(words, dtype=cp.uint32)
    if n == 0:
        return cp.empty(0, dtype=cp.uint64)

    index = cp.arange(n, dtype=cp.uint32)
    shift = 5 if word_bits == 32 else int(word_bits).bit_length() - 1
    group = index >> cp.uint32(shift)
    lane = index & cp.uint32(word_bits - 1)
    return ((words[group] >> lane) & cp.uint32(1)).astype(cp.uint64)

