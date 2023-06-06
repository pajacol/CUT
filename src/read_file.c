#include <unistd.h>
#include "read_file.h"

int read_file(int fd, char buf[])
{
    int total = 0, size;
    lseek(fd, 0, SEEK_SET);
    while((size = read(fd, &buf[total], 4096)) > 0)
    {
        total += size;
    }
    buf[total] = 0;
    return total;
}
