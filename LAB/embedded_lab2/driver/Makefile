obj-m += lab2_driver.o

# Target Kernel directory (Linux 5.15 built in Lab 01)
KDIR ?= $(PWD)/../../embedded_lab1/kernel/linux-5.15
ARCH ?= arm
CROSS_COMPILE ?= arm-linux-gnueabihf-

all:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) CC="$(CROSS_COMPILE)gcc -mfloat-abi=soft" modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
