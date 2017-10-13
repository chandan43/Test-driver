#include <linux/module.h>
#include <linux/init.h>

extern int myvar;

int myinit(void){
	printk("%s: My first dependent driver ,and variable val=%d\n",__func__,myvar);
	return 0;
}

void myexit(void){
	printk("%s: Good Bye.!\n",__func__);
}

module_init(myinit);
module_exit(myexit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("chandan@jha.com");
MODULE_DESCRIPTION("My driver is dependent on mfd.ko");



