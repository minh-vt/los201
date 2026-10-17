# Makefile - build the SMS application for x86_64 host and ARMv7 target.
#   make host  -> sms_app_x86   (Valgrind / strace / perf / GDB)
#   make arm   -> sms_app       (static ARM binary for the QEMU initramfs)

CROSS   ?= arm-linux-gnueabihf-
CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O2 -g -pthread
LDLIBS  ?= -pthread -lrt

SRCS = sms_app.c sensor_thread.c processor_thread.c alert_thread.c \
       logger_thread.c circular_buffer.c log_queue.c

.PHONY: all host arm clean
all: host

host: sms_app_x86
arm:  sms_app

sms_app_x86: $(SRCS) $(wildcard *.h) ../driver/sms_sensor.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDLIBS)

sms_app: $(SRCS) $(wildcard *.h) ../driver/sms_sensor.h
	$(CROSS)gcc $(CFLAGS) -static -o $@ $(SRCS) $(LDLIBS)

clean:
	rm -f sms_app sms_app_x86
