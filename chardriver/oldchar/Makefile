obj-m := mychar.o

KDIR=/lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	$(CC) test.c -o test
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean 
	rm test


