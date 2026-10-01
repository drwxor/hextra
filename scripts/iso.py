from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from typing import NoReturn
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_KERNEL = ROOT / "build" / "hextra.elf"
DEFAULT_USERLAND = ROOT / "build" / "userland"
DEFAULT_SHELL_DIR = DEFAULT_USERLAND / "shell"
DEFAULT_INIT = DEFAULT_USERLAND / "init.elf"
DEFAULT_ISO_ROOT = ROOT / "build" / "iso"
DEFAULT_OUTPUT = ROOT / "build" / "hextra.iso"

LIMINE_FILES = (
    "limine-bios.sys",
    "limine-bios-cd.bin",
    "limine-uefi-cd.bin",
    "BOOTX64.EFI",
)


class IsoError(RuntimeError):
    pass


def die(message: str) -> NoReturn:
    raise IsoError(message)


def run(command: list[str]) -> None:
    print("[ISO]", " ".join(command))
    try:
        subprocess.run(command, check=True)
    except FileNotFoundError:
        die(f"required command not found: {command[0]}")
    except subprocess.CalledProcessError as exc:
        die(f"command failed with exit status {exc.returncode}: {' '.join(command)}")


def require_file(path: Path, description: str) -> None:
    if not path.is_file():
        die(f"{description} not found: {path}")


def check_elf(path: Path, description: str) -> None:
    require_file(path, description)
    try:
        with path.open("rb") as file:
            magic = file.read(4)
    except OSError as exc:
        die(f"cannot read {description} {path}: {exc}")

    if magic != b"\x7fELF":
        die(f"{description} is not an ELF file: {path}")


