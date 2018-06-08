#include <linux/module.h>
#include <linux/init.h>
#include <linux/compiler.h>
#include <linux/pci.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>
#include <linux/rtnetlink.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/rtnetlink.h>
#include <linux/completion.h>
#include <linux/completion.h>
#include <linux/crc32.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/gfp.h>
#include <asm/irq.h>

#define DRV_NAME	"realtek8139"
#define DRV_VERSION	".1"
#define RTL8139_DRIVER_NAME   DRV_NAME " Fast Ethernet driver " DRV_VERSION
//================>
/*The message level was not precisely defined past level 3, but were
 always implemented within +-1 of the specified level.  Drivers tended
 to shed the more verbose level messages as they matured.
    0  Minimal messages, only essential information on fatal errors.
    1  Standard messages, initialization status.  No run-time messages
    2  Special media selection messages, generally timer-driver.
    3  Interface starts and stops, including normal status messages
    4  Tx and Rx frame error messages, and abnormal driver operation
    5  Tx packet queue information, interrupt events.
    6  Status on each completed Tx packet and received Rx packets
    7  Initial contents of Tx and Rx packets
The set of message levels is named
  Old level   Name          Bit position
    0    NETIF_MSG_DRV		0x0001
    1    NETIF_MSG_PROBE	0x0002
    2    NETIF_MSG_LINK		0x0004
    2    NETIF_MSG_TIMER	0x0004
    3    NETIF_MSG_IFDOWN	0x0008
    3    NETIF_MSG_IFUP		0x0008
    4    NETIF_MSG_RX_ERR	0x0010
    4    NETIF_MSG_TX_ERR	0x0010
    5    NETIF_MSG_TX_QUEUED	0x0020
    5    NETIF_MSG_INTR		0x0020
    6    NETIF_MSG_TX_DONE	0x0040
    6    NETIF_MSG_RX_STATUS	0x0040
    7    NETIF_MSG_PKTDATA	0x0080
 */
/*The design of the debugging message interface was guided and constrained by backwards compatibility previous practice.*/
/* Default Message level */
#define RTL8139_DEF_MSG_ENABLE   (NETIF_MSG_DRV   |  NETIF_MSG_PROBE  | NETIF_MSG_LINK)


/* define to 1, 2 or 3 to enable copious debugging info */
#define RTL8139_DEBUG 0

/* define to 1 to disable lightweight runtime debugging checks */
#undef RTL8139_NDEBUG

/*
likely() and unlikely() are macros that Linux kernel developers use to give hints to the compiler and chipset. 
Modern CPUs have extensive branch-prediction heuristics that attempt to predict incoming commands in order to 
optimize speed. The likely() and unlikely() macros allow the developer to tell the CPU, through the compiler, 
that certain sections of code are likely, and thus should be predicted, or unlikely, so they shouldn't be predicted. 
They are defined in include/linux/compiler.h:
*/


#ifdef RTL8139_NDEBUG
#  define assert(expr) do {} while (0)
#else
#  define assert(expr) if (unlikely(!(expr))) { pr_err("Assertion failed! %s,%s,%s,line=%d\n",	#expr, __FILE__, __func__, __LINE__);}
#endif

/*
 * Receive ring size
 * Warning: 64K ring has hardware issues and may lock up.
 */
#if defined(CONFIG_SH_DREAMCAST)        /*Use declared coherent memory for dreamcast pci ethernet adapter*/
#define RX_BUF_IDX 0	/* 8K ring */
#else
#define RX_BUF_IDX	2	/* 32K ring */
#endif
#define RX_BUF_LEN	(8192 << RX_BUF_IDX)
#define RX_BUF_PAD	16   /* see 11th and 12th bit of RCR: 0x44 */
#define RX_BUF_WRAP_PAD 2048 /* spare padding to handle lack of packet wrap */

#if RX_BUF_LEN == 65536
#define RX_BUF_TOT_LEN	RX_BUF_LEN
#else
#define RX_BUF_TOT_LEN	(RX_BUF_LEN + RX_BUF_PAD + RX_BUF_WRAP_PAD)
#endif

/* Number of Tx descriptor registers.  The transmit path of RTL8139(A/B) use 4 descriptors .*/
#define NUM_TX_DESC	4

/* max supported ethernet frame size -- must be at least (dev->mtu+18+4).*/
#define MAX_ETH_FRAME_SIZE	1792

/* max supported payload size */
/* #define ETH_FCS_LEN     4           Octets in the FCS(frame check sequence)
   #define VLAN_ETH_HLEN   18           Total octets in header in if_vlan.h.       
*/
#define MAX_ETH_DATA_SIZE (MAX_ETH_FRAME_SIZE - VLAN_ETH_HLEN - ETH_FCS_LEN)    

/* Size of the Tx bounce buffers -- must be at least (dev->mtu+18+4). */
#define TX_BUF_SIZE	MAX_ETH_FRAME_SIZE
#define TX_BUF_TOT_LEN	(TX_BUF_SIZE * NUM_TX_DESC)

/* PCI Tuning Parameters Threshold is bytes transferred to chip before transmission starts. */
#define TX_FIFO_THRESH 256	/* In bytes, rounded down to 32 byte units. */
/* The following settings are log_2(bytes)-4:  0 == 16 bytes .. 6==1024, 7==end of packet. */

#define RX_FIFO_THRESH	7	/* Rx buffer level before first PCI xfer. Receive Configuration Register (Bit 15-13: RXFTH2, 1, 0) */
#define RX_DMA_BURST	7	/* Maximum PCI burst, '6' is 1024 (Receive Configuration Register ,BIT 10-8: MXDMA2, 1, 0)*/
#define TX_DMA_BURST	6	/* Maximum PCI burst, '6' is 1024 (Transmit Configuration Register , 10-8 MXDMA2, 1, 0)*/
#define TX_RETRY	8	/* 0-15.  retries = 16 + (TX_RETRY * 16) ( Transmit Configuration Register: Bit 7-4 ,TXRR)*/

/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (6*HZ)
/*
	{ "100/10M Ethernet PCI Adapter",	HAS_CHIP_XCVR },
	{ "1000/100/10M Ethernet PCI Adapter",	HAS_MII_XCVR },
*/
enum {
	HAS_MII_XCVR = 0x010000,
	HAS_CHIP_XCVR = 0x020000,
	HAS_LNK_CHNG = 0x040000,
};
#define RTL_NUM_STATS 4		/* number of ETHTOOL_GSTATS u64's */
#define RTL_REGS_VER 1		/* version of reg. data in ETHTOOL_GREGS */
#define RTL_MIN_IO_SIZE 0x80
#define RTL8139B_IO_SIZE 256

#define RTL8129_CAPS	HAS_MII_XCVR
#define RTL8139_CAPS	(HAS_CHIP_XCVR|HAS_LNK_CHNG)


enum {
	RTL8139 = 0,
	RTL8129,
}board_t;
/* indexed by board_t, above */
static const struct {
	const char *name;
	u32 hw_flags;
} board_info[] = {
	{ "RealTek RTL8139", RTL8139_CAPS },
	{ "RealTek RTL8129", RTL8129_CAPS },
};


