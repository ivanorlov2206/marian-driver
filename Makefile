obj-m 		:= marian.o
KERNELVER 	?= $(shell uname -r)
KERNELDIR	?= /lib/modules/$(KERNELVER)/build

all:
	make -C $(KERNELDIR) -Wunused-variable M=$(PWD) modules
clean:
	make -C $(KERNELDIR) M=$(PWD) clean
