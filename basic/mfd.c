#include <linux/module.h>
#include <linux/init.h>

int myvar;
int myinit(void){
	printk("%s: My first device driver\n",__func__);
	myvar=10;
	return 0;
}

void myexit(void){
	printk("%s: Good Bye.!\n",__func__);
}


EXPORT_SYMBOL_GPL(myvar);

module_init(myinit);
module_exit(myexit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("xyz");
MODULE_DESCRIPTION("My first driver");