/*# (vendorID, deviceID, subvendor, subdevice, class, class_mask driver_data)
 *  vendorID and deviceID : This 16-bit register identifies a hardware manufacturer and This is another 16-bit register, selected by the manufacturer; no official registration is require    d for the device ID. 
 *  subvendor and subdevice:These specify the PCI subsystem vendor and subsystem device IDs of a device. If a driver can handle any type of subsystem ID, the value PCI_ANY_ID should be u    sed for these fields.
 *  class AND class_mask :These two values allow the driver to specify that it supports a type of PCI class device. The different classes of PCI devices (a VGA controller is one example)    are described in the    PCI specification. If    a driver can handle any type of subsystem ID, the value PCI_ANY_ID should be used for these fields.
 *  driver_data :This value is not used to match a device but is used to hold information that the PCI driver can use to differentiate between different devices if it wants to.
 *  Ref : http://www.makelinux.net/ldd3/chp-12-sect-1
 */
static const struct pci_device_id rtl8139_pci_tbl[] = {
	{0x10ec, 0x8139, PCI_ANY_ID, PCI_ANY_ID, 0, 0, RTL8139},
	/* some crazy cards report invalid vendor ids like
	 * 0x0001 here.  The other ids are valid and constant,
	 * so we simply don't match on the main vendor id.
	 */
	{PCI_ANY_ID, 0x8139, 0x10ec, 0x8139, 0, 0, RTL8139 },
	{0,}
};
MODULE_DEVICE_TABLE (pci, rtl8139_pci_tbl);
/* The rest of these values should never change. */

/* Symbolic offsets to registers. */
enum RTL8139_registers {
	MAC0		= 0,	 /* Ethernet hardware address. */
	MAR0		= 8,	 /* Multicast filter. */
	TxStatus0	= 0x10,	 /* Transmit status (Four 32bit registers). */
	TxAddr0		= 0x20,	 /* Tx descriptors (also four 32bit). */
	RxBuf		= 0x30,  /* Receive (Rx) Buffer Start Address*/
	ChipCmd		= 0x37,  /* Command Register*/
	RxBufPtr	= 0x38,  /* Current Address of Packet Read*/
	RxBufAddr	= 0x3A,  /* Current Buffer Address: The initial value is 0000h. It reflects total received byte-count in the rx buffer.*/
	IntrMask	= 0x3C,  /* Interrupt Mask Register*/
	IntrStatus	= 0x3E,  /* Interrupt Status Register*/
	TxConfig	= 0x40,  /* Transmit (Tx) Configuration Register*/
	RxConfig	= 0x44,  /* Receive (Rx) Configuration Register*/
	Timer		= 0x48,	 /* A 32-bit general-purpose counter.Timer CounT Register Writing any value to this 32-bit register will reset the original timer and begin to count from zero.*/
	RxMissed	= 0x4C,  /* 24 bits valid, write clears. indicates the number of packets discarded due to Rx FIFO overflow. After s/w reset, MPC is cleared. Only the lower 3 bytes are valid.*/
	Cfg9346		= 0x50,  /* 93C46 Command Register*/
	Config0		= 0x51,  /* Configuration Register 0*/
	Config1		= 0x52,  /* Configuration Register 1*/
	TimerInt	= 0x54,  /* Timer Interrupt Register.*/
	MediaStatus	= 0x58,  /* Media Status Register*/
	Config3		= 0x59,  /* Configuration register 3*/
	Config4		= 0x5A,	 /* absent on RTL-8139A  : Configuration register 4*/
	HltClk		= 0x5B,  /* Reserved*/
	MultiIntr	= 0x5C,  /* Multiple Interrupt Select*/
	TxSummary	= 0x60,  /* Transmit Status of All Descriptors*/
	BasicModeCtrl	= 0x62,  /* Basic Mode Control Register*/
	BasicModeStatus	= 0x64,  /* Basic Mode Status Register*/
	NWayAdvert	= 0x66,  /* Auto-Negotiation Advertisement Register*/
	NWayLPAR	= 0x68,  /* Auto-Negotiation Link Partner Register*/
	NWayExpansion	= 0x6A,  /* Auto-Negotiation Expansion Register*/
	/* Undocumented registers, but required for proper operation. */
	FIFOTMS		= 0x70,	 /* FIFO Control and test. : N-way Test Register*/
	CSCR		= 0x74,	 /* Chip Status and Configuration Register. */
	PARA78		= 0x78,  /* PHY parameter 1 :EPROM Reg*/
	FlashReg	= 0xD4,	/* Communication with Flash ROM, four bytes. */
	PARA7c		= 0x7c,	 /* Magic transceiver parameter register. :Twister parameter*/
	Config5		= 0xD8,	 /* absent on RTL-8139A */
};
/*MultiInt is true when MulERINT=0 (bit17, RCR). When MulERINT=1, any received packetinvokes early interrupt according to the MISR[11:0] setting in early mode*/
/*Command Register : Bit : 0 -R- BUFE- Buffer Empty , 1-Reserved ,2-R/W-TE,Transmitter Enable: When set to 1,3-R/W-RE,Receiver Enable: When set to 1, 4-R/W-RST-Reset: Setting to 1 forces,7-5:Reserved*/
/*Config1Clear: Bit : 0 R/W PMEn - Power Management Enable(No change), 1 R/W VPD Set to enable Vital Product Data,2-R-IOMAP: I/O Mapping, 3-R-MEMMAP : Memory Mapping ,4-R/W LWACT: LWAKE active mode: 5-R/W-DV RLOAD : Driver Load, 7-6 : R/W- LEDS1-0 Refer to LED PIN definition*/
enum ClearBitMasks {
	MultiIntrClear	= 0xF000, /* Multiple Interrupt Select Register : Bit 11 to 0 , 1 to set and 0 for Clear , 15-12 : Reserved*/   
	ChipCmdClear	= 0xE2,   /* Reset: 111(bit 5-7)0001(bit 1)0 ,Reserved bit will be set ,otherwise clear*/ 
	Config1Clear	= (1<<7)|(1<<6)|(1<<3)|(1<<2)|(1<<1), /*Bit 0,4,5 values are bydefault 0 so no need to change otherwise flip*/ 
};
enum ChipCmdBits {
	CmdReset	= 0x10,  /*Bit : 4-R/W-RST-Reset: Setting to 1 forces*/
	CmdRxEnb	= 0x08,  /*Bit : 3-R/W-RE,Receiver Enable: When set to 1*/
	CmdTxEnb	= 0x04,  /*BIT : 2-R/W-TE,Transmitter Enable: When set to 1 */
	RxBufEmpty	= 0x01,  /*Bit : 0 -R- BUFE- Buffer Empty  */
};
/* Interrupt register bits, using my own meaningful names. */
enum IntrStatusBits {
	PCIErr		= 0x8000,/*BIT : 15-R/W-SERR :Set to 1 when the RTL8139D(L) signals a system error on the PCI bus.*/ 
	PCSTimeout	= 0x4000,/*BIT : 14 R/W TimeOut: Set to 1 when the TCTR register reaches to the value of the TimerInt register.*/ 
	RxFIFOOver	= 0x40,  /*BIT : 6 R/W FOVW: Rx FIFO Overflow: Set when an overflow occurs on the Rx status FIFO.*/
	RxUnderrun	= 0x20,  /*BIT : 5 R/W PUN/LinkChg : Packet Underrun/Link Change: Set to 1 when CAPR is written but Rx buffer is empty, or when link status is changed.*/
	RxOverflow	= 0x10,  /*BIT : 4 R/W RXOVW : Rx Buffer Overflow: Set when receive (Rx) buffer ring storage resources have been exhausted.*/
	TxErr		= 0x08,  /*BIT : 3 R/W TER : Transmit (Tx) Error: Indicates that a packet transmission was aborted, due to excessive collisions, according to the TXRR's setting.*/
	TxOK		= 0x04,  /*BIT : 2 R/W TOK : Transmit (Tx) OK: Indicates that a packet transmission is completed successfully.*/
	RxErr		= 0x02,  /*BIT : 1 R/W RER : Receive (Rx) Error: Indicates that a packet has either CRC error or frame alignment error (FAE). */
	RxOK		= 0x01,  /*BIT : 0 R/W ROK : Receive (Rx) OK: In normal mode, indicates the successful completion of a packet reception.*/

