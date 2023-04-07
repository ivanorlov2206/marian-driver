marianpci-y := marian.o a3.o generic.o m2.o seraph8.o spi.o
obj-m := marianpci.o

all:
	make -C $(KSRC) M=$(PWD) modules
clean:
	make -C $(KSRC) M=$(PWD) clean
