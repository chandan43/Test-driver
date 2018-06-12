#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/netdevice.h> 
#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/rtnetlink.h>
#include <linux/delay.h>
#include "rtl_register.h"

#define DRV_NAME "8139too"
#define DRV_VERSION "0.0.1"
#define NUM_TX_DESC 4
#define RTL8139_DRIVER_NAME   DRV_NAME " Fast Ethernet driver " DRV_VERSION

/* write MMIO register, with flush */
/* Flush avoids rtl8139 bug w/ posted MMIO writes */
#define RTL_W8_F(reg, val8)	do { iowrite8 ((val8), ioaddr + (reg)); ioread8 (ioaddr + (reg)); } while (0)
#define RTL_W16_F(reg, val16)	do { iowrite16 ((val16), ioaddr + (reg)); ioread16 (ioaddr + (reg)); } while (0)
#define RTL_W32_F(reg, val32)	do { iowrite32 ((val32), ioaddr + (reg)); ioread32 (ioaddr + (reg)); } while (0)
/* write MMIO register */
#define RTL_W8(reg, val8)   iowrite8 ((val8), ioaddr + (reg))
#define RTL_W16(reg, val16) iowrite16 ((val16), ioaddr + (reg))
#define RTL_W32(reg, val32) iowrite32 ((val32), ioaddr + (reg))

/* read MMIO register */
#define RTL_R8(reg)         ioread8 (ioaddr + (reg))
#define RTL_R16(reg)        ioread16 (ioaddr + (reg))
#define RTL_R32(reg)        ioread32 (ioaddr + (reg))

struct rtl8139_priv {
	struct pci_dev *pci_dev;                /*PCI device */
	void *mmio_addr;                        /*memory mapped I/O addr */
	unsigned long regs_len;                 /* length of I/O or MMI/O region */
	unsigned int tx_flag;                   /*tx_flag shall contain transmission flags to notify the device*/
	unsigned int cur_tx;                    /*cur_tx shall hold current transmission descriptor*/
	unsigned int dirty_tx;                  /*dirty_tx denotes the first of transmission descriptors which have not completed transmission.*/
	unsigned char *tx_buf[NUM_TX_DESC];     /* Tx bounce buffers */
	unsigned char *tx_bufs;                 /* Tx bounce buffer region. */
	dma_addr_t tx_bufs_dma;
	struct net_device_stats stats;
	unsigned char *rx_ring;
	dma_addr_t rx_ring_dma;
	unsigned int cur_rx;
	struct napi_struct	napi; /* Structure for NAPI scheduling similar to tasklet but with weighting */ 
	struct net_device	*dev; /*  The NET DEVICE structure.*/
    unsigned long   fifo_copy_timeout;
};

