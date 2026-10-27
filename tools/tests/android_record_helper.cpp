#include "record_capture.h"
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const int fd = open(argv[1], O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0644);
    if (fd < 0) { std::perror("open recording"); return 2; }
    const int status = record_capture::Run(fd, 4000000);
    close(fd);
    return status;
}
