/*----Memory pool---*/
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/errno.h>


void *ptr;
int vmalloc__init(void){
	ptr=vmalloc(6000);  /*allocate a cache*/
	if(!ptr)
		return -ENOMEM;
	pr_info("Vmalloc: value of ptr(Virtual address)=%p\n",ptr);
	strcpy(ptr,"Data written in page\n");
	return 0;
}

void vmalloc__exit(void){
	vfree(ptr);
	pr_info("%s: Virtual mem exited successful\n",__func__);
}

module_init(vmalloc__init);
module_exit(vmalloc__exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("cjha@gmail.com");
MODULE_DESCRIPTION("Kmem_cache Example-->Memory pool");
