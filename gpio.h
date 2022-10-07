#ifndef GPIO_H
#define GPIO_H

#include <stdio.h>

#define gpio_err(str, ...) \
    do { \
        fprintf(stderr, "[%s+%d: %s] "str, __func__, __FILE__, __LINE__, ##__VA_ARGS__); \
    } while(0)

struct gpio_fd {
    int value;
    int direction;
    int edge;
};

typedef struct tag_gpio {
    unsigned int gpio_nr;
    struct gpio_fd fds;
} gpio;

enum gpio_value {
    GPIO_HIGH = 0,
    GPIO_LOW = 1,
};

enum gpio_direction {
    GPIO_IN = 0,
    GPIO_OUT = 1,
};

enum gpio_edge {
    GPIO_RISING = 0,
    GPIO_FALLING = 1,
    GPIO_BOTH = 2,
    GPIO_NONE = 3,
};

typedef int (*irq_handler)(enum gpio_value signal, void *data);

struct gpio_ops {
    gpio *(*open)(unsigned int gpio_nr);
    void (*close)(gpio *io);
    int (*set_direction)(gpio *io, enum gpio_direction dir);
    int (*set_value)(gpio *io, enum gpio_value value);
    int (*get_value)(gpio *io, enum gpio_value *value);
    int (*set_edge)(gpio *io, enum gpio_edge);
    int (*handle_irq)(gpio *io, irq_handler handler, void *data);
};

struct gpio_ops *get_gpio_ops();

#endif
