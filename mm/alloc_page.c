#include <linux/module.h>
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/sched.h>

struct page *p;
char *lmem;
int init_module(void){
	unsigned int pfn;
	p=alloc_page(GFP_KERNEL);
	if(!p){
		pr_err("\nCan't allocate Mem\n");
		return -ENOMEM;
	}
	/*===Get Linear address===*/
	lmem=page_address(p);
	pr_info("%s: Linear memory address using alloc_page is %p\n",__func__,p);
	pr_info("%s: page frame No. using __page_to_fpn API %ld",__func__,page_to_pfn(p));
	
	/*Get memory using __get_free_pages===*/

	lmem=(char *)__get_free_page(GFP_KERNEL);
	if(!lmem){
		pr_err("\nCan't allocate Mem\n");
		return -ENOMEM;
	}
	pr_info("%s: Linear memory address using ''__get_free_page'' is %p\n",__func__,lmem);
	pr_info("%s: Physical memory address : %p\n",__func__,(void *)__pa(lmem));
	pfn=__pa(lmem)>>PAGE_SHIFT;
	pr_info("\nPage frame number %d\n",pfn);
	pr_info("Physical frame number using ''pfn_to_page'' API%p\n",pfn_to_page(pfn));
	return 0;
}

void cleanup_module(void){
	pr_info("%s: Good Bye\n",__func__);
}


MODULE_LICENSE("GPL");
MODULE_AUTHOR("cjha@gmail.com");
MODULE_DESCRIPTION("allocate_page Example");
