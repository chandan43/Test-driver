#obj-m := rtl8139_1.o
obj-m := rtl8139.o
CFLAGS_rtl8139.o := -DDEBUG


KDIR=/lib/modules/$(shell uname -r )/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules 
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean 