static int rtl8139_rx(struct net_device *dev, struct rtl8139_priv *tp,
		      int budget)
{
	void __iomem *ioaddr = tp->mmio_addr;
	int received = 0;
	unsigned char *rx_ring = tp->rx_ring;
	unsigned int cur_rx = tp->cur_rx;
	unsigned int rx_size = 0;
	
    while (netif_running(dev) && received < budget &&
	       (RTL_R8 (CR) & 0x01) == 0) {
		u32 ring_offset = cur_rx % RX_BUF_LEN;
		u32 rx_status;
		unsigned int pkt_size;
		struct sk_buff *skb;
		
		rmb();
		/* read size+status of next frame from DMA ring buffer */
		rx_status = le32_to_cpu (*(__le32 *) (rx_ring + ring_offset));
		rx_size = rx_status >> 16;              /* status : First 16 bit */
		if (likely(!(dev->features & NETIF_F_RXFCS)))       /*Axpend FCS to skb pkt data */
			pkt_size = rx_size - 4;  /* first two bytes are receive status register*/
		else 
			pkt_size = rx_size;
        if (unlikely(rx_size == 0xfff0)) {
			if (!tp->fifo_copy_timeout)
				    tp->fifo_copy_timeout = jiffies + 2;
 		/* time_after(a,b) returns true if the time a is after time b.*/
			else if (time_after(jiffies, tp->fifo_copy_timeout)) {
				netdev_dbg(dev, "hung FIFO. Reset\n");
				rx_size = 0;
				goto no_early_rx;
			}
			break;
		 }
no_early_rx:
		tp->fifo_copy_timeout = 0;
		/* Malloc up new buffer, compatible with net-2e. */
		/* Omit the four octet CRC from the length. */
		skb = napi_alloc_skb(&tp->napi, pkt_size);
		if (likely(skb)) {
			skb_copy_to_linear_data (skb, &rx_ring[ring_offset + 4], pkt_size);  //memcpy(skb->data, from, len);
			
            skb_put (skb, pkt_size); /*  add data to a buffer */
			skb->protocol = eth_type_trans (skb, dev); /*  determine the packet's protocol ID. */
			netif_receive_skb (skb);/* process receive buffer from network */	
		}
		received++;
		/* update tp->cur_rx to next writing location  */
		
		cur_rx = (cur_rx + rx_size + 4 + 3) & ~3;              /*  ~3 : First two bytes are receive status register*/ 
		RTL_W16 (CAPR, (u16) (cur_rx - 16));               /* 16 byte align the IP fields  */
		
	}
	
	tp->cur_rx = cur_rx;
	if (tp->fifo_copy_timeout)
		received = budget;
	return received;	
}
static int rtl8139_poll(struct napi_struct *napi, int budget)
{
	struct rtl8139_priv *tp = container_of(napi, struct rtl8139_priv, napi);
	struct net_device *dev = tp->dev;
	void __iomem *ioaddr = tp->mmio_addr;
	int work_done;
    pr_info("%s##\n",__func__);
	work_done = 0;
	if (likely(RTL_R16(ISR) & RxOK))  /*if RxIFOOver | RxOverflow | RxOK is set then work_done ++ */
		work_done += rtl8139_rx(dev, tp, budget);
	
	if (work_done < budget) {
		/*
		 * Order is important since data can get interrupted
		 * again when we think we are done.
		 */
		__napi_complete(napi);                   //NAPI processing complete
	}

	return work_done;
}
static void rtl8139_tx_interrupt (struct net_device *dev,
				  struct rtl8139_priv *tp,
				  void __iomem *ioaddr)
{
	unsigned long dirty_tx, tx_left;

	dirty_tx = tp->dirty_tx;
	tx_left = tp->cur_tx - dirty_tx;
	pr_info("dirty_tx : %d and tx_left : %d\n",dirty_tx,tx_left);
    msleep(100000);
	while (tx_left > 0) {
        if(printk_ratelimit()) {
                pr_info("::::->>>%d\n",tx_left);
        }
		int entry = dirty_tx % NUM_TX_DESC;
		int txstatus;
		txstatus = RTL_R32 (TSD0 + (entry * sizeof (u32)));  /* TxStatus0 + entry(0-3) * discriptor size*/
		if (!(txstatus & (TxStatOK | TxUnderrun | TxAborted)))
			    break;	/* It still hasn't been Txed */
		/* Note: TxCarrierLost is always asserted at 100mbps. */
		if (txstatus & (TxOutOfWindow | TxAborted)) {
			/* There was an major error, log it. */
			dev->stats.tx_errors++;
			if (txstatus & TxAborted) {
				dev->stats.tx_aborted_errors++;
				RTL_W16 (ISR, TxErr);
				wmb();                                    /*The wmb() macro does: prevent reordering of the stores. */
			}
			if (txstatus & TxCarrierLost)
				dev->stats.tx_carrier_errors++;
			if (txstatus & TxOutOfWindow)
				dev->stats.tx_window_errors++;
		} else {
			if (txstatus & TxUnderrun) {
				/* Add 64 to the Tx FIFO threshold. */
				if (tp->tx_flag < 0x00300000)      /*if Collision Count*/
					tp->tx_flag += 0x00020000; /*Threshold level in the Tx FIFO is increased*/
				dev->stats.tx_fifo_errors++;
			}
			dev->stats.collisions += (txstatus >> 24) & 15;  /*right shit 24 times i.e Number of Collision Count & (0001(collision signal)0101 (Collision Count) ) i.e 15*/
		}
		dirty_tx++;
		tx_left--;
	}
	/* only wake the queue if we did work, and the queue is stopped */
	if (tp->dirty_tx != dirty_tx) {
		tp->dirty_tx = dirty_tx;
		mb();
		netif_wake_queue (dev); /* restart transmit */
	} 
    pr_info("At End###########################--->\n");
}
static irqreturn_t rtl8139_interrupt(int irq, void *dev_instance){
	struct net_device *dev = (struct net_device * ) dev_instance;
	struct rtl8139_priv *priv = netdev_priv(dev);
    void __iomem *ioaddr = priv->mmio_addr;
  //  unsigned short isr1 = readw(ioaddr + ISR);
    unsigned short isr = RTL_R16(ISR);
    int handled = 0;
    /* h/w no longer present (hotplug?) or major error, bail */
    if (unlikely(isr == 0xFFFF))
        goto out;
    handled = 1;
	if (unlikely(!netif_running(dev))) {
		RTL_W16 (IMR, 0);
		goto out;
	}
   /* clear all interrupt.
    * Specs says reading ISR clears all interrupts and writing
    * has no effect. But this does not seem to be case. I keep on
    * getting interrupt unless I forcibly clears all interrupt :-(
    */	
	pr_debug("rtl8139_interrupt: \n");
//    writew(0xffff,ioaddr + ISR);
	if(isr & RxErr){
		pr_err("receive err interrupt\n");
		priv->stats.rx_errors++;
	}
	if(isr & RxOK ){
		pr_info("receive interrupt received\n");
		if (napi_schedule_prep(&priv->napi)) {
			__napi_schedule(&priv->napi);
        }
	}
	if (isr & (TxOK | TxErr)) {
		pr_info("Tx  interrupt received\n");
		rtl8139_tx_interrupt (dev, priv, ioaddr);
		if (isr & TxErr)
			RTL_W16 (ISR, TxErr);
	}
	 if(isr & CableLen) pr_err("cable length change interrupt\n");
	 if(isr & TimeOut) pr_err("time interrupt\n");
	 if(isr & SysErr) pr_err("system err interrupt\n");
out:
     netdev_dbg(dev, "exiting interrupt, intr_status=%#4.4x\n",
           RTL_R16(ISR));
	 return IRQ_RETVAL(handled);
}

