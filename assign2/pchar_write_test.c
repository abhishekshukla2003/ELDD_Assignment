#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

void sigint_handler(int sig)
{
    printf("SIGINT caught.\n");
}

int main(int argc, char *argv[])
{
    int fd, ret;
    signal(SIGINT, sigint_handler);

    // validate cmd line args
    if (argc != 3)
    {
        printf("insufficient cmd line args.\nsyntax: %s </dev/pchar*> \"<data to write>\"\n", argv[0]);
        _exit(1);
    }

    // open device file for wr
    fd = open(argv[1], O_WRONLY);
    if (fd < 0)
    {
        perror("failed to open device");
        _exit(1);
    }
    printf("device file opened.\n");

    // write into device
    ret = write(fd, argv[2], strlen(argv[2])); // returns cnt
    printf("Wr - bytes written in device: %d\n", ret);

    // close device file
    close(fd);
    printf("device file closed.\n");
    return 0;
}

// cmd> gcc pchar_write_test.c -o pchar_wr_test.out
// cmd> sudo insmod pchar.ko     # if driver is not already loaded
// cmd> sudo ./pchar_wr_test.out /dev/pchar2 "ABC...XYZ"
// cmd> sudo dmesg | tail 20
