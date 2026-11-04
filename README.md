# LOS201 — Embedded Linux Systems

Coursework and experiments for **LOS201: Embedded Linux Systems**. This repository keeps the lab and assignment source code, configuration files, and reports together so I can revisit how each system was built and tested.

The work covers building an embedded Linux environment, preparing a root filesystem, and writing kernel-side code. It is a collection of course exercises, not a single application.

## Repository layout

```text
.
├── LAB/
│   ├── embedded\\\_lab1/       # Linux, U-Boot, and BusyBox build work
│   └── embedded\\\_lab2/       # Kernel modules and filesystem experiments
├── Assignment/
│   ├── asgn01/              # ioctl and miscellaneous-device exercises
│   └── asgn02/              # Application and system-image work
└── Lecture Slides/         # Course materials, where tracked
```

The paths above reflect the project directories referenced by the repository configuration. Check each directory for its own source files, scripts, and reports; not every exercise uses the same build procedure.

## Tools and components

The exercises reference these components:

* **Linux kernel 5.15** for the target system and kernel modules
* **U-Boot 2022.04** for bootloader work
* **BusyBox 1.35.0** for a small user-space environment
* An **ARM cross-compiler** and **QEMU** for building and testing embedded targets

Exact toolchain and emulator settings depend on the exercise. Use the instructions and scripts in the relevant lab or assignment directory rather than assuming one command builds the entire repository.

## Getting started

Clone the repository:

```bash
git clone https://github.com/minh-vt/los201.git
cd los201
```

Then choose the lab or assignment you want to work on. Before building, check its configuration for the expected kernel source path, toolchain prefix, and target architecture.

Some dependencies and generated files are deliberately **not committed**. In particular, the repository excludes downloaded Linux, U-Boot, and BusyBox source trees; compiled objects; kernel and disk images; initramfs archives; and packaged submission files. You may need to download or rebuild these locally.

For kernel-module exercises, build against the **same kernel version and configuration** used by the target image. A module compiled against a different kernel may fail to load even if compilation succeeds.

## What is kept in Git?

Source code, configuration, scripts, small module binaries where applicable, screenshots, and PDF reports can be tracked. Large third-party source trees, generated system images, videos, and temporary build output are excluded by [`.gitignore`](.gitignore).

This means cloning the repository alone may not reproduce an already-built environment. It preserves the coursework, not every intermediate build artifact.

## Notes

This repository is maintained as a personal record of coursework. Solutions and configuration choices are tied to the course environment and may require adjustment on another Linux host or toolchain.

If you are using it to study, read the exercise requirements and try the implementation yourself before referring to the submitted work.

