import os
import subprocess
import sys

def start():
    print("==> Starting QEMU...")
    codefd = os.environ.get("CODEFD", "/usr/share/OVMF/OVMF_CODE.fd")
    img = os.environ.get("IMG", "image/uorix.img")

    cmd = [
        "qemu-system-x86_64",
        "-machine", "pc",
        "-m", "512M",
        "-drive", f"if=pflash,format=raw,readonly=on,file={codefd}",
        "-drive", f"format=raw,file={img}",
        "-serial", "stdio",
        "-no-reboot", "-no-shutdown"
    ]

    result = subprocess.run(cmd)
    if result.returncode != 0:
        sys.stderr.write("Error: Failed to start QEMU\n")
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(start())
