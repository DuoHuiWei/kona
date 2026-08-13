"""Small helpers for local XOR-share reconstruction in tests/simulators."""


def reconstruct_xor(x0, x1):
    """Reconstruct XOR shares."""
    return x0 ^ x1


def bool_reconstruct(x_share):
    """Reconstruct a pair-form XOR share."""
    x0, x1 = x_share
    return reconstruct_xor(x0, x1)
