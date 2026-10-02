import os
import subprocess
import sys


def start():
    print("==> Starting QEMU...")

    codefd = os.environ.get("CODEFD", "/usr/share/edk2/OvmfX64/OVMF_CODE.fd",)
    iso = os.environ.get("ISO", "build/hextra.iso")

    cmd = [
        "qemu-system-x86_64",

        "-machine", "q35",
        "-m", "512M",

        "-drive",
        f"if=pflash,format=raw,readonly=on,file={codefd}",

        "-cdrom", iso,

        "-serial", "stdio",

        "-boot", "menu=on",

        "-no-reboot",
        "-no-shutdown",
    ]

    print("==>", " ".join(cmd))

    result = subprocess.run(cmd)

    if result.returncode != 0:
        sys.stderr.write("Error: Failed to start QEMU\n")
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(start())
