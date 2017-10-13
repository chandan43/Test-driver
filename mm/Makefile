#obj-m :=structpage.o
#obj-m +=alloc_page.o
#obj-m +=kmem_cache.o
#obj-m +=kmemcache_objpool.o
obj-m +=vmalloc.o

KDIR=/lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
