#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include "mychar.h"

#define BUFFERSIZE 256
#define DEVICE_NAME "/dev/mychar"
#define err_handler(en,msg) do{errno=en; perror(msg);exit(EXIT_SUCCESS);}while(0)
static char message[BUFFERSIZE];

int main(){
	int ret,fd;
	char StringtoSend[BUFFERSIZE];
	fd=open(DEVICE_NAME,O_RDWR);
	if(fd<0)
		err_handler(fd,"open");
	printf("Enter short string to send kernel module: \n");
	scanf("%[^\n]%*c",StringtoSend);
	printf("Writing msg to Device [%s]:\n",StringtoSend);
	ret=ioctl(fd,IOCTL_WRITE_MSG,StringtoSend);
	if(ret<0)
	   err_handler(ret,"ioctl");
	printf("Press Enter to read from device..\n");
	getchar();
	printf("Reading from Device...\n");
	ret=ioctl(fd,IOCTL_READ_MSG,StringtoSend);
	if(ret<0)
	   err_handler(ret,"read");
	printf("The received message is: [%s]\n",message);
	printf("End of the program\n");
	return 0;
}
