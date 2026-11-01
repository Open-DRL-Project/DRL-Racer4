"""Build the EC v25 image."""

from pathlib import Path
import os
from link_check import check_sections
import subprocess
import hashlib
import struct
import json

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "ec/src"
OUTPUT = ROOT / "build/ec_v25"
OUTPUT.mkdir(parents=True, exist_ok=True)
TOOLCHAIN = Path(os.environ["ARM_TOOLCHAIN"]) / "bin"


def run(tool, *args):
    result = subprocess.run(
        [str(TOOLCHAIN / ("arm-none-eabi-" + tool + ".exe")), *map(str, args)],
        cwd=OUTPUT,
        capture_output=True,
        text=True,
    )
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout


base = (ROOT / "baselines/ec_v25.bin").read_bytes()
assert (
    hashlib.sha256(base).hexdigest()
    == "12de399f5df589db006ce814af60671119cb4d6f9d179b9dd4a5818c1b3f893d"
)
flags = [
    "-c",
    "-Os",
    "-mthumb",
    "-mcpu=cortex-m4",
    "-ffreestanding",
    "-fno-builtin",
    "-fno-pic",
    "-ffunction-sections",
    "-fdata-sections",
    "-I" + str(SOURCE),
    "-I" + str(ROOT / "build/betaflight/src/main/io"),
]
objects = []
for name in (
    "bridge.c",
    "hook.S",
    "file_forward.c",
    "file_wait.c",
    "dan_hook.S",
    "control_forward.c",
    "smartaudio.c",
    "receiver_tx.c",
):
    obj = Path(name).stem + ".o"
    run("gcc", *flags, SOURCE / name, "-o", obj)
    objects.append(obj)
run(
    "gcc",
    "-nostdlib",
    "-Wl,-T," + str(SOURCE / "bridge.ld"),
    "-Wl,--gc-sections",
    "-Wl,-u,dan_observe_entry",
    "-Wl,-u,sa_irq",
    "-Wl,--defsym,memset=0x15cb5",
    "-o",
    "bridge.elf",
    *objects,
)
run("objcopy", "-O", "binary", "-j", ".text", "bridge.elf", "bridge.bin")
code = (OUTPUT / "bridge.bin").read_bytes()
assert len(code) <= 0x2000
assert base[0x60000:0x62000] == b"\xff" * 0x2000
check_sections(run, "bridge.elf")
(OUTPUT / "symbols.txt").write_text(run("nm", "-n", "bridge.elf"), encoding="utf-8")

symbols = {
    line.split()[2]: int(line.split()[0], 16)
    for line in run("nm", "bridge.elf").splitlines()
    if len(line.split()) == 3
}
out = bytearray(base)
changes = []


def patch(address, before, after, reason):
    assert base[address : address + len(before)] == before, (
        hex(address),
        base[address : address + len(before)].hex(),
    )
    assert len(before) == len(after)
    out[address : address + len(after)] = after
    changes.append(
        {
            "address": hex(address),
            "before": before.hex(),
            "after": after.hex(),
            "reason": reason,
        }
    )


def branch(address, name, before):
    (OUTPUT / "patch.S").write_text(
        f".syntax unified\n.thumb\n.cpu cortex-m4\n.section .text\nb.w 0x{symbols[name]:x}\n"
    )
    run("gcc", "-c", "-mthumb", "-mcpu=cortex-m4", "patch.S", "-o", "patch.o")
    run(
        "ld",
        "-Ttext",
        hex(address),
        "-e",
        hex(address | 1),
        "patch.o",
        "-o",
        "patch.elf",
    )
    run("objcopy", "-O", "binary", "-j", ".text", "patch.elf", "patch.bin")
    patch(address, bytes.fromhex(before), (OUTPUT / "patch.bin").read_bytes(), name)


branch(0x8800, "bridge_entry", "96290446")
branch(0x54A8, "dan_observe_entry", "2de9f041")
branch(0xBE50, "bridge_clear", "10b5037f")
patch(
    0xBDDC,
    bytes.fromhex("a3f12001"),
    bytes.fromhex("194600bf"),
    "ASCII glyph addresses",
)
# Reserve the serial bridge state before the heap and clear it at startup.
for address in (0x12488, 0x19554):
    patch(address, struct.pack("<I", 0x20013720), struct.pack("<I", 0x20014800), "Serial bridge RAM reservation")
for address in (0x4138, 0x413c, 0x4140, 0x4144):
    patch(address, base[address:address + 4], struct.pack("<I", symbols["sa_irq"] | 1), "CTM SmartAudio interrupt")
patch(0x8708, base[0x8708:0x870c], bytes.fromhex("00207047"), "Retire CTM DAN sender")
patch(0x8d70, base[0x8d70:0x8d74], bytes.fromhex("00207047"), "Retire CTM DAN byte output")
out[0x60000 : 0x60000 + len(code)] = code
assert out[:0x4000] == base[:0x4000]
# Include complete changed erase sectors without touching settings or the bootloader.
sectors = sorted(
    {address // 8192 * 8192 for address, (x, y) in enumerate(zip(base, out)) if x != y}
)
assert sectors == [0x4000, 0x8000, 0xA000, 0x12000, 0x18000, 0x60000], sectors
addresses = [address for s in sectors for address in range(s, s + 8192, 256)]
uf2 = b"".join(
    struct.pack(
        "<8I",
        0x0A324655,
        0x9E5D5157,
        0x2000,
        address,
        256,
        i,
        len(addresses),
        0x55114460,
    )
    + out[address : address + 256]
    + bytes(220)
    + struct.pack("<I", 0x0AB16F30)
    for i, address in enumerate(addresses)
)
(OUTPUT / "EC_v25_candidate.bin").write_bytes(out)
(OUTPUT / "EC_v25_code_only.uf2").write_bytes(uf2)
(OUTPUT / "manifest.json").write_text(
    json.dumps(
        {
            "state": "OFFLINE_UNVALIDATED",
            "base_sha256": hashlib.sha256(base).hexdigest(),
            "sha256": hashlib.sha256(out).hexdigest(),
            "bridge_bytes": len(code),
            "patches": changes,
            "sectors": [hex(address) for address in sectors],
        },
        indent=2,
    )
)
print((OUTPUT / "manifest.json").read_text())
