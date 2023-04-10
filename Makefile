marianpci-y 	:= marian.o a3.o generic.o m2.o seraph8.o
obj-m 		:= marianpci.o
KERNELVER 	?= $(shell uname -r)
KERNELDIR	?= /lib/modules/$(KERNELVER)/build

all:
	make -C $(KERNELDIR) M=$(PWD) modules
clean:
	make -C $(KERNELDIR) M=$(PWD) clean
