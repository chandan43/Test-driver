#define MAJOR_NUM 105
#define IOCTL_WRITE_MSG _IOR(MAJOR_NUM, 0, char *)
#define IOCTL_READ_MSG _IOR(MAJOR_NUM, 1, char *)
