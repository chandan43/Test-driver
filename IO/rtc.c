#include <linux/module.h>
#include <linux/init.h>
#include <asm/io.h>

#include "rtc.h"

static unsigned char get_ret(unsigned char addrs){
	outb(addrs,ADDRESS_REG);
	return inb(DATA_REG);
}
static void set_rtc(unsigned char data,unsigned char addrs){
	outb(addrs,ADDRESS_REG);
	outb(data,DATA_REG);
}

int init_module(void){
//	pr_info("Year: %x\n",get_ret(YEAR));
//	pr_info("Month: %x\n",get_ret(MONTH));
//	pr_info("Day: %x\n",get_ret(DAY));
	set_rtc(0x16,0x19);
	set_rtc(0x11,0x11);
	set_rtc(0x12,0x4);
	return 0;
}

void cleanup_module(void){
	pr_info("Exit\n");
}
MODULE_LICENSE("GPL");
MODULE_AUTHOR("xyz");
MODULE_DESCRIPTION("My first driver");
