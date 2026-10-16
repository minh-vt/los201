#!/bin/sh
# test_counter.sh - ASSIGNMENT-01 Task 2 test script (runs inside QEMU ARM target)
echo '======================================================'
echo ' ASSIGNMENT-01 / Task 2 : counter_driver (misc) test'
echo '======================================================'

echo '[1] Load module (misc major 10)'
insmod /lib/modules/5.15.0/counter_driver.ko
echo "    insmod rc=$?"
lsmod | grep counter
ls -la /dev/counter

echo
echo '[2] /proc/counter_info (before test)'
cat /proc/counter_info

echo
echo '[3] Userspace counter test'
counter_test
T2=$?

echo
echo '[4] /proc/counter_info (after test)'
cat /proc/counter_info

echo
echo '[5] Kernel log (dmesg | grep counter_driver) :'
dmesg | grep counter_driver | tail -20

echo
echo '[6] Unload module'
rmmod counter_driver
lsmod | grep counter || echo '    (module removed)'
echo "=== Task 2 DONE (userspace rc=$T2) ==="
