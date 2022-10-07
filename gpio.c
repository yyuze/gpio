/* 
 * FOR STUDY USING, NOT RECONMANDED USING IN OTHER SCENE
 * Implemented based on https://docs.kernel.org/admin-guide/gpio/sysfs.html
 */
#include "gpio.h"

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <poll.h>

#define MAX_GPIO 100
static int gpio_export(unsigned int gpio_nr, bool export)
{
    int ret = 0;
    if (gpio_nr > MAX_GPIO) {
        gpio_err("gpio number is beyond range\n");
        ret - 1;
        goto end;
    }
    int fd = open(export ? "/sys/class/gpio/export" : "/sys/class/gpio/unexport", O_WRONLY);
    if (fd == -1) {
        gpio_err("open export file failed: %s\n", strerror(errno));
        ret = errno;
        goto end;
    }
    char buf[3];
    (void)snprintf(buf, sizeof(buf), "%d", gpio_nr); 
    if (write(fd, buf, sizeof(buf)) == -1) {
        gpio_err("write file failed: %s\n", strerror(errno)); 
        ret = errno;
        goto close_export;
    }
close_export:
    close(fd); 
end:
    return ret;
}

enum gpio_attr {
    ATTR_VALUE = 1,
    ATTR_DIRECTION = 2,
    ATTR_EDGE = 3,
};

static char *gpio_attr_path(unsigned int gpio_nr, enum gpio_attr attr)
{
    static const char *template = "/sys/devices/platform/soc/fe200000.gpio/gpiochip0/gpio/gpio%u/%s";
    static char path[PATH_MAX];
    switch (attr) {
        case ATTR_VALUE:
            snprintf(path, PATH_MAX, template, gpio_nr, "value");
            break;
        case ATTR_DIRECTION:
            snprintf(path, PATH_MAX, template, gpio_nr, "direction");
            break;
        case ATTR_EDGE:
            snprintf(path, PATH_MAX, template, gpio_nr, "edge");
            break;
        default:
            memset(path, 0, PATH_MAX);
            break;
    }
    return path; 
}

static gpio *gpio_open(unsigned int gpio_nr)
{
    gpio *io = NULL;
    if (gpio_export(gpio_nr, true) != 0) {
        gpio_err("export gpio failed\n");
        goto end;
    }
    io = (gpio *)malloc(sizeof(gpio));
    if (io == NULL) {
        gpio_err("alloc gpio failed\n");
        goto unexport_io;
    }
    io->fds.value = open(gpio_attr_path(gpio_nr, ATTR_VALUE), O_RDWR);
    if (io->fds.value == -1) {
        gpio_err("open value failed: %s\n", strerror(errno));
        goto free_io;
    }
    io->fds.direction = open(gpio_attr_path(gpio_nr, ATTR_DIRECTION), O_RDWR);
    if (io->fds.direction == -1) {
        gpio_err("open direction failed: %s\n", strerror(errno));
        goto close_value;
    }
    io->fds.edge = open(gpio_attr_path(gpio_nr, ATTR_EDGE), O_RDWR);
    if (io->fds.edge == -1) {
        gpio_err("open edge failed: %s\n", strerror(errno));
        goto close_direction;
    }
    io->gpio_nr = gpio_nr;
    goto end;
close_direction:
    close(io->fds.direction);
close_value:
    close(io->fds.value);
free_io:
    free(io);
    io = NULL;
unexport_io:
    gpio_export(gpio_nr, false);
end:
    return io;
}

static void gpio_close(gpio *io)
{
    close(io->fds.edge);
    close(io->fds.direction);
    close(io->fds.value);
    if (gpio_export(io->gpio_nr, false) != 0) {
        gpio_err("unexport gpio failed: %u\n", io->gpio_nr);
    }
    free(io);
}

static int gpio_attr_write(int fd, const void *buf, size_t len)
{
    int ret = 0;
    if (lseek(fd, 0, SEEK_SET) == -1) {
        gpio_err("lseed failed\n");
        ret = errno;
        goto end;
    }
    if (write(fd, buf, len) == -1) {
        gpio_err("write failed: %s\n", strerror(errno));
        ret = errno;
        goto end;
    }
end:
    return ret;
}

