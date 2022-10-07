#include "touch.h"
#include "gpio.h"

static int control_light(enum gpio_value signal, void *data)
{
    gpio *io = (gpio *)data;
    int ret;
    struct gpio_ops *ops = get_gpio_ops();
    ret = ops->set_value(io, signal); 
    if (ret != 0) {
        gpio_err("signal led failed\n");
        goto end;
    }
end:
    return ret;
}

int touch(void)
{
    int ret = 0;
    struct gpio_ops *ops = get_gpio_ops();
    gpio *irq_input = ops->open(22);
    ret = irq_input == NULL;
    if (ret != 0) {
        gpio_err("open gpio failed\n");    
        goto end;
    }
    ret = ops->set_direction(irq_input, GPIO_IN);
    if (ret != 0) {
        gpio_err("set io direction failed\n/");
        goto close_irq;
    }
    ret = ops->set_edge(irq_input, GPIO_BOTH);
    if (ret != 0) {
        gpio_err("set irq edge failed\n");
        goto close_irq;
    }
    gpio *led = ops->open(26);
    ret = led == NULL;
    if (ret != 0) {
        gpio_err("open led failed\n");
        goto close_irq;
    }
    ret = ops->set_direction(led, GPIO_OUT);
    if (ret != 0) {
        gpio_err("set io direction failed\n");
        goto close_led;
    }
    ret = ops->handle_irq(irq_input, control_light, led);
    if (ret != 0) {
        gpio_err("handle irq failed\n");
        goto close_led;
    }
close_led:
    ops->close(led);
close_irq:
    ops->close(irq_input);
end:
    return ret;
}