	RxAckBits	= RxFIFOOver | RxOverflow | RxOK,
};
/*1. BIT : 13 R/W OWN The RTL8139D(L) sets this bit to 1 when the Tx DMA operation of this descriptor was completed. The driver must set this bit to 0 when the Transmit Byte Count (bits 0-12) is written. The     default value is 1.
  2. BIT : 14 R/W SIZE : Transmit FIFO Underrun: Set to 1 if the Tx FIFO was exhausted during the transmission of a packet.
  3. BIT : 15 R/W TOK  : Transmit OK: Set to 1 indicates that the transmission of a packet was completed successfully and no transmit underrun has occurred.
  4. BIT : 29 R OWC    : Out of Window Collision: This bit is set to 1 if the RTL8139D(L) encountered an "out of window" collision during the transmission of a packet.
  5. BIT : 30 R TABT   : Transmit Abort: This bit is set to 1 if the transmission of a packet was aborted. This bit is read only, writing to this bit is not affected.
  6. BIT : 31 R CRS    : Carrier Sense Lost: This bit is set to 1 when the carrier is lost during transmission of a packet.   
*/ 
enum TxStatusBits {
	TxHostOwns	= 0x2000, 
	TxUnderrun	= 0x4000, 
	TxStatOK	= 0x8000,
	TxOutOfWindow	= 0x20000000,
	TxAborted	= 0x40000000,
	TxCarrierLost	= 0x80000000,
};
/*
 1. BIT : 15 R MAR : Multicast Address Received: This bit set to 1 indicates that a multicast packet is received.
 2. BIT : 14 R PAM : Physical Address Matched: This bit set to 1 indicates that the destination address of this packet matches the value written in ID registers.
 3. BIT : 13 R BAR : Broadcast Address Received: This bit set to 1 indicates that a broadcast packet is received. BAR, MAR bit will not be set simultaneously. 
 4. BIT : 5 R ISE  : Invalid Symbol Error: (100BASE-TX only) This bit set to 1 indicates that an invalid symbol was encountered during the reception of this packet. 
 5. BIT : 4 R RUNT : Runt Packet Received: This bit set to 1 indicates that the received packet length is smaller than 64 bytes ( i.e. media header + data + CRC < 64 bytes )
 6. BIT : 3 R LONG : Long Packet: This bit set to 1 indicates that the size of the received packet exceeds 4k bytes.
 7. BIT : 2 R CRC  : CRC Error: When set, indicates that a CRC error occurred on the received packet.
 8. BIT : 1 R FAE  : Frame Alignment Error: When set, indicates that a frame alignment error occurred on this received packet.
 9. BIT : 0 R ROK  : Receive OK: When set, indicates that a good packet is received.
*/
enum RxStatusBits {
	RxMulticast	= 0x8000,
	RxPhysical	= 0x4000,
	RxBroadcast	= 0x2000,
	RxBadSymbol	= 0x0020,
	RxRunt		= 0x0010,
	RxTooLong	= 0x0008,
	RxCRCErr	= 0x0004,
	RxBadAlign	= 0x0002,
	RxStatusOK	= 0x0001,
};
/*

Receive Configuration Register: 
AcceptErr : bit 5 Accept Error Packets : 1: Accept error packets
AcceptRunt : bit 4 Accept Runt Packets: 1: Accept runt packets
AcceptBroadcast :bit 3 Accept Broadcast Packets 
AcceptMulticast : bit 2 Accept Multicast Packets:
AcceptMyPhys : bit 1 Accept Physical Match Packets
AcceptAllPhys : bit 0 Accept Physical Address Packets

*/

/* Bits in RxConfig. */
enum rx_mode_bits {
	AcceptErr	= 0x20,
	AcceptRunt	= 0x10,
	AcceptBroadcast	= 0x08,
	AcceptMulticast	= 0x04,
	AcceptMyPhys	= 0x02,
	AcceptAllPhys	= 0x01,
};

/* Bits in TxConfig. */
/* Interframe Gap Time: This field allows the user to adjust the interframe gap time below the standard: 9.6 us for 10Mbps, 960 ns for 100Mbps. The time can be programmed from 9.6 us to 8.4 us (10Mbps)
   and 960ns to 840ns (100Mbps). Note that any value other than (1, 1) will violate the IEEE 802.3 standard. The formula for the inter frame gap is:
   10 Mbps:  8.4us + 0.4(IFG(1:0)) us , 100 Mbps: 840ns + 40(IFG(1:0)) ns
   BIT 18, 17 : R/W LBK1, LBK0 : 00 : normal operation , 01 : Reserved , 10 : Reserved 11 : Loopback mode
   BIT 0 W CLRABT : Clear Abort: Setting this bit to 1 causes the RTL8139D(L) to retransmit the packet at the last transmitted descriptor when this transmission was aborted, Setting this bit is only 
   permitted in the transmit abort state. 
*/
enum tx_config_bits {
        /* Interframe Gap Time. Only TxIFG96 doesn't violate IEEE 802.3 */
        TxIFGShift	= 24,                /* BIT 24-25 IFG1, 0 : R/W Interframe Gap Time: */  
        TxIFG84		= (0 << TxIFGShift), /* 8.4us / 840ns (10 / 100Mbps) */
        TxIFG88		= (1 << TxIFGShift), /* 8.8us / 880ns (10 / 100Mbps) */
        TxIFG92		= (2 << TxIFGShift), /* 9.2us / 920ns (10 / 100Mbps) */
        TxIFG96		= (3 << TxIFGShift), /* 9.6us / 960ns (10 / 100Mbps) */