static void rtl8139_chip_reset (void *ioaddr){
	int i;
	pr_debug("rtl8139_chip_reset : \n");
	/* Soft reset the chip. */
	//writeb(CmdReset, ioaddr + CR);
    RTL_W8 (CR, CmdReset);
	 /* Check that the chip has finished the reset. */
	/*the memory barrier is needed to ensure that the reset happen in the expected order.*/
	for(i=1000;i>0;i--){
		barrier();
		//if((readb(ioaddr + CR) & CmdReset) == 0) break;
		if((RTL_R8 (CR) & CmdReset) == 0) 
            break;
		udelay (10);
	}

}

static void rtl8139_init_ring(struct net_device *dev){
	struct rtl8139_priv *priv = netdev_priv(dev);
	int i;
	pr_debug("rtl8139_init_ring: \n");
	priv->cur_tx = 0;
	priv->dirty_tx = 0;
	for(i = 0; i < NUM_TX_DESC;i++)
		priv->tx_buf[i] = &priv->tx_bufs[i * TX_BUF_SIZE];
}
static void rtl8139_hw_start (struct net_device *dev) {
	struct rtl8139_priv *priv = netdev_priv(dev);
	void __iomem *ioaddr = priv->mmio_addr;
	u32 i;
	pr_debug("rtl8139_hw_start: \n");
	rtl8139_chip_reset(ioaddr);
	/* Must enable Tx/Rx before setting transfer thresholds! */
	writeb(CmdTxEnb|CmdrxEnb, ioaddr + CR);
	 /* tx config */
	writel(0x00000600, ioaddr + TCR); /* DMA burst size 1024 */
	/* rx config */
	/*
	 *Bit 1 - Accept physical match packets
	 *Bit 2 - Accept multicast packets
	 *Bit 3 - Accept broadcast packets
	 *Bit 7 - WRAP. When set to 1, RTL8139 will keep moving the rest of packet data into the memory immediately after the end of Rx buffer.
	 *Bit 8-10 - Max DMA burst size per Rx DMA burst. We are configuring this to 111, which means unlimited.
	 *Bit 11-12 - Rx buffer length. We are configuring to 10 which means 32K+16 bytes. */
	 writel((1<<1)|(1<<2)|(1<<3)|(1<<7)|(7<<8)|(11<<3),ioaddr + RCR); 
	priv->cur_rx = 0;
	/* init Rx ring buffer DMA address */
	RTL_W32_F (RBSTART, priv->rx_ring_dma);
	/* init Tx buffer DMA addresses */
	/* init Tx buffer DMA addresses */
	for (i = 0; i < NUM_TX_DESC; i++)
		RTL_W32_F (TSAD0 + (i * 4), priv->tx_bufs_dma + (priv->tx_buf[i] - priv->tx_bufs));
//	for(i=0;i<NUM_TX_DESC;i++){
//		writel(priv->tx_bufs_dma+(priv->tx_buf[i]- priv->tx_bufs),ioaddr + TSAD0 + (i*4));
//	}
	/* init RBSTART */
//	writel(priv->rx_ring_dma, ioaddr + RBSTART);
	/* initialize missed packet counter */
	writel(0, ioaddr + MPC);
    
	int tmp = RTL_R8 (CR);
	if ((!(tmp & CmdRxEnb)) || (!(tmp & CmdTxEnb)))
		RTL_W8 (CR, CmdRxEnb | CmdTxEnb);
	/* no early-rx interrupts. Configure the device for not generating early interrupts.*/
	writew((readw(ioaddr + MULINT) & 0xF000), ioaddr + MULINT);
	/* Enable all known interrupts by setting the interrupt mask. */
	writel(INT_MASK,ioaddr + IMR);
}
#define TX_FIFO_THRESH 256
static int rtl8139_open(struct net_device *dev){
	int retval;
	struct rtl8139_priv *tc=netdev_priv(dev);
    const int irq = tc->pci_dev->irq;	
	pr_debug("rtl8139_open: \n");
	/* get the IRQ
    	* second arg is interrupt handler
    	* third is flags, 0 means no IRQ sharing  requesting function is either 0 to indicate success */
	retval=request_irq(irq,(irq_handler_t)rtl8139_interrupt,IRQF_SHARED,dev->name,dev);
	//retval=request_irq(dev->irq,(irq_handler_t)rtl8139_interrupt,0,dev->name,dev);
	if(retval)
		return retval;
	/* get memory for Tx buffers
	 * memory must be DMAable  */
	/*
	 * To allocate and map large (PAGE_SIZE or so) consistent DMA regions,
	 * you should do: dma_addr_t dma_handle; cpu_addr = pci_alloc_consistent(dev, size, &dma_handle);*/
	//tc->tx_bufs = pci_alloc_consistent(tc->pci_dev,TOTAL_TX_BUF_SIZE,&tc->tx_bufs_dma);
	//tc->rx_ring = pci_alloc_consistent(tc->pci_dev,RX_BUF_TOT_LEN,&tc->rx_ring_dma);
    tc->tx_bufs = dma_alloc_coherent(&tc->pci_dev->dev, TOTAL_TX_BUF_SIZE,
                       &tc->tx_bufs_dma, GFP_KERNEL);
    tc->rx_ring = dma_alloc_coherent(&tc->pci_dev->dev, RX_BUF_TOT_LEN,
                         &tc->rx_ring_dma, GFP_KERNEL);
	if((!tc->tx_bufs) || (!tc->rx_ring)){
		free_irq(dev->irq, dev);
		if(tc->tx_bufs){
//			pci_free_consistent(tc->pci_dev,TOTAL_TX_BUF_SIZE,tc->tx_bufs,tc->tx_bufs_dma);
			dma_free_coherent(&tc->pci_dev->dev, TOTAL_TX_BUF_SIZE,
			                        tc->tx_bufs, tc->tx_bufs_dma);
			tc->tx_bufs = NULL;
		}
		if(tc->rx_ring){
                dma_free_coherent(&tc->pci_dev->dev, RX_BUF_TOT_LEN,
                        tc->rx_ring, tc->rx_ring_dma);
//			 pci_free_consistent(tc->pci_dev,RX_BUF_TOT_LEN,tc->rx_ring,tc->rx_ring_dma);
			 tc->rx_ring = NULL;
		}
		return -ENOMEM;
	}
	//tc->tx_flag=0;
	tc->tx_flag=(TX_FIFO_THRESH << 11) & 0x003f0000;;
	napi_enable(&tc->napi);  /*enable NAPI scheduling*/
	rtl8139_init_ring(dev);
	rtl8139_hw_start(dev);
	netif_start_queue (dev); // allow transmit
	return 0;
}

