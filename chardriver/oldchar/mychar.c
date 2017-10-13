#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <asm/uaccess.h>

#define DEVICE_NAME "mychar"
#define CLASS_NAME "char"
static DEFINE_MUTEX(mychar_mutex);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("cjha@gmail.com");
MODULE_DESCRIPTION("My First Char Driver");
MODULE_VERSION(".1");

static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
ssize_t dev_read(struct file *, char __user *, size_t, loff_t *);
ssize_t dev_write(struct file *, const char __user *, size_t, loff_t *);

static char message[256]={0};
static int majornumber;
static int numbersopen=0;
static struct class *charclass=NULL;
static struct device *chardevice=NULL;
static size_t size_of_msg;

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
	majornumber=register_chrdev(0,DEVICE_NAME,&fops);
	if(majornumber < 0){
		pr_err("%s: Device registration Failed\n",__func__);
		return majornumber;
	}
	pr_info("%s: Device is registered with %d Major Number\n",__func__,majornumber);
	charclass=class_create(THIS_MODULE,CLASS_NAME);
	if(IS_ERR(charclass)){
		pr_err("%s: Class creation failed\n",__func__);
		unregister_chrdev(majornumber,DEVICE_NAME);
		return PTR_ERR(charclass);
	}
	chardevice=device_create(charclass,NULL,MKDEV(majornumber,0),NULL,DEVICE_NAME);
	if(IS_ERR(chardevice)){
		pr_err("%s: Device creation Failed\n",__func__);
		class_destroy(charclass);
		unregister_chrdev(majornumber,DEVICE_NAME);
		return PTR_ERR(chardevice);
	}
	pr_info("%s: Device and Class is created successfully\n",__func__);
	return 0;
}

void mychar_exit(void){
	mutex_destroy(&mychar_mutex);
	device_destroy(charclass,MKDEV(majornumber,0));
	class_unregister(charclass);
	class_destroy(charclass);
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
