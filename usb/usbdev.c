/*
 * USB driver 
 *
 * This driver is based on the 2.6.3 version of drivers/usb/usb-skeleton.c
 * but has been rewritten to be easier to read and use.
 * Reference:  	
 * http://www.makelinux.net/ldd3/chp-13-sect-4
 * http://ecee.colorado.edu/~siewerts/extra/code/example_code_archive/a490dmis_code/examples-driver/usb/usb-skeleton.c
 * https://www.kernel.org/doc/Documentation/kref.txt
 * https://www.kernel.org/doc/Documentation/usb/dma.txt
 * https://www.kernel.org/doc/Documentation/usb/URB.txt
 * Before writing USB Device drive Please go throgh above mentioned link. 
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/usb.h>
#include <linux/kref.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/mutex.h>

/*Driver INFO*/
MODULE_LICENSE("GPL");
MODULE_AUTHOR("beingchandanjha@gamil.com");
MODULE_DESCRIPTION("My first USB device driver");
MODULE_VERSION(".2");
/* Define these values to match your devices */

#define USB_SKEL_VENDOR_ID	0x0bc2
#define USB_SKEL_PRODUCT_ID	0xa013
/**
 * USB_DEVICE - macro used to describe a specific usb device
 * @vend: the 16 bit USB Vendor ID
 * @prod: the 16 bit USB Product ID
 *
 * This macro is used to create a struct usb_device_id that matches a
 * specific device.
 */
/* table of devices that work with this driver */
static const struct usb_device_id usb_table[]={
	{USB_DEVICE(USB_SKEL_VENDOR_ID,USB_SKEL_PRODUCT_ID)},
	{}  /* Terminating entry */,
};
/*@id_table: USB drivers use ID table to support hotplugging.
 *      Export this with MODULE_DEVICE_TABLE(usb,...).  This must be set
 *      or your driver's probe function will never get called.
 */
MODULE_DEVICE_TABLE(usb,usb_table);


/* Get a minor range for your devices from the usb maintainer */
#define USB_SKEL_MINOR_BASE	192
struct usb_dev {
	struct usb_device* udev;                 /* the usb device for this device */
	struct usb_interface * interface;       /* the interface for this device */
	unsigned char * bulk_in_buffer;         /*the buffer to receive data */
	size_t bulk_in_size;                   /*the size of the receive buffer */
	__u8	bulk_in_endpointAddr;	/* the address of the bulk in endpoint */
	__u8	bulk_out_endpointAddr;	/* the address of the bulk out endpoint */
	struct kref kref;              
	spinlock_t lock;
	struct mutex  io_mutex;		/* synchronize I/O with disconnect */
};
/*krefs allow you to add reference counters to your objects.  If you
 * have objects that are used in multiple places and passed around, and
 * you don't have refcounts, your code is almost certainly broken.  If
 * you want refcounts, krefs are the way to go.*/

/*Macro sets up a pointer that points to the struct device_driver passed to the code . The macro to get a pointer to struct usb_dev by using:*/
#define to_usb_dev(d) container_of(d, struct usb_dev, kref);
static struct usb_driver usb_drv;
static void usb_delete(struct kref *ref){
	struct usb_dev *dev=to_usb_dev(ref);
	usb_put_dev(dev->udev); /*release a use of the usb device structure.Must be called when a user of a device is finished with it*/
	kfree(dev->bulk_in_buffer);  /*Free buffer*/
	kfree (dev);   /*Free device*/
}

static int usb_open(struct inode *inodep, struct file *filep){
	struct usb_dev *dev;
	struct usb_interface *interface;
	int subminor;
	int retval=0;
	/* Be sure to use iminor to obtain the minor number from the inode structure, and make sure that it corresponds to a device that your driver is actually prepared to handle.*/
	subminor=iminor(inodep);   
	interface=usb_find_interface(&usb_drv,subminor); /* find usb_interface pointer for driver and device */
	if(!interface){
		pr_err("%s: Can't find device for minor %d",__func__, subminor);
		retval=-ENODEV;
		goto exit;
	}
	dev = usb_get_intfdata(interface);	/* To retrieve the data, the function usb_get_intfdata should be called:*/
	if(!dev){
		retval=-ENODEV;
		goto exit;
	}
	/* increment our usage count for the device */
	kref_get(&dev->kref);
	/* save our object in the file's private structure */
	filep->private_data=dev;
	return 0;
exit:
	return retval;
}
static  int usb_release(struct inode *inodep, struct file *filep){
	struct usb_dev *dev;
	dev=(struct usb_dev *)filep->private_data;
	if (dev == NULL)
		return -ENODEV;
	/* decrement the count on our device */
	kref_put(&dev->kref, usb_delete);
	return 0;
}

