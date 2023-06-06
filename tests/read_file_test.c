#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

#include "read_file.h"

int main(int argc, char *argv[])
{
    int fd = open(argv[1], O_RDONLY), size, offset = 0, i;
    char *buf = malloc(0x10000);
    size = read_file(fd, buf);
    while(offset < size)
    {
        i = write(1, &buf[offset], size - offset > 4096 ? 4096 : size - offset);
        offset += i;
    }
    return 0;
    (void)argc;
    (void)argv;
}
