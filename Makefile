obj-m := csl_dev.o
csl_dev-objs := dev.o metadata.o

KDIR := /lib/modules/$(shell uname -r)/build
RESET_DEVICE = 0

all:
	make -C $(KDIR) M=$(PWD) modules

mutex:
	make -C $(KDIR) M=$(PWD) modules EXTRA_CFLAGS="-D_USE_MUTEX"

semaphore:
	make -C $(KDIR) M=$(PWD) modules EXTRA_CFLAGS="-D_USE_SEMAPHORE"

debug:
	make -C $(KDIR) M=$(PWD) modules EXTRA_CFLAGS="-DDEBUG"

clean:
	make -C $(KDIR) M=$(PWD) clean

print:
	sudo dmesg

load:
	sudo insmod csl_dev.ko __reset_device=$(RESET_DEVICE)
	sudo chmod 666 /dev/csl

unload:
	sudo rmmod csl_dev

test:
	gcc -o test test.c
	./test
	rm test

fio:
	sudo fio fio_test.fio