static int rtl8139_stop(struct net_device *dev){
	struct rtl8139_priv *priv = netdev_priv(dev);
	struct pci_dev *pdev= priv->pci_dev;
    void __iomem *ioaddr = priv->mmio_addr;
	netif_stop_queue(dev);  /*Stop transmitted packets*/
	napi_disable(&priv->napi);
	/* Stop the chip's Tx and Rx DMA processes. */
	RTL_W8 (CR, 0);
	
	/* Disable interrupts by clearing the interrupt mask. */
	RTL_W16 (IMR, 0);
	
	//RTL_W32 (RxMissed, 0);
	free_irq(pdev->irq, dev);
	priv->cur_tx = 0;
	priv->dirty_tx = 0;
//	pci_free_consistent(priv->pci_dev,TOTAL_TX_BUF_SIZE,priv->tx_bufs,priv->tx_bufs_dma);
//	pci_free_consistent(priv->pci_dev,RX_BUF_TOT_LEN,priv->rx_ring,priv->rx_ring_dma);
	dma_free_coherent(&priv->pci_dev->dev, TOTAL_TX_BUF_SIZE,
			                        priv->tx_bufs, priv->tx_bufs_dma);
    dma_free_coherent(&priv->pci_dev->dev, RX_BUF_TOT_LEN,
                        priv->rx_ring, priv->rx_ring_dma);
	priv->rx_ring = NULL;
	priv->tx_bufs = NULL;
	pr_info("rtl8139 Device Stoped\n");
	
    return 0;
}