static ssize_t usb_read(struct file *filep,char __user *buffer,size_t count,loff_t *offset){
	int retval=0;
	struct usb_dev *dev;
	dev=(struct usb_dev *)filep->private_data;
	if(dev == NULL)
		return -ENODEV;
	/* do a blocking bulk read to get data from the device */
	/*usb_bulk_msg : This function sends a simple bulk message to a specified endpoint and waits for the message to complete, or timeout. */
	retval=usb_bulk_msg(dev->udev,usb_rcvbulkpipe(dev->udev,dev->bulk_in_endpointAddr),dev->bulk_in_buffer,min(dev->bulk_in_size,count), (int *)&count,HZ*10);
	/*P1: A pointer to the USB device
	 *p2: The specific endpoint of the USB device to which this bulk message is to be sent. This value is created with a call to either usb_sndbulkpipe or usb_rcvbulkpipe.
	 *p3:A pointer to the data to send to the device if this is an OUT endpoint. If this is an IN endpoint, this is a pointer to where the data should be placed after being read from         the device.
	 *p4:The length of the buffer that is pointed to by the data parameter.min is defined in kernel.h
	 *p5:A pointer to where the function places the actual number of bytes that have either been transferred to the device or received from the device, depending on the direction of            the endpoint.
	 *p6: The amount of time, in jiffies, that should be waited before timing out.
	 *HZ : value for i386 was changed to 1000, yeilding a jiffy interval of 1 ms. Recently (2.6.13) the kernel changed HZ for i386 to 250. (1000 was deemed too high).*/
	/*If successful, it returns 0, otherwise a negative error number. */
	/* if the read was successful, copy the data to userspace */
	if(!retval){
		if(copy_to_user(buffer, dev->bulk_in_buffer, count))  //  On success, this will be zero. 
			return -EFAULT;
		else 
			return count;
	}
	return retval;
}
/* (in) completion routine */
/*
 *   - Out of memory (-ENOMEM)
 *   - Unplugged device (-ENODEV)
 *   - Stalled endpoint (-EPIPE)
 *   - Too many queued ISO transfers (-EAGAIN)
 *   - Too many requested ISO frames (-EFBIG)
 *   - Invalid INT interval (-EINVAL)
 *   - More than one packet for INT (-EINVAL)
 */
static void usb_write_bulk_callback(struct urb *urb){
	if(urb->status && !(urb->status == -ENOENT || urb->status == -ECONNRESET ||urb->status == -ESHUTDOWN))
		pr_debug("%s:  Nonzero write bulk status received: %d",__func__,urb->status);
	/* free up our allocated buffer */
	usb_free_coherent(urb->dev,urb->transfer_buffer_length,urb->transfer_buffer,urb->transfer_dma);

}
static ssize_t usb_write(struct file *filep, const char __user *buffer, size_t count, loff_t *offset){
	struct usb_dev *dev;
	int retval;
	struct urb *urb=NULL;  /* struct urb - USB Request Block*/
	char *buf = NULL;
	dev=(struct usb_dev *)filep->private_data;
	/* verify that we actually have some data to write */
	if (count == 0)
		goto exit;
	/* create a urb, and a buffer for it, and copy the data to the urb.URBs are allocated with the following call*/
	urb=usb_alloc_urb(0,GFP_KERNEL);  /* If the return value is NULL, some error occurred within the USB core*/
	/*p1:iso_packets If the driver want to use this urb for interrupt, control, or bulk endpoints, pass '0' as the number of iso packets. */
	if(!urb){
		retval = -ENOMEM;
		goto error;
	}
	/*usb_buffer_alloc() is renamed to usb_alloc_coherent(), allocate dma-consistent buffer for URB_NO_xxx_DMA_MAP*/
	buf=usb_alloc_coherent(dev->udev,count,GFP_KERNEL,&urb->transfer_dma);/*dma_addr_t transfer_dma;  (in) dma addr for transfer_buffer */
	if (!buf) {
		retval = -ENOMEM;
		goto error;
	}
	if (copy_from_user(buf, buffer, count)) {
		retval = -EFAULT;
		goto error;
	}
	/**
	 * usb_fill_bulk_urb - macro to help initialize a bulk urb
	 * @urb: pointer to the urb to initialize.
	 * @dev: pointer to the struct usb_device for this urb.
	 * @pipe: the endpoint pipe  : usb_sndbulkpipe(dev, endpoint)
	 * @transfer_buffer: pointer to the transfer buffer
	 * @buffer_length: length of the transfer buffer
	 * @complete_fn: pointer to the usb_complete_t function
	 * @context: what to set the urb context to.
	 *
	 * Initializes a bulk urb with the proper information needed to submit it
	 * to a device.
	 */
	usb_fill_bulk_urb(urb,dev->udev,usb_sndbulkpipe(dev->udev,dev->bulk_out_endpointAddr),buf,count,usb_write_bulk_callback,dev);
	/*set URB_NO_TRANSFER_DMA_MAP so that usbcore won't map or unmap the buffer.*/
	/*If short packets should NOT be tolerated, set URB_SHORT_NOT_OK in transfer_flags.*/
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	/* send the data out the bulk port */
	retval=usb_submit_urb(urb, GFP_KERNEL); //ON submit return 0.
	if(retval){
		pr_err("%s: failed submitting write urb, error %d",__func__,retval);
		goto error;
	}
	/* release our reference to this urb, the USB core will eventually free it entirely */
	usb_free_urb(urb);

exit:
	return count;
error:
	usb_free_coherent(dev->udev,count,buf,urb->transfer_dma);
	usb_free_urb(urb);
	kfree(buf);
	return retval;
}
/* * @probe: Called to see if the driver is willing to manage a particular
 *      interface on a device.  If it is, probe returns zero and uses
 *      usb_set_intfdata() to associate driver-specific data with the
 *      interface.  It may also use usb_set_interface() to specify the
 *      appropriate altsetting.  If unwilling to manage the interface,
 *      return -ENODEV, if genuine IO errors occurred, an appropriate
 *      negative errno value. */