	TxLoopBack	= (1 << 18) | (1 << 17), /* enable loopback test mode */
	TxCRC		= (1 << 16),	/* DISABLE Tx pkt CRC append */
	TxClearAbt	= (1 << 0),	/* Clear abort (WO) */
	TxDMAShift	= 8, /* DMA burst value (0-7) is shifted X many bits */
	TxRetryShift	= 4, /* TXRR value (0-15) is shifted X many bits */

	TxVersionMask	= 0x7C800000, /* mask out version bits 30-26, 23 0111   */
};
/* Bits in Config1 */
/*Config1Bits: 
              Bit : 0 R/W PMEn - Power Management Enable, 
                    1 R/W VPD Set to enable Vital Product Data,
                    2-R-IOMAP: I/O Mapping, 
                    3-R-MEMMAP : Memory Mapping ,
                    4-R/W LWACT: LWAKE active mode: 
                    5-R/W-DV RLOAD : Driver Load, 
                    7-6 : R/W- LEDS1-0 Refer to LED PIN definition
*/
enum Config1Bits {
	Cfg1_PM_Enable	= 0x01,
	Cfg1_VPD_Enable	= 0x02,
	Cfg1_PIO	= 0x04,
	Cfg1_MMIO	= 0x08,
	LWAKE		= 0x10,		/* not on 8139, 8139A */
	Cfg1_Driver_Load= 0x20,
	Cfg1_LED0	= 0x40,
	Cfg1_LED1	= 0x80,
	SLEEP		= (1 << 1),/* only on 8139, 8139A : Set to enable Vital Product Data: The VPD data is stored in 93C46 from within offset 40h-7Fh.*/
	PWRDN		= (1 << 0),/* only on 8139, 8139A : 1  means A(bit 4 of the Status Register) in the PCI)=1, B(Cap_Ptr register)=50h, C(power management)=01h, D(PM registers) valid, E=0*/
};

/* Bits in Config3 */
enum Config3Bits {
	Cfg3_FBtBEn   	= (1 << 0), /* 1	= Fast Back to Back */
	Cfg3_FuncRegEn	= (1 << 1), /* 1	= enable CardBus Function registers */
	Cfg3_CLKRUN_En	= (1 << 2), /* 1	= enable CLKRUN */
	Cfg3_CardB_En 	= (1 << 3), /* 1	= enable CardBus registers */
	Cfg3_LinkUp   	= (1 << 4), /* 1	= wake up on link up */
	Cfg3_Magic    	= (1 << 5), /* 1	= wake up on Magic Packet (tm) */
	Cfg3_PARM_En  	= (1 << 6), /* 0	= software can set twister parameters */
	Cfg3_GNTSel   	= (1 << 7), /* 1	= delay 1 clock from PCI GNT signal */
};

/* Bits in Config4 */
enum Config4Bits {
	LWPTN	= (1 << 2),	/* not on 8139, 8139A : LWAKE pattern: Please refer to LWACT bit : 4 in CONFIG1 register. */
};

/* Bits in Config5 */
enum Config5Bits {
	Cfg5_PME_STS   	= (1 << 0), /* 1	= PCI reset resets PME_Status */
	Cfg5_LANWake   	= (1 << 1), /* 1	= enable LANWake signal */
	Cfg5_LDPS      	= (1 << 2), /* 0	= save power when link is down */
	Cfg5_FIFOAddrPtr= (1 << 3), /* Realtek internal SRAM testing : 1: Both Rx and Tx FIFO address pointers are updated in descending , */
	Cfg5_UWF        = (1 << 4), /* 1 = accept unicast wakeup frame */
	Cfg5_MWF        = (1 << 5), /* 1 = accept multicast wakeup frame */
	Cfg5_BWF        = (1 << 6), /* 1 = accept broadcast wakeup frame */
};
/*
1.Rx FIFO Threshold: 15-13 R/W RXFTH2, 1, 0   Specifies Rx FIFO Threshold level ,Whenever Rx FIFO, has reached to this level the receive PCI bus master function
  will begin to transfer the data from the FIFO to the host memory . 
  111 = no rx threshold. begins the transfer of data after having received a whole packet in the FIFO
2.Max DMA Burst Size per Rx DMA Burst: 10-8 R/W MXDMA2, 1, 0, This field sets the maximum size of the receive DMA data bursts according to the following table:
  111 = unlimited
3.Rx Buffer Length: 12-11 R/W RBLEN1, 0  ,This field indicates the size of the Rx ring buffer. 00 = 8k + 16 byte 01 = 16k + 16 byte 10 = 32K + 16 byte 11 = 64K + 16 byte
*/
enum RxConfigBits {
	/* rx fifo threshold */
	RxCfgFIFOShift	= 13,
	RxCfgFIFONone	= (7 << RxCfgFIFOShift),

	/* Max DMA burst */
	RxCfgDMAShift	= 8,
	RxCfgDMAUnlimited = (7 << RxCfgDMAShift),

	/* rx ring buffer length */
	RxCfgRcv8K	= 0,
	RxCfgRcv16K	= (1 << 11),
	RxCfgRcv32K	= (1 << 12),
	RxCfgRcv64K	= (1 << 11) | (1 << 12),

	/* Disable packet wrap at end of Rx buffer. (not possible with 64k) */
	RxNoWrap	= (1 << 7),
};
/* Twister tuning parameters from RealTek.
   Completely undocumented, but required to tune bad links on some boards. */
/*
15        Testfun                1 = Auto-neg speeds up internal timer
14-10       -                    Reserved
 9          LD                   Active low TPI link disable signal. When low, TPI still transmits
                                         link pulses and TPI stays in good link state.
 8        HEART BEAT             1 = HEART BEAT enable, 0 = HEART BEAT disable. HEART
 7            JBEN               BEAT function is only valid in 10Mbps mode.
                                        1 = enable jabber function; 0 = disable jabber function
 6        F_LINK_100              Used to login force good link in 100Mbps for diagnostic purposes.
                                         1 = DISABLE, 0 = ENABLE.
 5         F_Connect              Assertion of this bit forces the disconnect function to be bypassed.
 4           -                    Reserved
 3        Con_status              This bit indicates the status of the connection. 1 = valid connected
                                  link detected; 0 = disconnected link detected.
 2        Con_status_En           Assertion of this bit configures LED1 pin to indicate connection
                                  status.
 1             -                  Reserved
 0          PASS_SCR              Bypass Scramble
 */
enum CSCRBits {
	CSCR_LinkOKBit		= 0x0400,
	CSCR_LinkChangeBit	= 0x0800,
	CSCR_LinkStatusBits	= 0x0f000,
	CSCR_LinkDownOffCmd	= 0x003c0,
	CSCR_LinkDownCmd	= 0x0f3c0,
};
/*
  9346CR: 93C46 Command Register : Bit 7-6 , R/W EEM1-0 ,Operating Mode: These 2 bits select the RTL8139D(L) operating mode, 
  00 : Normal (RTL8139D(L) network/host communication
  01 : Auto-load: Entering this mode
  10 : 93C46 programming
  11 : Config register write enable: Before writing to CONFIG0, 1, 3, 4 registers, and bit13, 12, 8 of BMCR(offset 62h-63h), the RTL8139D(L) must be placed in this mode.
       This will prevent RTL8139D(L)'s configurations from accidental change
*/
enum Cfg9346Bits {
	Cfg9346_Lock	= 0x00,
	Cfg9346_Unlock	= 0xC0,
};