static int rtl8139_start_xmit(struct sk_buff *skb,struct net_device *dev){
	struct rtl8139_priv *priv = netdev_priv(dev);
	void __iomem *ioaddr = priv->mmio_addr;
	unsigned int entry = priv->cur_tx  % NUM_TX_DESC;
	unsigned int len = skb->len;
	pr_debug("rtl8139_start_xmit: \n");
	if (len < TX_BUF_SIZE) {
		if(len<ETH_MIN_LEN) memset(priv->tx_buf[entry],0,ETH_MIN_LEN);
		skb_copy_and_csum_dev(skb,priv->tx_buf[entry]); // which copies the packet contents to the DMA capable memory.
		dev_kfree_skb_any(skb);
		//dev_kfree_skb(skb);
	}
	else{
		dev_kfree_skb_any(skb);
		//dev_kfree_skb(skb);
        dev->stats.tx_dropped++;
		return 0;
	}
	writel(priv->tx_flag|max(len, (unsigned int)ETH_MIN_LEN),ioaddr + TSD0 + (entry *sizeof (u32)));
	entry++;
	priv->cur_tx = entry % NUM_TX_DESC;
	if (priv->cur_tx == priv->dirty_tx){
		netif_stop_queue(dev);
	}
	return NETDEV_TX_OK;
}

static struct net_device_stats* rtl8139_get_stats(struct net_device *dev){
	struct rtl8139_priv *priv=netdev_priv(dev);
	void __iomem *ioaddr = priv->mmio_addr;
	if(netif_running(dev)){
		priv->stats.rx_missed_errors += readl(ioaddr+MPC);
		writel(0,ioaddr+MPC); 
	}
	return  &(priv->stats);
}
struct net_device_ops rtl8139_device_ops={
	.ndo_open       = rtl8139_open,
	.ndo_stop       = rtl8139_stop,
	.ndo_start_xmit = rtl8139_start_xmit,
	.ndo_get_stats  = rtl8139_get_stats,
};

