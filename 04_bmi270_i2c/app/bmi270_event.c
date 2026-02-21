#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/iio/events.h>

int main(void)
{
    int dev_fd = open("/dev/iio:device0", O_RDONLY);
    if (dev_fd < 0) {
        perror("open");
        return 1;
    }

    int event_fd;
    if (ioctl(dev_fd, IIO_GET_EVENT_FD_IOCTL, &event_fd) < 0) {
        perror("ioctl IIO_GET_EVENT_FD_IOCTL");
        return 1;
    }

    struct pollfd pfd = {
        .fd = event_fd,
        .events = POLLIN,
    };

    while (1) {
        int ret = poll(&pfd, 1, -1);
        if (ret > 0 && (pfd.revents & POLLIN)) {
            struct iio_event_data event;
            ssize_t len = read(event_fd, &event, sizeof(event));

            if (len == sizeof(event)) {
                printf("event id=0x%llx ts=%lld\n",
                       (unsigned long long)event.id,
                       (long long)event.timestamp);
            }
        }
    }

    return 0;
}