static struct file_operations usb_fops= {
	.owner   = THIS_MODULE,
	.read   = usb_read,
	.write  = usb_write,
	.open   = usb_open,
	.release= usb_release,
};
/**
 * struct usb_class_driver - identifies a USB driver that wants to use the USB major number
 * @name: the usb class device name for this driver.  Will show up in sysfs.
 * @devnode: Callback to provide a naming hint for a possible
 *      device node to create.
 * @fops: pointer to the struct file_operations of this driver.
 * @minor_base: the start of the minor range for this driver.
 *
 * This structure is used for the usb_register_dev() and
 * usb_unregister_dev() functions, to consolidate a number of the
 * parameters used for them.
 */
struct usb_class_driver usb_class={
	.name="usbdrv%d",
	.fops=&usb_fops,
	.minor_base = USB_SKEL_MINOR_BASE,
};

static int usb_probe(struct usb_interface *interface,const struct usb_device_id *id){
	struct usb_dev *dev=NULL;
	struct usb_host_interface *interface_disc; 
	struct usb_endpoint_descriptor *endpoint;
	size_t buffer_size;
	int i;
	int retval = -ENOMEM;
	/* allocate memory for our device state and initialize it */
	dev=kmalloc(sizeof(struct usb_dev),GFP_KERNEL);
	if(dev==NULL){
		pr_err("kmalloc: Out of memory\n");
		goto error;
	}
	memset(dev,0x00,sizeof(*dev)); /*Clearing memory*/
	kref_init(&dev->kref);
	mutex_init(&dev->io_mutex);
	spin_lock_init(&dev->lock);
	/*usb_get_dev — increments the reference count of the usb device structure*/
	dev->udev=usb_get_dev(interface_to_usbdev(interface));  /* interface_to_usbdev is convert interface to udev*/
	dev->interface=interface;
	/* set up the endpoint information */
	/* use only the first bulk-in and bulk-out endpoints */
	interface_disc=interface->cur_altsetting;   /* The currently active alternate setting */
	/* struct usb_host_endpoint *endpoint; 
	 * array of desc.bNumEndpoints endpoints associated with this
	 * * interface setting.  these will be in no particular order.
	 */
	/**
	 * usb_endpoint_type - get the endpoint's transfer type
	 * @epd: endpoint to be checked
	 * Returns one of USB_ENDPOINT_XFER_{CONTROL, ISOC, BULK, INT} according
	 * to @epd's transfer type.
	 */
	/*This block of code first loops over every endpoint that is present in this interface and assigns a local pointer to the endpoint structure to make it easier to access later:
	 *Then, after we have an endpoint, and we have not found a bulk IN type endpoint already, we look to see if this endpoint's direction is IN.
	 *hat can be tested by seeing whether the bitmask USB_DIR_IN is contained in the bEndpointAddress endpoint variable. If this is true, we determine whether the endpoint type is bu	   *or not, by first masking off the bmAttributes variable with the USB_ENDPOINT_XFERTYPE_MASK bitmask, and then checking if it matches the value USB_ENDPOINT_XFER_BULK:
	 */
	/* use only the first bulk-in and bulk-out endpoints */

	for(i=0;i < interface_disc->desc.bNumEndpoints; ++i){  /*  Usb device driver usually want to detect wahat the endpoint address and buffer size are for the devices*/
		endpoint=&interface_disc->endpoint[i].desc;      /*  @desc: descriptor for this endpoint, wMaxPacketSize in native byteorder*/
		if(!dev->bulk_in_endpointAddr && usb_endpoint_is_bulk_in(endpoint)){
		/*Used to signify direction of data for a UsbEndpoint is IN (device to host) */
		/* we found a bulk in endpoint */
			buffer_size=endpoint->wMaxPacketSize;
			dev->bulk_in_size=buffer_size;
			dev->bulk_in_endpointAddr=endpoint->bEndpointAddress;
			dev->bulk_in_buffer=kmalloc(buffer_size,GFP_KERNEL);
			if(dev->bulk_in_buffer==NULL){
				pr_err("kmalloc: Couldn't alloc memory-bulk_in_buffer\n");
				goto error; 
			}
		}
		if(!dev->bulk_out_endpointAddr && usb_endpoint_is_bulk_out(endpoint)) {
			/* we found a bulk out endpoint */
			dev->bulk_out_endpointAddr=endpoint->bEndpointAddress;                              
			/*endpoint->bEndpointAddress:The address of the endpoint described by this descriptor. 
			 *Bits 0:3 are the endpoint number. Bits 4:6 are reserved. Bit 7 indicates direction*/
		}
	}
	if(!(dev->bulk_out_endpointAddr && dev->bulk_in_endpointAddr)){
		pr_err("ENDPOINT: Could not find both bulk-in and bulk-out endpoints\n");
		goto error;
	}
	/* save our data pointer in this interface device */
	/*Because the USB driver needs to retrieve the local data structure that is associated with this 
	 *struct usb_interface later in the lifecycle of the device, the function usb_set_intfdata can be called*/
	usb_set_intfdata(interface, dev);
	/* we can register the device now, as it is ready */
	retval=usb_register_dev(interface,&usb_class); /*ON success return 0;*/
	if(retval){
		/* something prevented us from registering this driver */
		pr_err("usb_register_dev: Not able to get a minor for this device.\n");
		usb_set_intfdata(interface, NULL);
		goto error;
	}
	/* let the user know what node this device is now attached to */
	pr_info("USB device now attached to USBdrv-%d", interface->minor);
	pr_info("USB device  (%04X:%04X) is plugged\n", id->idVendor, id->idProduct);
	return 0;
error:
	if(dev)
		kref_put(&dev->kref,usb_delete);
	return retval;
}
/* @disconnect: Called when the interface is no longer accessible, usually
 *      because its device has been (or is being) disconnected or the
 *      driver module is being unloaded.*/
void usb_disconnect(struct usb_interface *interface){
	struct usb_dev *dev;
	int minor = interface->minor;  /* minor number this interface is bound to */
	/* prevent skel_open() from racing skel_disconnect() */
	dev=usb_get_intfdata(interface);
	//spin_lock(&dev->lock);
	mutex_lock(&dev->io_mutex);
	usb_set_intfdata(interface, NULL);
	/* give back our minor */
	usb_deregister_dev(interface, &usb_class);
	//	spin_unlock(&dev->lock);
	mutex_unlock(&dev->io_mutex);
	/* decrement our usage count */
	kref_put(&dev->kref, usb_delete);
	pr_info("USB drv #%d now disconnected", minor);
}

static struct usb_driver usb_drv={
	.name="usbdev", 
	.id_table= usb_table,
	.probe= usb_probe,
	.disconnect=usb_disconnect,
};

int __init usb_init(void){
	pr_info("Initialization of USB driver\n");
	usb_register(&usb_drv);
	return 0;
}

void __exit usb_exit(void){
	usb_deregister(&usb_drv);
}

module_init(usb_init);
module_exit(usb_exit);



