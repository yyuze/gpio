#include "led_flash.h"

#include <unistd.h>

#include "gpio.h"

int led_flash(int times, float hz)
{
    int ret;
    struct gpio_ops *ops = get_gpio_ops();
    gpio *io = ops->open(26);
    if (io == NULL) {
        ret = -1;
        gpio_err("open io failed\n");
        goto end;
    }
    ops->set_direction(io, GPIO_OUT);
    for (int i = 0; i < times; ++i) {
        ret = ops->set_value(io, GPIO_HIGH);
        if (ret != 0) {
            gpio_err("send high signal failed: %s\n");
            goto close_gpio;
        }
        (void)usleep(500000/hz); 
        ret = ops->set_value(io, GPIO_LOW);
        if (ret != 0) {
            gpio_err("send low signal failed: %s\n");
            goto close_gpio;
        }
        (void)usleep(500000/hz); 
    }
close_gpio:
    ops->close(io);
end:
    return ret;
}
