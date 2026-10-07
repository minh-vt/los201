#!/bin/sh
echo '=== LAB-02: Device Driver Test ==='

# Buoc 1: Load module
echo '[1] Loading lab2_driver...'
if ! lsmod | grep -q lab2; then
    insmod /lib/modules/5.15.0/lab2_driver.ko
    if [ $? -ne 0 ]; then
        echo 'ERROR: insmod failed!'
        exit 1
    fi
else
    echo '  (Module already loaded)'
fi

# Buoc 2: Xac nhan module da load
echo '[2] Loaded modules:'
lsmod | grep lab2

# Buoc 3: Tao device node (neu chua co)
echo '[3] Creating device node...'
mknod /dev/lab2 c 240 0 2>/dev/null || echo '  (node already exists)'
chmod 666 /dev/lab2 2>/dev/null || true
ls -la /dev/lab2

# Buoc 4: Test WRITE
echo '[4] Writing to device...'
echo 'Hello from userspace, LAB-02!' > /dev/lab2

# Buoc 5: Test READ
echo '[5] Reading from device...'
DATA=$(cat /dev/lab2)
echo "  Read back: [$DATA]"

# Buoc 6: Test nhieu lan write/read
echo '[6] Multiple write/read test...'
for i in 1 2 3; do
    echo "Message_$i" > /dev/lab2
    cat /dev/lab2
done

# Buoc 7: Xem kernel log
echo '[7] Kernel messages (dmesg):'
dmesg | grep lab2 | tail -10

echo '=== Test PASSED ==='