struct rtl8139_private {
	void __iomem		*mmio_addr;  /*memory mapped I/O addr */
	int		        	drv_flags;    
	struct pci_dev		*pci_dev;    /*PCI device */
	u32			        msg_enable; 
	struct napi_struct	napi; /* Structure for NAPI scheduling similar to tasklet but with weighting */ 
	struct net_device	*dev; /*  The NET DEVICE structure.*/

	unsigned char		*rx_ring;
	unsigned int		cur_rx;	  /* RX buf index of next pkt */
	dma_addr_t		    rx_ring_dma;

	unsigned int		tx_flag;  /*tx_flag shall contain transmission flags to notify the device*/
	unsigned long		cur_tx;   /*cur_tx shall hold current transmission descriptor*/
	unsigned long		dirty_tx; /*dirty_tx denotes the first of transmission descriptors which have not completed transmission.*/
	unsigned char		*tx_buf[NUM_TX_DESC];	/* Tx bounce buffers */
	unsigned char		*tx_bufs;	/* Tx bounce buffer region. */
	dma_addr_t		 tx_bufs_dma;

	signed char		 phys[4];	/* MII deive_skb (skb); addresses. */

				/* Twister tune state. */


	spinlock_t		lock;
	spinlock_t		rx_lock;

	u32			rx_config;

	unsigned int		regs_len;
    unsigned long           fifo_copy_timeout;
};

static int rtl8139_open (struct net_device *dev);
static void rtl8139_init_ring (struct net_device *dev);
static netdev_tx_t rtl8139_start_xmit (struct sk_buff *skb,
				       struct net_device *dev);
static int rtl8139_poll(struct napi_struct *napi, int budget);
static irqreturn_t rtl8139_interrupt (int irq, void *dev_instance);
static int rtl8139_close (struct net_device *dev);
static void rtl8139_hw_start (struct net_device *dev);

/* write MMIO register, with flush */
/* Flush avoids rtl8139 bug w/ posted MMIO writes */
#define RTL_W8_F(reg, val8)	do { iowrite8 ((val8), ioaddr + (reg)); ioread8 (ioaddr + (reg)); } while (0)
#define RTL_W16_F(reg, val16)	do { iowrite16 ((val16), ioaddr + (reg)); ioread16 (ioaddr + (reg)); } while (0)
#define RTL_W32_F(reg, val32)	do { iowrite32 ((val32), ioaddr + (reg)); ioread32 (ioaddr + (reg)); } while (0)

/* write MMIO register */
#define RTL_W8(reg, val8)	iowrite8 ((val8), ioaddr + (reg))
#define RTL_W16(reg, val16)	iowrite16 ((val16), ioaddr + (reg))
#define RTL_W32(reg, val32)	iowrite32 ((val32), ioaddr + (reg))

/* read MMIO register */
#define RTL_R8(reg)		ioread8 (ioaddr + (reg))
#define RTL_R16(reg)		ioread16 (ioaddr + (reg))
#define RTL_R32(reg)		ioread32 (ioaddr + (reg))

static const u16 rtl8139_intr_mask =
	PCIErr | PCSTimeout | RxUnderrun | RxOverflow | RxFIFOOver |
	TxErr | TxOK | RxErr | RxOK;

static const u16 rtl8139_norx_intr_mask =
	PCIErr | PCSTimeout | RxUnderrun |
	TxErr | TxOK | RxErr ;

/* RxCfgRcv8K : 8K/16K/32K/64K | NO wrap | RX_FIFO_THRESH(7) << 14 i.e no rx threshold| RX_DMA_BURST(7) << 8 i.e unlimited DMA data bursts */
#if RX_BUF_IDX == 0
static const unsigned int rtl8139_rx_config =
	RxCfgRcv8K | RxNoWrap |
	(RX_FIFO_THRESH << RxCfgFIFOShift) |
	(RX_DMA_BURST << RxCfgDMAShift);
#elif RX_BUF_IDX == 1
static const unsigned int rtl8139_rx_config =
	RxCfgRcv16K | RxNoWrap |
	(RX_FIFO_THRESH << RxCfgFIFOShift) |
	(RX_DMA_BURST << RxCfgDMAShift);
#elif RX_BUF_IDX == 2
static const unsigned int rtl8139_rx_config =
	RxCfgRcv32K | RxNoWrap |
	(RX_FIFO_THRESH << RxCfgFIFOShift) |
	(RX_DMA_BURST << RxCfgDMAShift);
#else
#error "Invalid configuration for 8139_RXBUF_IDX"
#endif
/* rtl8139_tx_config : TxIFG96 () | (TX_DMA_BURST (6)<< TxDMAShift(8)) i.e 1024 | TX_RETRY(8 = 1000) << TxRetryShift (4) : retry =16*/
static const unsigned int rtl8139_tx_config =
	TxIFG96 | (TX_DMA_BURST << TxDMAShift) | (TX_RETRY << TxRetryShift);
/*
 -pci_release_regions: Release reserved PCI I/O and memory resources,Releases all PCI I/O and memory resources previously reserved by a successful call to pci_request_regions. Call this   function only after all use of the PCI regions has ceased.   
*/
/**
 * free_netdev - free network device
 * @dev: device
 *
 * This function does the last stage of destroying an allocated device
 * interface. The reference to the device object is released. If this
 * is the last reference then it will be freed.Must be called in process
 * context.
 */
static void __rtl8139_cleanup_dev (struct net_device *dev)
{
	struct rtl8139_private *tp = netdev_priv(dev);
	struct pci_dev *pdev;
	assert (dev != NULL);
	assert (tp->pci_dev != NULL);
	pdev = tp->pci_dev;
	/*Before release pci region we have to unmap */
	if (tp->mmio_addr)
		pci_iounmap (pdev, tp->mmio_addr);
	/* it's ok to call this even if we have no regions to free */
	pci_release_regions (pdev);

	free_netdev(dev);
}
static void rtl8139_chip_reset (void __iomem *ioaddr)
{
	int i;
	
	/* Soft reset the chip. */
	RTL_W8 (ChipCmd, CmdReset);
	/* Check that the chip has finished the reset. */
	/* the memory barrier is needed to ensure that the reset happen in the expected order.*/
	for (i = 1000; i > 0; i--) {
		barrier();
		if ((RTL_R8 (ChipCmd) & CmdReset) == 0)
			break;
		udelay (10);
	}
}
static struct net_device *rtl8139_init_board(struct pci_dev *pdev)
{
	struct device *d = &pdev->dev;
	void __iomem *ioaddr;
	struct net_device *dev;
	struct rtl8139_private *tp;   /*private data*/
	int rc, disable_dev_on_err = 0;
	unsigned long io_len;
	assert (pdev != NULL);
	
