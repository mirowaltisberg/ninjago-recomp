import os
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))

GUEST_SO = os.path.join(ROOT, "input", "extracted", "lib", "armeabi-v7a", "libLEGO_M1.so")
BASE = 0x00100000


@pytest.fixture(scope="session")
def guest_elf():
    if not os.path.exists(GUEST_SO):
        pytest.skip("guest library not present")
    from recomp.elfimage import GuestElf
    return GuestElf.load(GUEST_SO, BASE)


@pytest.fixture(scope="session")
def program(guest_elf):
    from recomp.discover import discover
    return discover(guest_elf)