static int rtl8139_init_one(struct pci_dev *pdev,
                    const struct pci_device_id *ent)
{
	unsigned long mmio_start, mmio_end, mmio_len, mmio_flags;
	void __iomem *ioaddr;
	struct rtl8139_priv *priv;
    struct device *d = &pdev->dev;
    struct net_device *ndev;
	int i,rc;
	printk("%s: Initialization of REALTEK Network Device driver\n",__func__);
	
	ndev=alloc_etherdev(sizeof(*priv)); /*alloc_etherdev(sizeof_priv)*/
	if(!ndev){
		pr_err("Couldn't allocate etherdev\n");
		return -1;
	}
    SET_NETDEV_DEV(ndev, &pdev->dev);
	/*Get network device private data*/
	priv=netdev_priv(ndev);  /*dev_priv - access network device private data @ndev: network device */
	priv->pci_dev = pdev; 
    /* enable device (incl. PCI PM wakeup and hotplug setup) */
    rc = pci_enable_device (pdev);
    if (rc)
        goto err_out; 
    rc = pci_request_regions (pdev, DRV_NAME);
    if (rc)
        goto err_out; 
    pci_set_master (pdev);
	priv=netdev_priv(ndev);   /* rtl8139 private information */
	/* get PCI memory mapped I/O space base address from BAR1 */
	mmio_start=pci_resource_start(pdev, 1); /*The function returns the first address (memory address or I/O port number) associated with one of the six PCI I/O regions*/
	/*The function returns the last address that is part of the I/O region number bar. Note that this is the last usable address, not the first address after the region.*/
	mmio_end=pci_resource_end(pdev, 1);
	mmio_len=pci_resource_len(pdev,1);
	mmio_flags=pci_resource_flags(pdev, 1); /*This function returns the flags associated with this resource.*/
    dev_dbg(d, "%s region size = 0x%02lX\n",__func__, mmio_len);
	/*it is memory( or anything that can be mapped as memory :-)) then it's IORESOURCE_MEM #define IORESOURCE_MEM	0x00000200 in ioport.h*/
	if(!(mmio_flags & IORESOURCE_MEM)){
		pr_err("Region is not MMI/O region\n");
		goto err_out;
	}
	/* Enable bus mastering of the PCI device to enable the device to initiate transactions  */
	 /* ioremap MMI/O region */
	/*it must first set up an appropriate kernel page-table mapping  I/O memory may or may not be accessed through page tables */
	/*When access passes though page tables, the kernel must first arrange for the physical address to be visible from your driver, 
	 * and this usually means that you must call ioremap before doing any I/O. #include <asm/io.h>*/
	//ioaddr=ioremap(mmio_start, mmio_len);
	ioaddr = pci_iomap(pdev, 1, 0);
    pr_info("%s ioaddr  %p\n",__func__,ioaddr);
	if(!ioaddr){
		pr_err("Couldn't ioremap\n");
		goto cleanup2;
	}
	netif_napi_add(ndev, &priv->napi, rtl8139_poll, 64); /* Initialize a NAPI context */	
	ndev->base_addr=(long)ioaddr;
	priv->mmio_addr=ioaddr;
	priv->regs_len=mmio_len;
    priv->dev = ndev; 
	/* UPDATE NET_DEVICE */
	for(i=0;i<6;i++){ /* Hardware Address */
		ndev->dev_addr[i]=readb((const volatile void *)ndev->base_addr + i);
		ndev->broadcast[i]=0xff;
	}
	/*The "hardware header length" is the number of octets that lead the transmitted packet before IP header, or other protocol information. The value of hard_header_len is 14 for Et	  hernet interfaces. */
	ndev->hard_header_len = 14; 
	memcpy(ndev->name, DRV_NAME, sizeof(DRV_NAME)); /* Device Name */
	ndev->irq = pdev->irq;  /* Interrupt Number */
	ndev->netdev_ops = &rtl8139_device_ops;
	 /* register the device */
	if(register_netdev(ndev)){
		pr_err("Couldn't register netdevice\n");
		goto cleanup0;
	}
    pci_set_drvdata (pdev, ndev);
	return 0;
cleanup0: 
	 netif_napi_del(&priv->napi); /* remove a napi context */
	 free_netdev(ndev);
//cleanup1:
     if (priv->mmio_addr)
	    iounmap(priv->mmio_addr);
cleanup2:
	pci_release_regions(priv->pci_dev);
err_out:
    pci_disable_device (pdev);
    return 0;
}
static void rtl8139_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata (pdev);
    struct rtl8139_priv *priv = netdev_priv(dev);   /* rtl8139 private information */
	netif_napi_del(&priv->napi);
	unregister_netdev(dev);
    if (priv->mmio_addr)
		pci_iounmap (pdev, priv->mmio_addr);
	pci_release_regions(pdev);
	free_netdev(dev);
	pci_disable_device(pdev);
	printk("%s: Cleanup module is executed well\n",__func__);
}

static DEFINE_PCI_DEVICE_TABLE(rtl8139_pci_tbl) = {
    {0x10ec, 0x8139, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
    {0,}
};
MODULE_DEVICE_TABLE (pci, rtl8139_pci_tbl);
static struct pci_driver rtl8139_pci_driver = {
    .name       = DRV_NAME,
    .id_table   = rtl8139_pci_tbl,
    .probe      = rtl8139_init_one,
    .remove     = rtl8139_remove_one,
};


static int __init rtl8139_init_module (void)
{
    /* when we're a module, we always print a version message,
 *   * even if no 8139 board is found.
 *       */
#ifdef MODULE
    pr_info(RTL8139_DRIVER_NAME "\n");
#endif

    return pci_register_driver(&rtl8139_pci_driver);
}


static void __exit rtl8139_cleanup_module (void)
{
    pci_unregister_driver (&rtl8139_pci_driver);
}


MODULE_LICENSE("GPL");
MODULE_AUTHOR("beingchandanjha@gmail.com");
MODULE_DESCRIPTION("Basic Network driver");
MODULE_VERSION(".1");

module_init(rtl8139_init_module);
module_exit(rtl8139_cleanup_module);


/*https://github.com/profglavcho/mt6577-kernel-3.10.65/blob/master/Documentation/PCI/pci.txt*/

