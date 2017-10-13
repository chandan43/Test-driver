#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/io.h> 

#include "rtc.h"

int main(int argc,char **argv){
	int ret,a,b;
	/*  sets the port access permission bits for the calling thread for num bits starting from port address from.  If turn_on is nonzero, then permission for the specified bits
	 is enabled; otherwise it is disabled.*/
	ret=ioperm(ADDRESS_REG,0x04,1);
	printf("Ret=%d\n",ret);
	outb(YEAR,ADDRESS_REG);
	printf("year=%x\n",inb(DATA_REG));
	outb(MONTH,ADDRESS_REG);
	printf("Month=%x\n",inb(DATA_REG));
	outb(DAY,ADDRESS_REG);
	printf("Day=%x\n",inb(DATA_REG));
	return 0;
}
