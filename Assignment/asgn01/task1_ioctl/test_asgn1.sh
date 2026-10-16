#!/bin/sh
# test_asgn1.sh - ASSIGNMENT-01 Task 1 test script (runs inside QEMU ARM target)
echo '======================================================'
echo ' ASSIGNMENT-01 / Task 1 : asgn1_driver ioctl test'
echo '======================================================'

echo '[1] Load module (major 241)'
insmod /lib/modules/5.15.0/asgn1_driver.ko
echo "    insmod rc=$?"
lsmod | grep asgn1
ls -la /dev/asgn1

echo
echo '[2] Userspace ioctl test'
asgn1_test
T1=$?

echo
echo '[3] Driver registered in /proc/devices :'
grep -i asgn1 /proc/devices

echo
echo '[4] Kernel log (dmesg | grep asgn1) :'
dmesg | grep asgn1 | tail -30

echo
echo '[5] Unload module'
rmmod asgn1_driver
lsmod | grep asgn1 || echo '    (module removed)'
dmesg | grep asgn1 | tail -2
echo "=== Task 1 DONE (userspace rc=$T1) ==="