	/* dev and priv zeroed in alloc_etherdev */
	dev = alloc_etherdev (sizeof (*tp));
	if(dev == NULL)
		return ERR_PTR(-ENOMEM);
	/*As a result,entries for these virtual devices are created under /sys/devices/virtual/net*/
	SET_NETDEV_DEV(dev, &pdev->dev);  
	tp = netdev_priv(dev);
	tp->pci_dev = pdev;	
	/* enable device (incl. PCI PM wakeup and hotplug setup) */
	rc = pci_enable_device (pdev);
	if (rc)
		goto err_out;
	disable_dev_on_err = 1;
	rc = pci_request_regions (pdev, DRV_NAME);
	if (rc)
		goto err_out;
	pci_set_master (pdev);
	io_len = pci_resource_len(pdev, 1);
	if (!(pci_resource_flags(pdev, 1) & IORESOURCE_MEM)) {
		dev_err(d, "region #IORESOURCE_MEM not a MMIO resource, aborting\n");
		rc = -ENODEV;
		goto err_out;
	}
	if (io_len < RTL_MIN_IO_SIZE) {
		dev_err(d, "Invalid PCI IORESOURCE_MEM region size(s), aborting\n");
		rc = -ENODEV;
		goto err_out;
	}
	/* Create a virtual mapping cookie for a PCI BAR (memory or IO) */
	ioaddr = pci_iomap(pdev, 1, 0);
	if (!ioaddr) {
		dev_err(d, "cannot map #IORESOURCE_MEM\n");
		rc = -ENODEV;
		goto err_out;
	}
	tp->regs_len = io_len;
	tp->mmio_addr = ioaddr;
	rtl8139_chip_reset (ioaddr);
	return dev;	
err_out:
	__rtl8139_cleanup_dev (dev);
	if (disable_dev_on_err)
		pci_disable_device (pdev);
	return ERR_PTR(rc);
}

static const struct net_device_ops rtl8139_netdev_ops = {
	.ndo_open		    = rtl8139_open,
	.ndo_stop		    = rtl8139_close,
	.ndo_start_xmit		= rtl8139_start_xmit,
};
/**
 *	netif_napi_add - initialize a NAPI context
 *	@dev:  network device
 *	@napi: NAPI context
 *	@poll: polling function
 *	@weight: default weight
 *
 * netif_napi_add() must be used to initialize a NAPI context prior to calling
 * *any* of the other NAPI-related functions.
 */
/* New device inserted : probe function -*/
/**
 *  netif_napi_del - remove a napi context
 *  @napi: napi context
 *
 *  netif_napi_del() removes a napi context from the network device napi list
 */
static int  rtl8139_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *dev = NULL;
	struct rtl8139_private *tp;                /*our private data structures,*/
	int i; 
	void __iomem *ioaddr;                      /*IO MEM Addresse*/
	assert (pdev != NULL);
	assert (ent != NULL );
	
	/* when we're built into the kernel, the driver version message
	 * is only printed if at least one 8139 board has been found
	 */
	if(pdev->vendor == PCI_VENDOR_ID_REALTEK && 
		pdev->device == PCI_DEVICE_ID_REALTEK_8139 && pdev->revision >= 0x20){
		dev_info(&pdev->dev,
			   "this (id %04x:%04x rev %02x) is an enhanced 8139c+ chip, use 8139cp\n",
		       	   pdev->vendor, pdev->device, pdev->revision);
		return -ENODEV;
	}
	dev = rtl8139_init_board (pdev);
	if (IS_ERR(dev))
		return PTR_ERR(dev);
	assert (dev != NULL);
	tp = netdev_priv(dev);
	tp->dev = dev;
	ioaddr = tp->mmio_addr;
	assert (ioaddr != NULL);
	/* The Rtl8139-specific entries in the device structure. */
	dev->netdev_ops = &rtl8139_netdev_ops;
	netif_napi_add(dev, &tp->napi, rtl8139_poll, 64); /* Initialize a NAPI context */	
	tp = netdev_priv(dev);
	tp->mmio_addr = ioaddr;
	spin_lock_init (&tp->lock);
	spin_lock_init (&tp->rx_lock);
	/* dev is fully set up and ready to use now */
	pr_debug("about to register device named %s (%p)...\n",
		 dev->name, dev);
	i = register_netdev (dev);
	if (i) 
		goto err_out;
	/* Similar to the helpers above, these manipulate per-pci_dev
 	* driver-specific data.  They are really just a wrapper around
 	* the generic device structure functions of these calls.
 	*/
	pci_set_drvdata (pdev, dev);
	netdev_info(dev, "%s at 0x%p, %pM, IRQ %d\n",
		    board_info[ent->driver_data].name,
		    ioaddr, dev->dev_addr, pdev->irq);
	return 0;
