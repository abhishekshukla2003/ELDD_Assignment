

#include<stdio.h>
#include<string.h>
#include<unistd.h>
#include<fcntl.h>
#include<sys/ioctl.h>
#include"pchar_ioctl.h"

int main()
{
    int fd,ret;
    char buf1[32]="";
    char buf2[32]="";

    struct fifo_info info;

    fd=open("/dev/pchar0",O_RDWR);
    if(fd<0){
        perror("faild to open");
        _exit(1);
    }
 printf("devicefile open\n");
 strcpy(buf1,"abcdefghijklmnopqrstuvwxyz");

 ret=write(fd,buf1,strlen(buf1));
 printf("wr-1 bytes written to device :%d\n",ret);
 
ret=ioctl(fd,FIFO_GET_INFO,&info);
if(ret<0){
    perror("failed ioctl");
    close(fd);
    _exit(1);
}
printf("FIFO state: size=%d,lenth=%d, avail =%d\n",info.size,info.length,info.avail);

//write(4 bytes) in device file
strcpy(buf1,"DESD");
ret=write(fd,buf1,strlen(buf1));
printf("WR 2 bytes writen in device:%d\n", ret);


ret=ioctl(fd,FIFO_GET_INFO,&info);
if(ret<0){
    perror("ioctl failed");
    close(fd);
    _exit(1);
}
printf("FIFO stat:size=%d,length=%d, avail=%d\n",info.size,info.length,info.avail);

//clear device
ret=ioctl(fd,FIFO_CLEAR);
if(ret<0){
    perror("ioctl() failed");
    close(fd);
    _exit(1);
}
printf("fifo is cleared\n");

//get info
ret=ioctl(fd,FIFO_GET_INFO,&info);
 if (ret < 0)
    {
        perror("ioctl() failed");
        close(fd);
        _exit(1);
    }
    printf("FIFO state: size=%d, length=%d, avail=%d\n",
           info.size, info.length, info.avail);


           //resize
           ret=ioctl(fd, FIFO_RESIZE,64);
           if(ret<0){
            printf("resize failed\n");
            close(fd);
            _exit(1);
           }
           printf("FIFO resized successfully.\n");

ret = ioctl(fd, FIFO_GET_INFO, &info);
printf("After resize: size=%d, length=%d, avail=%d\n",
       info.size, info.length, info.avail);
    // close device file
    close(fd);
    printf("device file closed.\n");
    return 0;

    }
