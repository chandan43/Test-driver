#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <asm/uaccess.h>
#include <linux/cdev.h>

#define DEVICE_NAME "mychar"

static DEFINE_MUTEX(mychar_mutex);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("cjha@gmail.com");
MODULE_DESCRIPTION("My New Char Driver");
MODULE_VERSION(".2");
static dev_t mychar;
static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
ssize_t dev_read(struct file *, char __user *, size_t, loff_t *);
ssize_t dev_write(struct file *, const char __user *, size_t, loff_t *);

static char message[256]={0};
static int majornumber;
static int minornumber;
static int numbersopen=0;
static struct cdev *mydev;
static size_t size_of_msg;
static int ret;
static struct file_operations fops = {
	.owner   = THIS_MODULE,
	.open    = dev_open, 
	.release = dev_release,
	.read    = dev_read,
	.write   = dev_write,
};

int mychar_init(void){
	pr_info("%s: Char Driver Initializing\n",__func__);
	mutex_init(&mychar_mutex);
	ret=alloc_chrdev_region(&mychar,0,1,DEVICE_NAME);
	//ret=register_chrdev_region(MKDEV(majornumber,minornumber),1,DEVICE_NAME);
	if(ret<0){
		pr_err("%s: alloc_chrdev_region Failed\n",__func__);
		return ret;
	}
	majornumber=MAJOR(mychar);
	minornumber=MINOR(mychar);
	pr_info("%s: Device is registered with %d Major Number and %d Minor Number\n",__func__,majornumber,minornumber);
	mydev=cdev_alloc();
	mydev->ops=&fops;
	mydev->owner=THIS_MODULE;
	ret=cdev_add(mydev,mychar,1);
	if(ret<0){
		pr_err("%s: cdev_add is failed\n",__func__);
		return ret;
	}
	pr_info("%s: Cdev allocation is  successfully\n",__func__);
	return 0;
}

void mychar_exit(void){
	mutex_destroy(&mychar_mutex);
	cdev_del(mydev);
	unregister_chrdev(majornumber,DEVICE_NAME);
	pr_info("%s: Char Driver is Exited successfully.Good Bye.!\n",__func__);
}

static int dev_open(struct inode *inodep, struct file *filep){
	if(!mutex_trylock(&mychar_mutex)){
		pr_err("%s:  Device in use by another process\n",__func__);
		return -EBUSY;
	}
	numbersopen++;
	pr_info("%s: Device opened %d times\n",__func__,numbersopen);
	return 0;
}
ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset){
	int err_count=0;
	size_of_msg=len;
	err_count=copy_to_user(buffer,message,size_of_msg);
	if(err_count==0){
		pr_info("%s: Sent %zu characters to the user\n",__func__,size_of_msg);
		return(size_of_msg=0);
	}
	pr_err("%s: Device Read is failed\n",__func__);
	return -EFAULT;
	
}
ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset){
	int err_count=0;
	size_of_msg=len;
	memset(message,0,len);
	err_count=copy_from_user(message,buffer,size_of_msg);
	if(err_count!=0){
		pr_err("%s: Device write is failed\n",__func__);
		return -EFAULT;
	}
	pr_info("%s: Received %zu characters from the user\n",__func__,size_of_msg);
	return(size_of_msg=0);
}

static int dev_release(struct inode *inodep, struct file *filep){
	mutex_unlock(&mychar_mutex);
	pr_info("%s: Device is closed successfully\n",__func__);
	return 0;
}
module_init(mychar_init);
module_exit(mychar_exit);
