#!/usr/bin/bash
qemu-system-arm \
  -M vexpress-a9 \
  -cpu cortex-a9 \
  -m 512M \
  -smp 2 \
  -nographic \
  -echr 2 \
  -kernel ./output/zImage \
  -dtb ./output/vexpress-v2p-ca9.dtb \
  -initrd ./output/initramfs.cpio.gz \
  -append 'console=ttyAMA0,115200 rdinit=/sbin/init mem=512M'