err_out:
	netif_napi_del(&tp->napi); /* remove a napi context */
	__rtl8139_cleanup_dev (dev);
	pci_disable_device (pdev);
	return i;
}
static void rtl8139_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata (pdev);
	struct rtl8139_private *tp = netdev_priv(dev);
	assert (dev != NULL);
	
	netif_napi_del(&tp->napi);
	
	unregister_netdev (dev);

	__rtl8139_cleanup_dev (dev);
	pci_disable_device (pdev);
}
static int rtl8139_open (struct net_device *dev)
{
	struct rtl8139_private *tp = netdev_priv(dev);
	const int irq = tp->pci_dev->irq;
	int retval;
	retval = request_irq(irq, rtl8139_interrupt, IRQF_SHARED, dev->name, dev);
	if (retval)
		return retval;
    pr_info("%s##\n",__func__);
	/* dma allocation for rx and tx buffer*/
	tp->tx_bufs = dma_alloc_coherent(&tp->pci_dev->dev, TX_BUF_TOT_LEN,
					   &tp->tx_bufs_dma, GFP_KERNEL);
	tp->rx_ring = dma_alloc_coherent(&tp->pci_dev->dev, RX_BUF_TOT_LEN,
					   &tp->rx_ring_dma, GFP_KERNEL);
	if (tp->tx_bufs == NULL || tp->rx_ring == NULL) {
		free_irq(irq, dev);	
	
		if (tp->tx_bufs)
			dma_free_coherent(&tp->pci_dev->dev, TX_BUF_TOT_LEN,
					    tp->tx_bufs, tp->tx_bufs_dma);
		if (tp->rx_ring)
			dma_free_coherent(&tp->pci_dev->dev, RX_BUF_TOT_LEN,
					    tp->rx_ring, tp->rx_ring_dma);
		return -ENOMEM;
	}
	napi_enable(&tp->napi);  /*enable NAPI scheduling*/
	tp->tx_flag = (TX_FIFO_THRESH << 11) & 0x003f0000;   /*80000 & 0x003f0000 i.e ERTXTH0 to 5 six bit : i.e 3f */
	rtl8139_init_ring (dev);
	rtl8139_hw_start (dev);
	netif_start_queue (dev);
	return 0;
	
}
static void rtl8139_hw_start (struct net_device *dev)
{
	struct rtl8139_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	u32 i;
	u8 tmp;
	/* Bring old chips out of low-power mode. */
	rtl8139_chip_reset (ioaddr);	
	/* unlock Config[01234] and BMCR register writes */
	RTL_W8_F (Cfg9346, Cfg9346_Unlock);
	/* Restore our idea of the MAC address. */
	RTL_W32_F (MAC0 + 0, le32_to_cpu (*(__le32 *) (dev->dev_addr + 0)));
	RTL_W32_F (MAC0 + 4, le16_to_cpu (*(__le16 *) (dev->dev_addr + 4)));

	tp->cur_rx = 0;
	/* init Rx ring buffer DMA address */
	RTL_W32_F (RxBuf, tp->rx_ring_dma);

	/* Must enable Tx/Rx before setting transfer thresholds! */
	RTL_W8 (ChipCmd, CmdRxEnb | CmdTxEnb);
	
	tp->rx_config = rtl8139_rx_config | AcceptBroadcast | AcceptMyPhys;
	RTL_W32 (RxConfig, tp->rx_config);
	RTL_W32 (TxConfig, rtl8139_tx_config);
	
//	rtl_check_media (dev, 1); //TODO
	RTL_W8 (Config3, RTL_R8 (Config3) & ~Cfg3_Magic);
	netdev_dbg(dev, "init buffer addresses\n");
	/* Lock Config[01234] and BMCR register writes */
	RTL_W8 (Cfg9346, Cfg9346_Lock);

	/* init Tx buffer DMA addresses */
	for (i = 0; i < NUM_TX_DESC; i++)
		RTL_W32_F (TxAddr0 + (i * 4), tp->tx_bufs_dma + (tp->tx_buf[i] - tp->tx_bufs));
	RTL_W32 (RxMissed, 0);

	/* no early-rx interrupts */
	RTL_W16 (MultiIntr, RTL_R16 (MultiIntr) & MultiIntrClear);
	/* make sure RxTx has started */
	tmp = RTL_R8 (ChipCmd);
	if ((!(tmp & CmdRxEnb)) || (!(tmp & CmdTxEnb)))
		RTL_W8 (ChipCmd, CmdRxEnb | CmdTxEnb);

	/* Enable all known interrupts by setting the interrupt mask. */
	RTL_W16 (IntrMask, rtl8139_intr_mask);
}
/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void rtl8139_init_ring (struct net_device *dev)
{
	struct rtl8139_private *tp = netdev_priv(dev);
	int i;

	tp->cur_rx = 0;
	tp->cur_tx = 0;
	tp->dirty_tx = 0;

	for (i = 0; i < NUM_TX_DESC; i++)
		tp->tx_buf[i] = &tp->tx_bufs[i * TX_BUF_SIZE];
}
/*TODO : All of this is magic and undocumented.*/

static inline void rtl8139_tx_clear (struct rtl8139_private *tp)
{
	tp->cur_tx = 0;
	tp->dirty_tx = 0;
}

static netdev_tx_t rtl8139_start_xmit (struct sk_buff *skb,
					     struct net_device *dev)
{
	struct rtl8139_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	unsigned int entry;
	unsigned int len = skb->len;
	unsigned long flags;
	
    pr_info("%s##\n",__func__);
	/* Calculate the next Tx descriptor entry. */
	
	entry = tp->cur_tx % NUM_TX_DESC;
	
	/* Note: the chip doesn't have auto-pad! */
	if (likely(len < TX_BUF_SIZE)) {
		if (len < ETH_ZLEN)
			memset(tp->tx_buf[entry], 0, ETH_ZLEN);
		skb_copy_and_csum_dev(skb, tp->tx_buf[entry]);
		/*dev_kfree_skb_irq(skb) : when caller drops a packet from irq context, replacing kfree_skb(skb) */
		dev_kfree_skb_any(skb);
	}else {
		dev_kfree_skb_any(skb);
		dev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}
	spin_lock_irqsave(&tp->lock, flags);
	/*
	 * Writing to TxStatus triggers a DMA transfer of the data
	 * copied to tp->tx_buf[entry] above. Use a memory barrier
	 * to make sure that the device sees the updated data.
	 */
	wmb();
	RTL_W32_F (TxStatus0 + (entry * sizeof (u32)),
		   tp->tx_flag | max(len, (unsigned int)ETH_ZLEN));

	tp->cur_tx++;
	if ((tp->cur_tx - NUM_TX_DESC) == tp->dirty_tx)
		netif_stop_queue (dev);
	spin_unlock_irqrestore(&tp->lock, flags);
	
	netif_dbg(tp, tx_queued, dev, "Queued Tx packet size %u to slot %d\n",
		  len, entry);

