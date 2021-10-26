# TODO: Change this to the location of your kernel source code
KERNEL_SOURCE=/project/scratch01/compile/anambakam/linux_source/linux

obj-m := lab2_atomic.o lab2_spinlock.o

all:
	$(MAKE) -C $(KERNEL_SOURCE) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_SOURCE) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- M=$(PWD) clean 
