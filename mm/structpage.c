#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mm.h>

static unsigned char *ptr;
static unsigned char *pa;

int init_module(void){
	pr_info("%s: Size of struct page =%ld\n",__func__,sizeof(struct page));
	pr_info("%s: No. of pages available =%ld\n",__func__,get_num_physpages());

	ptr=kmalloc(4096,GFP_KERNEL);
	pr_info("%s: Value of ptr (Linear address/virtual address)= %p\n",__func__,ptr);
	pa=(unsigned char *) __pa(ptr);
	pr_info("%s: Value of physical address =%p\n",__func__,pa);
	strcpy(ptr,"Data written to page\n");
	return 0;
}

void cleanup_module(void){
	pr_info("%s : This is low level MM \n",__func__);
}


MODULE_LICENSE("GPL");
MODULE_AUTHOR("cjha@gmail.com");
MODULE_DESCRIPTION("My first low level mm program");