	return NETDEV_TX_OK; /* driver took care of packet */	
}
static void rtl8139_tx_interrupt (struct net_device *dev,
				  struct rtl8139_private *tp,
				  void __iomem *ioaddr)
{
	unsigned long dirty_tx, tx_left;
	assert (dev != NULL);
	assert (ioaddr != NULL);

	dirty_tx = tp->dirty_tx;
	tx_left = tp->cur_tx - dirty_tx;
	while (tx_left > 0) {
		int entry = dirty_tx % NUM_TX_DESC;
		int txstatus;
		txstatus = RTL_R32 (TxStatus0 + (entry * sizeof (u32)));  /* TxStatus0 + entry(0-3) * discriptor size*/
		if (!(txstatus & (TxStatOK | TxUnderrun | TxAborted)))
			    break;	/* It still hasn't been Txed */
		/* Note: TxCarrierLost is always asserted at 100mbps. */
		if (txstatus & (TxOutOfWindow | TxAborted)) {
			/* There was an major error, log it. */
			netif_dbg(tp, tx_err, dev, "Transmit error, Tx status %08x\n",
				  txstatus);
			dev->stats.tx_errors++;
			if (txstatus & TxAborted) {
				dev->stats.tx_aborted_errors++;
				RTL_W32 (TxConfig, TxClearAbt);
				RTL_W16 (IntrStatus, TxErr);
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
}

static int rtl8139_rx(struct net_device *dev, struct rtl8139_private *tp,
		      int budget)
{
	void __iomem *ioaddr = tp->mmio_addr;
	int received = 0;
	unsigned char *rx_ring = tp->rx_ring;
	unsigned int cur_rx = tp->cur_rx;
	unsigned int rx_size = 0;
	netdev_dbg(dev, "In %s(), current %04x BufAddr %04x, free to %04x, Cmd %02x\n",
		   __func__, (u16)cur_rx,
	RTL_R16(RxBufAddr), RTL_R16(RxBufPtr), RTL_R8(ChipCmd));
	while (netif_running(dev) && received < budget &&
	       (RTL_R8 (ChipCmd) & RxBufEmpty) == 0) {
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
		netif_dbg(tp, rx_status, dev, "%s() status %04x, size %04x, cur %04x\n",
			  __func__, rx_status, rx_size, cur_rx);	
        if (unlikely(rx_size == 0xfff0)) {
			if (!tp->fifo_copy_timeout)
				    tp->fifo_copy_timeout = jiffies + 2;
 		/* time_after(a,b) returns true if the time a is after time b.*/
			else if (time_after(jiffies, tp->fifo_copy_timeout)) {
				netdev_dbg(dev, "hung FIFO. Reset\n");
				rx_size = 0;
				goto no_early_rx;
			}
		    netif_dbg(tp, intr, dev, "fifo copy in progress\n");
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
		RTL_W16 (RxBufPtr, (u16) (cur_rx - 16));               /* 16 byte align the IP fields  */
		
	}
	netdev_dbg(dev, "Done %s(), current %04x BufAddr %04x, free to %04x, Cmd %02x\n",
		   __func__, cur_rx,
		   RTL_R16(RxBufAddr), RTL_R16(RxBufPtr), RTL_R8(ChipCmd));
	
	tp->cur_rx = cur_rx;
	if (tp->fifo_copy_timeout)
		received = budget;
	return received;	
}
static int rtl8139_poll(struct napi_struct *napi, int budget)
{
	struct rtl8139_private *tp = container_of(napi, struct rtl8139_private, napi);
	struct net_device *dev = tp->dev;
	void __iomem *ioaddr = tp->mmio_addr;
	int work_done;
    pr_info("%s##\n",__func__);
	spin_lock(&tp->rx_lock);
	work_done = 0;
	if (likely(RTL_R16(IntrStatus) & RxAckBits))  /*if RxIFOOver | RxOverflow | RxOK is set then work_done ++ */
		work_done += rtl8139_rx(dev, tp, budget);
	
	if (work_done < budget) {
		unsigned long flags;
		/*
		 * Order is important since data can get interrupted
		 * again when we think we are done.
		 */
		spin_lock_irqsave(&tp->lock, flags);
		__napi_complete(napi);                   //NAPI processing complete
		RTL_W16_F(IntrMask, rtl8139_intr_mask);
		spin_unlock_irqrestore(&tp->lock, flags);
	}
	spin_unlock(&tp->rx_lock);

	return work_done;
}
static irqreturn_t rtl8139_interrupt (int irq, void *dev_instance)
{
	struct net_device *dev = (struct net_device *) dev_instance;
	struct rtl8139_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	u16 status, ackstat;
	int link_changed = 0; /* avoid bogus "uninit" warning */
	int handled = 0;
	spin_lock (&tp->lock);
	status = RTL_R16 (IntrStatus);
	/* shared irq? */
	if (unlikely((status & rtl8139_intr_mask) == 0))
		goto out;
	handled = 1;	
	/* h/w no longer present (hotplug?) or major error, bail */
	if (unlikely(status == 0xFFFF))
		goto out;
	/* close possible race's with dev_close netif_running - test if up*/
	if (unlikely(!netif_running(dev))) {
		RTL_W16 (IntrMask, 0);
		goto out;
	}
	/* Acknowledge all of the current interrupt sources ASAP, but
	   an first get an additional status bit from CSCR. */
	if (unlikely(status & RxUnderrun))
		link_changed = RTL_R16 (CSCR) & CSCR_LinkChangeBit;
	ackstat = status & ~ (RxAckBits | TxErr);
	if(ackstat)
		RTL_W16(IntrStatus, ackstat);
	/* Receive packets are processed by poll routine.If not running start it now. */
	if (status & RxAckBits){
		if (napi_schedule_prep(&tp->napi)) {
			RTL_W16_F (IntrMask, rtl8139_norx_intr_mask);
			__napi_schedule(&tp->napi);
		}
	}
	/* Check uncommon events with one test. */
//	if (unlikely(status & (PCIErr | PCSTimeout | RxUnderrun | RxErr)))
//		rtl8139_weird_interrupt (dev, tp, ioaddr,
//					 status, link_changed);
	if (status & (TxOK | TxErr)) {
		rtl8139_tx_interrupt (dev, tp, ioaddr);
		if (status & TxErr)
			RTL_W16 (IntrStatus, TxErr);
	}
out:
	spin_unlock (&tp->lock);
	netdev_dbg(dev, "exiting interrupt, intr_status=%#4.4x\n",
		   RTL_R16(IntrStatus));
	/*The possible return values from an interrupt handler, indicating whether an actual interrupt from the device was present.*/
	return IRQ_RETVAL(handled);    	
}


static int rtl8139_close (struct net_device *dev)
{
	struct rtl8139_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	unsigned long flags;
	netif_stop_queue(dev);  /*Stop transmitted packets*/
	napi_disable(&tp->napi);
	netif_dbg(tp, ifdown, dev, "Shutting down ethercard, status was 0x%04x\n",
		  RTL_R16(IntrStatus));
	spin_lock_irqsave (&tp->lock, flags);
	
	/* Stop the chip's Tx and Rx DMA processes. */
	RTL_W8 (ChipCmd, 0);
	
	/* Disable interrupts by clearing the interrupt mask. */
	RTL_W16 (IntrMask, 0);
	
	/* Update the error counts. */
	dev->stats.rx_missed_errors += RTL_R32 (RxMissed);
	RTL_W32 (RxMissed, 0);
	
	spin_unlock_irqrestore (&tp->lock, flags);
	
	/*Free IRQ*/
	free_irq(tp->pci_dev->irq, dev);
	
	rtl8139_tx_clear (tp);
	dma_free_coherent(&tp->pci_dev->dev, RX_BUF_TOT_LEN,
			  tp->rx_ring, tp->rx_ring_dma);
	dma_free_coherent(&tp->pci_dev->dev, TX_BUF_TOT_LEN,
			  tp->tx_bufs, tp->tx_bufs_dma);
	tp->rx_ring = NULL;
	tp->tx_bufs = NULL;
	
	/* Green! Put the chip in low-power mode. */
	RTL_W8 (Cfg9346, Cfg9346_Unlock);
	
	return 0;
}
static struct pci_driver rtl8139_pci_driver = {
	.name           = DRV_NAME,
	.id_table	= rtl8139_pci_tbl,
	.probe		= rtl8139_init_one,
	.remove		= rtl8139_remove_one,
};

/*register a pci driver ::- on Success return 0 otherwise errorno*/
static int __init rtl8139_init_module (void)
{
#ifdef MODULE
	pr_info(RTL8139_DRIVER_NAME "\n");
#endif
	return pci_register_driver (&rtl8139_pci_driver);
}

static void __exit rtl8139_cleanup_module (void)
{
	pci_unregister_driver (&rtl8139_pci_driver);
}

module_init(rtl8139_init_module);
module_exit(rtl8139_cleanup_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR ("Chandan Jha <beingchandanjha@gmail.com>");
MODULE_DESCRIPTION ("RealTek RTL-8139 Fast Ethernet driver: Rewritten  for better understanding of code");
MODULE_VERSION(DRV_VERSION);


