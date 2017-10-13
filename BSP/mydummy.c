#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/slab.h>

#define NAME "sample"

struct platform_device *pdev;

static int __init my_dummy_init(void){
	int ret;
	pdev=platform_device_alloc(NAME, -1);
	ret=platform_device_add(pdev);
	if(ret){
		pr_err("%s: platform device is not added successfully.\n",__func__);
		goto free;
	}	
	pr_info("%s: platform device added successfully.\n",__func__);
	return ret;
free:
	platform_device_del(pdev);
	return ret;
}
static void __exit my_dummy_exit(void){
	platform_device_del(pdev);
	pr_info("Exited successfully\n");
}

module_init(my_dummy_init);
module_exit(my_dummy_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("beingchandanjha@gmail.com");
MODULE_DESCRIPTION("Platform device Registration");
MODULE_VERSION(".1");
