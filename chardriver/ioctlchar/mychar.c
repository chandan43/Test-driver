#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <asm/uaccess.h>
#include <linux/string.h>
#include "mychar.h"
#define DEVICE_NAME "mychar"
#define SIZE 256
static DEFINE_MUTEX(mychar_mutex);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("cjha@gmail.com");
MODULE_DESCRIPTION("My First Char Driver");
MODULE_VERSION(".3");

static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
ssize_t dev_read(struct file *, char __user *, size_t, loff_t *);
ssize_t dev_write(struct file *,const char __user *, size_t, loff_t *);
static long dev_ioctl(struct file *, unsigned int, unsigned long);
static char message[SIZE]={0};
static int numbersopen=0;
static int majornumber;
static size_t size_of_msg;

static struct file_operations fops = {
	.owner   = THIS_MODULE,
	.open    = dev_open, 
	.release = dev_release,
	.unlocked_ioctl=dev_ioctl,
	.read    = dev_read,
	.write   = dev_write,
};

int mychar_init(void){
	pr_info("%s: Char Driver Initializing\n",__func__);
	mutex_init(&mychar_mutex);
	majornumber=register_chrdev(MAJOR_NUM,DEVICE_NAME,&fops);
	if(majornumber < 0){
		pr_err("%s: Device registration Failed\n",__func__);
		return majornumber;
	}
	pr_info("%s: Device is registered with %d Major Number\n",__func__,MAJOR_NUM);
	return 0;
}

void mychar_exit(void){
	mutex_destroy(&mychar_mutex);
	unregister_chrdev(MAJOR_NUM,DEVICE_NAME);
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
ssize_t dev_read(struct file *filep,char *buffer, size_t len, loff_t *offset){
	int err_count=0;
	size_of_msg=len;
	err_count=copy_to_user(buffer,message,size_of_msg);
	pr_info("%s: buffer is [%s]\n",__func__,message);
	if(err_count==0){
		pr_info("%s: Sent %zu characters to the user\n",__func__,size_of_msg);
		return(size_of_msg=0);
	}
	pr_err("%s: Device Read is failed\n",__func__);
	return -EFAULT;
	
}
ssize_t dev_write(struct file *filep,const char *buffer, size_t len, loff_t *offset){
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
static long dev_ioctl(struct file *filep, unsigned int ioctl_num, unsigned long ioctl_param){
	switch(ioctl_num){
		case IOCTL_WRITE_MSG:
			dev_write(filep,(char *)ioctl_param,strlen((char *)ioctl_param),0);
			break;
		case IOCTL_READ_MSG:
			dev_read(filep,(char *)ioctl_param,strlen((char *)ioctl_param),0);
			copy_to_user(message,message,strlen((char *)ioctl_param));
			break;
	}
	return 0;
}

static int dev_release(struct inode *inodep, struct file *filep){
	mutex_unlock(&mychar_mutex);
	pr_info("%s: Device is closed successfully\n",__func__);
	return 0;
}
module_init(mychar_init);
module_exit(mychar_exit);
