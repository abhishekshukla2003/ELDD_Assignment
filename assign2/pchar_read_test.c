#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char *argv[])
{
    int fd, ret;
    char buf[64];
    // validate cmd line args
    if (argc != 2)
    {
        printf("insufficient cmd line args.\nsyntax: %s </dev/pchar*> \"<data to write>\"\n", argv[0]);
        _exit(1);
    }

    // open device file for rd
    fd = open(argv[1], O_RDONLY);
    if (fd < 0)
    {
        perror("failed to open device");
        _exit(1);
    }
    printf("device file opened.\n");

    // read from device
    memset(buf, 0, sizeof(buf));
    ret = read(fd, buf, sizeof(buf)); // returns cnt
    printf("Rd - bytes read from device: %d -- %s\n", ret, buf);

    // close device file
    close(fd);
    printf("device file closed.\n");
    return 0;
}

// cmd> gcc pchar_read_test.c -o pchar_rd_test.out
// cmd> sudo insmod pchar.ko     # if driver is not already loaded
// cmd> sudo ./pchar_rd_test.out /dev/pchar2
// cmd> sudo dmesg | tail 20
