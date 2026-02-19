// twinkle.c - play "Twinkle Twinkle Little Star" via sysfs freq
// Build: gcc -O2 -Wall -o twinkle twinkle.c
// Run:   sudo ./twinkle
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char *FREQ_PATH = "/sys/devices/platform/my-beeper/freq";

static int write_freq(int hz)
{
    char buf[32];
    int fd = open(FREQ_PATH, O_WRONLY);
    if (fd < 0) {
        fprintf(stderr, "open(%s) failed: %s\n", FREQ_PATH, strerror(errno));
        return -1;
    }

    int len = snprintf(buf, sizeof(buf), "%d\n", hz);
    if (write(fd, buf, len) != len) {
        fprintf(stderr, "write freq %d failed: %s\n", hz, strerror(errno));
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

static void msleep(int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

struct note {
    int hz;      // 0 means rest
    int ms;      // duration
};

/*
 * Twinkle Twinkle Little Star (C major)
 * C C G G A A G | F F E E D D C |
 * G G F F E E D | G G F F E E D |
 * C C G G A A G | F F E E D D C
 */
static const struct note song[] = {
    {262, 400}, {262, 400}, {392, 400}, {392, 400}, {440, 400}, {440, 400}, {392, 700},
    {349, 400}, {349, 400}, {330, 400}, {330, 400}, {294, 400}, {294, 400}, {262, 700},

    {392, 400}, {392, 400}, {349, 400}, {349, 400}, {330, 400}, {330, 400}, {294, 700},
    {392, 400}, {392, 400}, {349, 400}, {349, 400}, {330, 400}, {330, 400}, {294, 700},

    {262, 400}, {262, 400}, {392, 400}, {392, 400}, {440, 400}, {440, 400}, {392, 700},
    {349, 400}, {349, 400}, {330, 400}, {330, 400}, {294, 400}, {294, 400}, {262, 900},
};

int main(void)
{
    // quick sanity: try turn off first
    if (write_freq(0) < 0) return 1;

    // play
    for (size_t i = 0; i < sizeof(song)/sizeof(song[0]); i++) {
        if (write_freq(song[i].hz) < 0) return 1;
        msleep(song[i].ms);
        // small gap between notes
        write_freq(0);
        msleep(40);
    }

    write_freq(0);
    return 0;
}

