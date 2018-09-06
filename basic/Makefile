obj-m := mfd.o
obj-m += mfdd.o
#obj-m += modparam.o
#obj-m += mfdd_indir.o

KDIR=/lib/modules/$(shell uname -r )/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules 
install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
	depmod -a
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean 

