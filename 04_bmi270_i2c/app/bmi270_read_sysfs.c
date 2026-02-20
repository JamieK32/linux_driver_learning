#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef IIO_DEV
#define IIO_DEV "iio:device0"
#endif

static const char *BASE = "/sys/bus/iio/devices/" IIO_DEV;

static volatile sig_atomic_t g_timed_out = 0;

static void on_alarm(int sig) {
    (void)sig;
    g_timed_out = 1;
}

static int read_line_timeout(const char *path, char *buf, size_t bufsz, int timeout_sec) {
    g_timed_out = 0;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_alarm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGALRM, &sa, NULL) < 0) return -errno;

    alarm((unsigned)timeout_sec);

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        alarm(0);
        return -errno;
    }

    ssize_t n = read(fd, buf, bufsz - 1);
    int saved_errno = errno;
    close(fd);
    alarm(0);

    if (g_timed_out) return -ETIMEDOUT;
    if (n < 0) return -saved_errno;

    buf[n] = '\0';
    // trim newline
    char *nl = strpbrk(buf, "\r\n");
    if (nl) *nl = '\0';
    return 0;
}

static int read_long_timeout(const char *name, long *out, int timeout_sec) {
    char path[256], line[128];
    snprintf(path, sizeof(path), "%s/%s", BASE, name);

    int rc = read_line_timeout(path, line, sizeof(line), timeout_sec);
    if (rc) return rc;

    char *end = NULL;
    errno = 0;
    long v = strtol(line, &end, 10);
    if (errno) return -errno;
    *out = v;
    return 0;
}

static int read_double_timeout(const char *name, double *out, int timeout_sec) {
    char path[256], line[128];
    snprintf(path, sizeof(path), "%s/%s", BASE, name);

    int rc = read_line_timeout(path, line, sizeof(line), timeout_sec);
    if (rc) return rc;

    char *end = NULL;
    errno = 0;
    double v = strtod(line, &end);
    if (errno) return -errno;
    *out = v;
    return 0;
}

static void msleep(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static void die_rc(const char *what, int rc) {
    fprintf(stderr, "%s: %s (rc=%d)\n", what, strerror(-rc), rc);
    exit(1);
}

int main(int argc, char **argv) {
    int interval_ms = 100;     // 打印间隔
    int io_timeout = 2;        // 单次 sysfs 读超时(秒)

    if (argc >= 2) interval_ms = atoi(argv[1]);
    if (argc >= 3) io_timeout  = atoi(argv[2]);
    if (interval_ms <= 0) interval_ms = 100;
    if (io_timeout <= 0) io_timeout = 2;

    // 读 scale（这些一般不会卡）
    double accel_scale = 0.0, gyro_scale = 0.0;
    int rc = read_double_timeout("in_accel_scale", &accel_scale, io_timeout);
    if (rc) die_rc("read in_accel_scale", rc);

    rc = read_double_timeout("in_anglvel_scale", &gyro_scale, io_timeout);
    if (rc) die_rc("read in_anglvel_scale", rc);

    printf("BASE=%s\n", BASE);
    printf("accel_scale=%g, gyro_scale=%g\n", accel_scale, gyro_scale);
    printf("interval=%dms, io_timeout=%ds\n", interval_ms, io_timeout);
    printf("Press Ctrl+C to stop.\n\n");

    while (1) {
        long ax, ay, az, gx, gy, gz, tr;
        double ts = 0.0;
        long toff = 0;
        int has_temp = 0;

        rc = read_long_timeout("in_accel_x_raw", &ax, io_timeout);
        if (rc == -EBUSY) { fprintf(stderr, "accel_x_raw: EBUSY (device busy)\n"); return 2; }
        if (rc) die_rc("read in_accel_x_raw", rc);

        rc = read_long_timeout("in_accel_y_raw", &ay, io_timeout);
        if (rc) die_rc("read in_accel_y_raw", rc);

        rc = read_long_timeout("in_accel_z_raw", &az, io_timeout);
        if (rc) die_rc("read in_accel_z_raw", rc);

        rc = read_long_timeout("in_anglvel_x_raw", &gx, io_timeout);
        if (rc) die_rc("read in_anglvel_x_raw", rc);

        rc = read_long_timeout("in_anglvel_y_raw", &gy, io_timeout);
        if (rc) die_rc("read in_anglvel_y_raw", rc);

        rc = read_long_timeout("in_anglvel_z_raw", &gz, io_timeout);
        if (rc) die_rc("read in_anglvel_z_raw", rc);

        // 温度（可选）
        if (access("/sys/bus/iio/devices/" IIO_DEV "/in_temp_raw", R_OK) == 0) {
            if (read_long_timeout("in_temp_raw", &tr, io_timeout) == 0 &&
                read_double_timeout("in_temp_scale", &ts, io_timeout) == 0) {
                has_temp = 1;
                // offset 可能存在也可能不存在
                if (read_long_timeout("in_temp_offset", &toff, io_timeout) != 0) toff = 0;
            }
        }

        printf("ACC raw[%6ld %6ld %6ld]  SI[m/s^2]=[% .4f % .4f % .4f]\n",
               ax, ay, az,
               ax * accel_scale, ay * accel_scale, az * accel_scale);

        printf("GYR raw[%6ld %6ld %6ld]  SI[rad/s]=[% .4f % .4f % .4f]\n",
               gx, gy, gz,
               gx * gyro_scale, gy * gyro_scale, gz * gyro_scale);

        if (has_temp) {
		double temp_c = (tr + toff) * ts / 1000.0;
		printf("TMP raw=%ld off=%ld scale=%g  degC=% .2f\n", tr, toff, ts, temp_c);


        }

        printf("----\n");
        msleep(interval_ms);
    }

    return 0;
}

