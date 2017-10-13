#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/slab.h>

static int sample_probe(struct platform_device *pdev){
	pr_info("%s: Sample device probe invoked\n",__func__);
	return 0;
}
static int sample_remove(struct platform_device *pdev){
	pr_info("%s: Device Removed\n",__func__);
	return 0;
}
static const struct platform_device_id my_driver_ids[]={
	{"sample",-1},
	{},
};
MODULE_DEVICE_TABLE(platform,my_driver_ids);
static struct platform_driver my_sample_driver={
	.driver={
		.name="my_sample",
		.owner=THIS_MODULE,
	},
	.id_table=my_driver_ids,
	.probe=sample_probe,
	.remove=sample_remove,
};

module_platform_driver(my_sample_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("beingchandanjha@gmail.com");
MODULE_DESCRIPTION("Platform device Registration");
MODULE_VERSION(".1");
