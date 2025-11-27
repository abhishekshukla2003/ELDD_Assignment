
#ifndef __PCHAR_IOCTL_H
#define __PCHAR_IOCTL_H

#include<linux/ioctl.h>

struct fifo_info
{
    short size;
    short length;
    short avail;
};

#define FIFO_CLEAR _IO('x',1)
#define FIFO_GET_INFO _IOR('x',2,struct fifo_info)
#define FIFO_RESIZE  _IOW('x',3,int)

#endif
