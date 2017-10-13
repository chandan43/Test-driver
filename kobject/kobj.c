#include <linux/module.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/errno.h>
#include <linux/sysfs.h>
#include <linux/device.h>

#define KOBJ_NAME "mykobj"
#define ATTR_NAME "myattr"
static int value;


void triger_print(){
	int i;
	for(i=0;i<10;i++){
		pr_info("%s: %d",__func__,i);
	}
}
static ssize_t value_show(struct kobject *kobj, struct attribute *attr,char *buff){
	return sprintf(buff, "%d\n",value);
}
static size_t value_store(struct kobject *kobj,struct attribute *attr,const char *buff, size_t count){
	sscanf(buff,"%du",&value);
	if(value==4)
		triger_print();
	return count;
}
static struct kobj_attribute value_attr=__ATTR(value,0666,value_show,value_store);

static struct attribute *val_attrs[]={
	&value_attr.attr,
	NULL,
};

const struct attribute_group attr_grp={
	.name  = ATTR_NAME,
	.attrs = val_attrs,
};

struct kobject *kobj;
int kobj_init(void){
	int result=0;
	pr_info("%s: Kobject is Initialization\n",__func__);
	kobj=kobject_create_and_add(KOBJ_NAME,kernel_kobj->parent);
	if(!kobj){
		pr_err("kobject_create_and_add: Not created successfully\n");
		return -ENOMEM;
	}
	result=sysfs_create_group(kobj,&attr_grp);
	return 0;
}

void kobj_exit(void){
	pr_info("%s: Kobject destroyed successfully\n",__func__);
	kobject_put(kobj);
}

module_init(kobj_init);
module_exit(kobj_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cjha@cavium.com");
MODULE_DESCRIPTION("My first kobject creation\n");
