# Makefile - cross-compile sms_sensor_driver.ko for ARMv7 (vexpress-a9)
obj-m += sms_sensor_driver.o

KDIR ?= $(PWD)/../../../LAB/embedded_lab1/kernel/linux-5.15
ARCH ?= arm
CROSS_COMPILE ?= arm-linux-gnueabihf-
CC_SOFT = $(CROSS_COMPILE)gcc -mfloat-abi=soft

all:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) CC="$(CC_SOFT)" modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