static int gpio_attr_read(int fd, void *buf, size_t len)
{
    int ret = 0;
    if (lseek(fd, 0, SEEK_SET) == -1) {
        ret = errno;
        gpio_err("lseed failed\n");
        goto end;
    }
    if (read(fd, buf, len) == -1) {
        ret = errno;
        gpio_err("read failed\n");
        goto end;
    }
end:
    return ret;
}

static int gpio_set_value(gpio *io, enum gpio_value value)
{
    int ret;
    static const char *high = "1";
    static const char *low = "0";
    switch(value) {
        case GPIO_HIGH:
            ret = gpio_attr_write(io->fds.value, high, strlen(high)); 
            break;
        case GPIO_LOW:
            ret = gpio_attr_write(io->fds.value, low, strlen(low));
            break;
        default:
            ret = -1;
            gpio_err("unsupport value\n");
            break;
    }
    return ret;
}

static int gpio_get_value(gpio *io, enum gpio_value *value)
{
    int ret;
    char buf[2];
    ret = gpio_attr_read(io->fds.value, buf, sizeof(buf));
    if (ret != 0) {
        gpio_err("read gpio value failed\n");
        goto end;
    }
    *value = buf[0] == '0' ? GPIO_LOW : GPIO_HIGH;
end:
    return ret;
}

static int gpio_set_direction(gpio *io, enum gpio_direction dir)
{
    static const char *out = "out";
    static const char *in = "in";
    int ret;
    int fd = io->fds.direction;
    switch (dir) {
        case GPIO_IN:
            ret = gpio_attr_write(fd, in, strlen(in));
            break;
        case GPIO_OUT:
            ret = gpio_attr_write(fd, out, strlen(out));
            break;
        default:
            ret = -1;
            gpio_err("unknown direction\n");
            goto end;
    }
end:
    return ret;
}

static int gpio_set_edge(gpio *io, enum gpio_edge edge)
{
    static const char *rising = "rising";
    static const char *falling = "falling";
    static const char *both = "both";
    static const char *none = "none";
    int ret;
    int fd = io->fds.edge;
    switch (edge) {
        case GPIO_RISING:
            ret = gpio_attr_write(fd, rising, strlen(rising));
            break;
        case GPIO_FALLING:
            ret = gpio_attr_write(fd, falling, strlen(falling));
            break;

        case GPIO_BOTH:
            ret = gpio_attr_write(fd, both, strlen(both));
            break;
        case GPIO_NONE:
            ret = gpio_attr_write(fd, none, strlen(none));
            break;
        default:
            gpio_err("unsupport edge\n"); 
            ret = -1;
            break;
    }
    return ret; 
}

static int gpio_handle_irq(gpio *io, irq_handler handler, void *data)
{
    int ret;
    unsigned char irq[2];
    int fd = io->fds.value; 
    struct pollfd poll_fd = { .fd = fd, .events = POLLPRI | POLLERR };
    while (true) {
        if (poll(&poll_fd, 1, -1) < 0) {
            gpio_err("poll failed %s\n", strerror(errno));
            ret = errno;
            goto end;
        }
        ret = gpio_attr_read(fd, irq, sizeof(irq));
        if (ret != 0) {
            gpio_err("read irq value failed\n");
            goto end;
        }
        ret = handler(irq[0] == '1' ? GPIO_HIGH : GPIO_LOW, data);
        if (ret != 0) {
            gpio_err("handle irq failed\n");
            goto end;
        }
    }
end:
    return ret;
}

static struct gpio_ops ops = {
    .open = gpio_open,
    .close = gpio_close,
    .set_value = gpio_set_value,
    .get_value = gpio_get_value,
    .set_direction = gpio_set_direction,
    .set_edge = gpio_set_edge,
    .handle_irq = gpio_handle_irq,
};

struct gpio_ops *get_gpio_ops()
{
    return &ops;
}
