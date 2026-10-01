import os
import subprocess
import sys
import time
import shutil
from pathlib import Path

def ensure_block_device(rootrun, device):
    path = Path(device)

    if path.exists():
        if not path.is_block_device():
            raise RuntimeError(f"{device} exists but is not a block device")
        return

    sysfs_dev = Path("/sys/class/block") / path.name / "dev"

    for _ in range(50):
        if sysfs_dev.exists():
            break
        time.sleep(0.1)
    else:
        raise RuntimeError(f"Kernel did not create {path.name}")

    major, minor = sysfs_dev.read_text().strip().split(":")

    run(rootrun, "mknod", device, "b", major, minor)

def is_busybox():
    losetup = shutil.which("losetup")
    if not losetup:
        return False

    result = subprocess.run(
        [losetup, "--help"],
        capture_output=True,
        text=True
    )

    return "busybox" in (result.stdout + result.stderr).lower()

def run(*args):
    result = subprocess.run(args, capture_output=True, text=True)

    if result.returncode != 0:
        if result.stderr:
            sys.stderr.write(result.stderr)
        raise RuntimeError(f"command failed: {' '.join(args)}")

    return result.stdout.strip()

def replace():
    print("==> Replacing...")

    rootrun = os.environ.get("RUN_AS_ROOT", "run0")

    kernel = "build/hextra.elf"
    init = "build/userland/init.elf"

    bootx64 = os.environ.get("BOOTX64", "/usr/share/limine/BOOTX64.EFI")

    img = "image/hextra.img"
    mnt = "/tmp/hextra-mnt"
    rt = "/tmp/hextra-root"

    os.makedirs("image", exist_ok=True)

    if not os.path.isfile(kernel):
        sys.stderr.write(f"Error: {kernel} not found – run with -b first\n")
        return 1

    if not os.path.isfile(init):
        sys.stderr.write(f"Error: {init} not found – run with -b first\n")
        return 1

    if not os.path.isfile(bootx64):
        sys.stderr.write(f"Error: {bootx64} not found\n")
        return 1

    loop = None
    mounted_esp = False
    mounted_root = False

    try:
        isBusyBox = is_busybox()

        if isBusyBox:
            print("System is using BusyBox")
            loop = run(rootrun, "losetup", "-f")
            run(rootrun, "losetup", "-P", loop, img)
        else:
            print("System is using something else")
            loop = run(rootrun, "losetup", "--find", "--show", "--partscan", img)

        time.sleep(0.5)

        esp = f"{loop}p1"
        root = f"{loop}p2"

        ensure_block_device(rootrun, esp)
        ensure_block_device(rootrun, root)

        run(rootrun, "mkdir", "-p", mnt)
        run(rootrun, "mkdir", "-p", rt)

        run(rootrun, "mount", esp, mnt)
        mounted_esp = True

        run(rootrun, "mkdir", "-p", f"{mnt}/EFI/BOOT", f"{mnt}/boot")

        run(rootrun, "cp", bootx64, f"{mnt}/EFI/BOOT/BOOTX64.EFI")

        run(rootrun, "cp", kernel, f"{mnt}/boot/hextra.elf")

        run(rootrun, "mount", root, rt)
        mounted_root = True

        run(rootrun, "mkdir", "-p", f"{rt}/bin")

        run(rootrun, "cp", "build/userland/init.elf", f"{rt}/bin/init")

        run(rootrun, "cp", "build/userland/shell/main.elf", f"{rt}/bin/sh")
        run(rootrun, "cp", "build/userland/shell/yes.elf", f"{rt}/bin/yes")
        run(rootrun, "cp", "build/userland/shell/libctest.elf", f"{rt}/bin/libctest")

        conf_path = "/tmp/hextra-limine.conf"

        with open(conf_path, "w") as f:
            f.write(
                "timeout: 1\n"
                "\n"
                "/Hextra\n"
                "    protocol: limine\n"
                "    path: boot():/boot/hextra.elf\n"
            )

        run(rootrun, "cp", conf_path, f"{mnt}/limine.conf")

        run("sync")

    except RuntimeError as e:
        sys.stderr.write(f"Error: {e}\n")
        return 1

    finally:
        if mounted_root:
            subprocess.run([rootrun, "umount", rt], check=False)

        if mounted_esp:
            subprocess.run([rootrun, "umount", mnt], check=False)

        if loop:
            subprocess.run([rootrun, "losetup", "-d", loop], check=False)

        subprocess.run(["sync"], check=False)

    print(f"==> Image ready: {img}")
    return 0

if __name__ == "__main__":
    sys.exit(replace())