def find_limine_assets(explicit: Path | None) -> Path:
    candidates: list[Path] = []

    if explicit is not None:
        candidates.append(explicit)

    env = os.environ.get("LIMINE_PATH")
    if env:
        candidates.append(Path(env).expanduser())

    # Common layouts when building Limine from source or unpacking a release.
    candidates.extend(
        [
            ROOT / "limine",
            ROOT.parent / "limine",
            Path("/usr/local/share/limine"),
            Path("/usr/share/limine"),
        ]
    )

    for candidate in candidates:
        if not candidate.is_dir():
            continue
        if all((candidate / name).is_file() for name in LIMINE_FILES):
            return candidate

    expected = ", ".join(LIMINE_FILES)
    searched = "\n".join(f"  {path}" for path in candidates)
    die(
        "could not find a Limine binary directory containing "
        f"{expected}.\nSearched:\n{searched}\n"
        "Set LIMINE_PATH or pass --limine /path/to/limine."
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build a BIOS/UEFI hybrid Hextra ISO from build/hextra.elf."
    )
    parser.add_argument(
        "--limine",
        type=Path,
        help="directory containing Limine BIOS/UEFI binaries",
    )
    parser.add_argument(
        "--kernel",
        type=Path,
        default=DEFAULT_KERNEL,
        help="kernel ELF (default: build/hextra.elf)",
    )
    parser.add_argument(
        "--shell-dir",
        type=Path,
        default=DEFAULT_SHELL_DIR,
        help="directory containing all shell userland files "
        "(default: build/userland/shell)",
    )
    parser.add_argument(
        "--init",
        dest="init_elf",
        type=Path,
        default=DEFAULT_INIT,
        help="userspace init ELF to copy into the ISO "
        "(default: build/userland/init.elf)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT,
        help="output ISO path (default: build/hextra.iso)",
    )
    parser.add_argument(
        "--iso-root",
        type=Path,
        default=DEFAULT_ISO_ROOT,
        help="temporary ISO root directory (default: build/iso)",
    )
    parser.add_argument(
        "--no-shell-module",
        action="store_true",
        help="do not add the shell as a Limine module",
    )
    parser.add_argument(
        "--keep-root",
        action="store_true",
        help="keep the generated ISO root directory after the ISO is created",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    kernel = args.kernel if args.kernel.is_absolute() else ROOT / args.kernel
    shell_dir = args.shell_dir if args.shell_dir.is_absolute() else ROOT / args.shell_dir
    init_elf = args.init_elf if args.init_elf.is_absolute() else ROOT / args.init_elf
    output = args.output if args.output.is_absolute() else ROOT / args.output
    iso_root = args.iso_root if args.iso_root.is_absolute() else ROOT / args.iso_root

    xorriso = shutil.which("xorriso")
    if xorriso is None:
        die("xorriso is not installed or is not in PATH")

    limine_cmd = shutil.which("limine")
    if limine_cmd is None:
        die("limine is not installed or is not in PATH")

    limine_dir = find_limine_assets(args.limine)
    check_elf(kernel, "kernel")

    use_shell_module = not args.no_shell_module
    require_file(init_elf, "userspace init")
    main_shell = shell_dir / "main.elf"
    if use_shell_module:
        check_elf(main_shell, "userspace shell")

    for parent in (output.parent, iso_root.parent):
        parent.mkdir(parents=True, exist_ok=True)

    print("[ISO] Building Hextra ISO...")
    print(f"[ISO] Kernel: {kernel}")
    print(f"[ISO] Init: {init_elf}")
    print(f"[ISO] Shell tree: {shell_dir}")
    print(f"[ISO] Limine: {limine_dir}")
    print(f"[ISO] Output: {output}")

    if iso_root.exists():
        shutil.rmtree(iso_root)

    iso_root.mkdir(parents=True)
    boot_dir = iso_root / "boot"
    limine_boot_dir = boot_dir / "limine"
    efi_boot_dir = iso_root / "EFI" / "BOOT"
    limine_boot_dir.mkdir(parents=True)
    efi_boot_dir.mkdir(parents=True)

    shutil.copy2(kernel, boot_dir / "hextra.elf")
    shutil.copy2(init_elf, boot_dir / "init.elf")

    # Copy the entire shell userland tree recursively. This deliberately does
    # not enumerate individual programs, so newly added files/directories
    # under build/userland/shell are included automatically.
    shell_dst = boot_dir / "userland" / "shell"
    shutil.copytree(shell_dir, shell_dst)

    # Limine's current hybrid-ISO layout expects these assets, and its config
    # file can live under /boot/limine. The UEFI executable must be present in
    # EFI/BOOT as well as the El Torito EFI image used by xorriso.
    for name in ("limine-bios.sys", "limine-bios-cd.bin", "limine-uefi-cd.bin"):
        shutil.copy2(limine_dir / name, limine_boot_dir / name)
    shutil.copy2(limine_dir / "BOOTX64.EFI", efi_boot_dir / "BOOTX64.EFI")

    config = [
        "timeout: 5",
        "",
        "/Hextra",
        "    protocol: limine",
        "    path: boot():/boot/hextra.elf",
    ]

    if use_shell_module:
        config.append("    module_path: boot():/boot/userland/shell/main.elf")

    (limine_boot_dir / "limine.conf").write_text(
        "\n".join(config) + "\n",
        encoding="ascii",
    )

    if output.exists():
        output.unlink()

    run(
        [
            xorriso,
            "-as",
            "mkisofs",
            "-quiet",
            "-R",
            "-r",
            "-J",
            "-b",
            "boot/limine/limine-bios-cd.bin",
            "-no-emul-boot",
            "-boot-load-size",
            "4",
            "-boot-info-table",
            "-hfsplus",
            "-apm-block-size",
            "2048",
            "--efi-boot",
            "boot/limine/limine-uefi-cd.bin",
            "-efi-boot-part",
            "--efi-boot-image",
            "--protective-msdos-label",
            str(iso_root),
            "-o",
            str(output),
        ]
    )

    print("[ISO] Installing Limine BIOS bootloader...")
    run([limine_cmd, "bios-install", str(output)])

    if not output.is_file() or output.stat().st_size == 0:
        die(f"ISO was not created correctly: {output}")

    if not args.keep_root:
        shutil.rmtree(iso_root)

    print(f"[ISO] ISO created successfully: {output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except IsoError as exc:
        print(f"[ERR] {exc}", file=sys.stderr)
        raise SystemExit(1)
