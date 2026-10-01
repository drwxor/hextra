# Uorix

Uorix is a small experimental Unix-like operating system for x86_64.

It is built from scratch in C with the goal of being simple, understandable, and fun to work on.

## Status

Uorix is currently in early kernel development.

Current functionality includes:

* Limine-based UEFI boot
* x86_64 kernel
* Framebuffer output
* Text renderer
* Ext2 read-only file system
* Basic Libc
* PS/2 keyboard input
* Userland
* kernel/userland shell
* Basic shell commands
* ELF Loading
* VFS
* Scheluder
* GPT Scanning
* Ring-3
* Basic serial/debug support

The shell currently can run from the kernel and userland.

## Building

Uorix uses [kage](https://github.com/drwxor/kage).

Build the kernel with:

```sh
kage
```

The resulting kernel is:

```text
build/uorix.elf
```

## Running

Uorix is currently tested with QEMU and OVMF.

The project includes helper scripts for building, replacing the kernel in the disk image, generating ISO image, and starting QEMU.

Helper can be ran via [uorix-tools](https://github.com/drwxor/uorix-tools)

## Toolchain

Current development uses:

* clang
* wild
* mold 
* kage 
* QEMU
* OVMF
* Limine

## License

Uorix is free software distributed under the GNU General Public License, version 3.

See `LICENSE` for the full license text.

Individual files contain SPDX license identifiers where appropriate.

## Contributing

Uorix is primarily a personal experimental operating-system project.

Code should stay small, explicit, and easy to understand.

Avoid unnecessary dependencies and complexity.

## Disclaimer

Uorix is experimental software and is not intended for production use.
