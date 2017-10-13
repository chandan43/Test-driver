#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/types.h>  /* size_t */
#include <linux/kernel.h> /* printk() */
#include <linux/fs.h>     /* everything... */
#include <linux/errno.h>
#include <linux/genhd.h>
#include <linux/vmalloc.h>  

#include <linux/blkdev.h>
#include <linux/hdreg.h> /*hd_geometry is defined*/

#define KERNEL_SECTOR_SIZE 512
#define DEVICE_NAME "sbd"
#define MINOR_NO 16
static int sbd_getgeo(struct block_device *, struct hd_geometry *);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("beingchandanjha@gamil.com");
MODULE_DESCRIPTION("My first basic Block Device Driver");
MODULE_VERSION(".1");

/*KERNEL_SECTOR_SIZE is locally defined constant that we use to scale between the kernel's 512 bytes sectors and whatever size we have been told to use.*/


static int majornumber=0;
module_param(majornumber,int,0);
MODULE_PARM_DESC(majornumber,"Major number of Block driver:it should be unsigned integer in between 0 to 255.");
static int logical_block_size=512;        /*Setctor Size*/ 
module_param(logical_block_size,int,0);
MODULE_PARM_DESC(logical_block_size,"Logical Block size : it should be power of 2 (2^n) and less then page size.By default 512 bytes.");
static int nsector=1024;         /*Size of Driver i.e total no sector */
module_param(nsector,int,0);
MODULE_PARM_DESC(nsector,"Sector Size : Default 1024");

/* Internal structure of block device */
struct sbd_dev{
	unsigned int size;        /*Device size in sectors*/
	spinlock_t lock;
	u8 *data;                 /* Data Array */ 
	struct gendisk *gd;       /* The gendisk structure */
	struct request_queue *Queue;  /*Device request queue */
}Device;
static void sbd_transfer(struct sbd_dev *dev,sector_t sector,unsigned long nsector,char *buffer,int write){  
	/*param1: Device, P2: Index of beginning sector P3:No. of sectors ,P4:Buffer P5:Zero:read and Non Zero for Write*/
	unsigned long offset=sector*logical_block_size;
	unsigned long nbytes=nsector*logical_block_size;
	if((offset + nbytes) > Device.size){
		pr_notice("Beyond-end write (%ld %ld)\n",offset,nbytes);
		return;
	}
	if(write)
	    memcpy(Device.data + offset,buffer,nbytes);
	else 
	    memcpy(buffer,Device.data + offset,nbytes);  /* Read*/
}

void sbd_req(struct request_queue *q){
	struct request *req;
	req=blk_fetch_request(q);    /*,This is combination of blk_peek + blk_start(),There is no more elv_next_request();,After 2.6 Kernel*/
	while(req!=NULL){
		if(req==NULL && req->cmd_type!=REQ_TYPE_FS){
			pr_err("req->cmd_type: is not REQ_TYPE_FS.!\n");
			__blk_end_request_all(req,-EIO); /*Completely finish req*/
			continue;
		}
		/*blk_rq_pos(): the current sector,blk_rq_cur_sectors(): sectors left in the current segment : blkdev.h rq_data_dir(req):-return non zero for write and 0 for read*/
		 sbd_transfer(&Device, blk_rq_pos(req),blk_rq_cur_sectors(req),req->buffer,rq_data_dir(req)); 
		if(!__blk_end_request_cur(req,0)){       /*Request to finish the current chunk  0 for success, < 0 for error */
			req=blk_fetch_request(q);    /*,This is combination of blk_peek + blk_start(),There is no more elv_next_request();,After 2.6 Kernel*/
		}
	}
}
static int sbd_getgeo(struct block_device * block_device, struct hd_geometry *geo){
	long size;
	size=Device.size * (logical_block_size / KERNEL_SECTOR_SIZE);
	geo->cylinders = (size & 0x3f) >> 6;     /*No of cylinders i.e (size & ~(111111) ) means lossing first 6 bit info and deviding by 2^6(64)*/
	geo->heads=4;   /*Four head*/
	geo->sectors=16;   /*We claim 16 sectors*/
	geo->start=4;   /*start of data at sector 4*/
	return 0;
}
static struct block_device_operations sdb_fops={                     /*Defined in blkdev.h*/
	.owner   = THIS_MODULE,
	.getgeo  =sbd_getgeo,
};

int sbd_init(void){
	pr_info("%s: Initialization of Block Device Driver\n",__func__);
	Device.size=nsector * logical_block_size;
	spin_lock_init(&Device.lock);
	Device.data=vmalloc(Device.size);
	if(Device.data==NULL){
		pr_err("%s: Insufficient memory\n",__func__);
		return -ENOMEM;
	}
	/*allocation of Request queue for request processing */
	Device.Queue=blk_init_queue(sbd_req,&Device.lock);
	if(Device.Queue==NULL){
		pr_err("blk_init_queue: queue Initialization Failed\n");
		goto free;
	}
	/*set logical_block_size for Initialised Queue*/
	blk_queue_logical_block_size(Device.Queue,logical_block_size);
	/*Block Device Registration */
	majornumber=register_blkdev(0,DEVICE_NAME);
	if(majornumber<0){
		pr_err("%s: Device Registration failed\n",__func__);
		goto free;
	}
	/*Initialised of gendisk structure */
	Device.gd=alloc_disk(MINOR_NO);
	if(!Device.gd){
		pr_err("Gendisk: allocation failed-%d.\n",__LINE__);
		goto unreg_dev;
	}
	Device.gd->major=majornumber;
	Device.gd->first_minor=0;
	//Device.gd->minors=16;                  /*0-15 Partition support*/
	Device.gd->fops=&sdb_fops;
	Device.gd->private_data=&Device;
	strcpy(Device.gd->disk_name,"sbd0");
	set_capacity(Device.gd,nsector);
	Device.gd->queue=Device.Queue;
	add_disk(Device.gd);
	return 0;
unreg_dev:
	unregister_blkdev(majornumber,DEVICE_NAME);
free: 
	vfree(Device.data);
	return -ENOMEM;
}

void sbd_exit(void){
	del_gendisk(Device.gd);       /*When disk is not longer needed, it should be free*/
	put_disk(Device.gd);  /*That call will cause the gendisk structure to be freed*/
	unregister_blkdev(majornumber,DEVICE_NAME);   
	blk_cleanup_queue(Device.Queue);           /*Queue clean up*/
	vfree(Device.data);
	pr_info("%s: Block Device Driver Exited successfully\n",__func__);
}

module_init(sbd_init);
module_exit(sbd_exit);

