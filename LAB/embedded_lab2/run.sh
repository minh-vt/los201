#!/usr/bin/bash
# LAB-02: boot the vexpress-a9 target with the LAB-02 initramfs.
# NOTE: output/zImage is the LAB-01 kernel rebuilt with CONFIG_MTD_RAW_NAND=m
# and CONFIG_MTD_NAND_NANDSIM=m so that Part C (nandsim + JFFS2) can run on the
# target (the CachyOS host cannot load kernel modules without root).
qemu-system-arm \
  -M vexpress-a9 \
  -cpu cortex-a9 \
  -m 512M \
  -smp 2 \
  -nographic \
  -echr 2 \
  -kernel ./output/zImage \
  -dtb ./output/vexpress-v2p-ca9.dtb \
  -initrd ./output/initramfs_lab2.cpio.gz \
  -append "console=ttyAMA0,115200 rdinit=/sbin/init mem=512M"
