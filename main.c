#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include "led_flash.h"
#include "touch.h"
#include "gpio.h"

struct rtc_gpio {
    gpio *clk;
    gpio *dat;
    gpio *rst;
    gpio *power;
};

static struct rtc_gpio *rtc_init(unsigned int power_nr, unsigned int clk_nr, unsigned int dat_nr, unsigned int rst_nr)
{
    struct rtc_gpio *rtc = (struct rtc_gpio *)malloc(sizeof(struct rtc_gpio));
    if (rtc == NULL) {
        gpio_err("malloc rtc failed\n");
        goto end;
    }
    struct gpio_ops *ops = get_gpio_ops();
    rtc->clk = ops->open(clk_nr);
    if (rtc->clk == NULL) {
        gpio_err("open clk gpio failed\n");
        goto free_rtc;
    }
    rtc->dat = ops->open(dat_nr);
    if (rtc->dat == NULL) {
        gpio_err("open dat gpio failed\n");
        goto close_clk;
    }
    rtc->rst = ops->open(rst_nr);
    if (rtc->rst == NULL) {
        gpio_err("open rst failed\n");
        goto close_dat;
    } 
    rtc->power = ops->open(power_nr);
    if (rtc->power == NULL) {
        gpio_err("open power failed\n");
        goto close_rst;
    }
    if (ops->set_direction(rtc->clk, GPIO_OUT) != 0) {
        gpio_err("set clk direction failed\n");
        goto close_power;
    }
    if (ops->set_direction(rtc->rst, GPIO_OUT) != 0) {
        gpio_err("set rst direction failed\n");
        goto close_power;
    }
    if (ops->set_direction(rtc->power, GPIO_OUT) != 0) {
        gpio_err("set power direction failed\n");
        goto close_power;
    }
    if (ops->set_value(rtc->power, GPIO_HIGH) != 0) {
        gpio_err("set power value failed\n");
        goto close_power;
    }
    goto end;
close_power:
    ops->close(rtc->power);
close_rst:
    ops->close(rtc->rst);
close_dat:
    ops->close(rtc->dat);
close_clk:
    ops->close(rtc->clk);
free_rtc:
    free(rtc);
    rtc = NULL;
end:
    return rtc;
}

static void rtc_finalize(struct rtc_gpio *rtc)
{
    struct gpio_ops *ops = get_gpio_ops();
    ops->close(rtc->rst);
    ops->close(rtc->dat);
    ops->close(rtc->clk);
    ops->close(rtc->power);
    free(rtc);
}

#define RTC_CLK_PERIOD_USEC 50
static int rtc_send_clk(struct rtc_gpio *rtc,
                        int (*func)(struct rtc_gpio *rtc, void *data, int t),
                        void *data,
                        bool high)
{
    int ret;
    struct gpio_ops *ops = get_gpio_ops();
    for (int i = 0; i < 8; ++i) {
        ret = ops->set_value(rtc->clk, GPIO_LOW);
        if (ret != 0) {
            gpio_err("send low clk signal failed\n");
            goto end;
        }
        usleep(RTC_CLK_PERIOD_USEC);
        if (!high) {
            ret = func(rtc, data, i);
            if (ret != 0) {
                gpio_err("execute clk failed\n");
                goto end;
            }
        }
        ret = ops->set_value(rtc->clk, GPIO_HIGH);
        if (ret != 0) {
            gpio_err("send high clk signal failed\n");
            goto end;
        }
        usleep(RTC_CLK_PERIOD_USEC);
        if (high) {
            ret = func(rtc, data, i);
            if (ret != 0) {
                gpio_err("execute clk failed\n");
                goto end;
            }
        }
    }
end:
    return ret;
}

static int rtc_send_data(struct rtc_gpio *rtc, void *data, int t)
{
    int ret;
    enum gpio_value val = ((*(unsigned char*)data >> t) & 1) == 0 ? GPIO_LOW : GPIO_HIGH;
    ret = get_gpio_ops()->set_value(rtc->dat, val);
    if (ret != 0) {
        gpio_err("send dat sigal failed\n");
        goto end;
    }
end:
    return ret;
}

static int rtc_get_data(struct rtc_gpio *rtc, void *data, int t)
{
    int ret;
    enum gpio_value val;
    ret = get_gpio_ops()->get_value(rtc->dat, &val);
    if (ret != 0) {
        gpio_err("send dat sigal failed\n");
        goto end;
    }
    *((unsigned char *)data) |= val == GPIO_HIGH ? 1 << t : 0;
end:
    return ret;
}

static int rtc_write(struct rtc_gpio *rtc, unsigned char cmd, unsigned char input)
{
    int ret;
    struct gpio_ops *ops = get_gpio_ops();
    ret = ops->set_value(rtc->rst, GPIO_HIGH);
    if (ret != 0) {
        gpio_err("rise rst failed\n");
        goto end;
    }
    ret = ops->set_direction(rtc->dat, GPIO_OUT);
    if (ret != 0) {
        gpio_err("set dat direction failed\n");
        goto lower_rst;
    }
    /* send cmd byte */
    ret = rtc_send_clk(rtc, rtc_send_data, &cmd, true);
    if (ret != 0) {
         gpio_err("send cmd failed\n");
         goto lower_rst;
    }
    /* send data byte */
    ret = rtc_send_clk(rtc, rtc_send_data, &input, true);
    if (ret != 0) {
        gpio_err("send data failed\n");
        goto lower_rst;
    }
lower_rst:
    ret = ops->set_value(rtc->rst, GPIO_LOW);
    if (ret != 0) {
        gpio_err("rise rst failed\n");
        goto end;
    }
end:
    return ret;
}

static int rtc_read(struct rtc_gpio *rtc, unsigned char cmd, unsigned char *output)
{
    int ret;
    struct gpio_ops *ops = get_gpio_ops();
    ret = ops->set_value(rtc->rst, GPIO_HIGH);
    if (ret != 0) {
        gpio_err("rise rst failed\n");
        goto end;
    }
    ret = ops->set_direction(rtc->dat, GPIO_OUT);
    if (ret != 0) {
        gpio_err("set dat direction failed\n");
        goto lower_rst;
    }
    /* send cmd byte */
    ret = rtc_send_clk(rtc, rtc_send_data, &cmd, true);
    if (ret != 0) {
        gpio_err("send cmd failed\n");
        goto lower_rst;
    }
    ret = ops->set_direction(rtc->dat, GPIO_IN);
    if (ret != 0) {
        gpio_err("set dat direction failed\n");
        goto lower_rst;
    }
    /* get data byte */
    memset(output, 0, 1);
    ret = rtc_send_clk(rtc, rtc_get_data, output, false);
    if (ret != 0) {
        gpio_err("get data failed\n");
        goto lower_rst;
    }
lower_rst:
    ret = ops->set_value(rtc->rst, GPIO_LOW);
    if (ret != 0) {
        gpio_err("rise rst failed\n");
        goto end;
    }
end:
    return ret;
}

typedef union tag_rtc_reg {
    union {
        struct {
            unsigned char one           : 4;
            unsigned char ten           : 3;
            unsigned char ch            : 1;
        } sec;

        struct {
            unsigned char one           : 4;
            unsigned char ten           : 3;
            unsigned char resv          : 1;
        } min;

        struct {
            unsigned char one           : 4;
            union {
                unsigned char val       : 2;
                struct {
                    unsigned char val   : 1;
                    unsigned char pm    : 1;
                } am_pm;
            } ten;
            unsigned char resv          : 1;
            unsigned char is_12         : 1;
        } hour;

        struct {
            unsigned char one           : 4;
            unsigned char ten           : 2;
            unsigned char resv          : 2;
        } date;

        struct {
            unsigned char one           : 4;
            unsigned char ten           : 1;
            unsigned char resv          : 3;
        } month;

        struct {
            unsigned char one           : 4;
            unsigned char ten           : 4;
        } year;

        struct {
            unsigned char one           : 3;
            unsigned char resv          : 5;
        } day;

        struct {
            unsigned char resv          : 7;
            unsigned char write_protect : 1;
        } wp;
    } regs;
    unsigned char val; 
} rtc_reg;

struct rtc_cmd {
    unsigned char read;
    unsigned char write;
};

enum RTC_CMDS {
    RTC_SECOND  = 0,
    RTC_MINUTE  = 1,
    RTC_HOUR    = 2,
    RTC_DATE    = 3,
    RTC_MONTH   = 4,
    RTC_YEAR    = 5,
    RTC_DAY     = 6,
    RTC_WP      = 7,
};

#define RTC_DEF_CMD(R, W) { .read = R, .write = W }

struct rtc_cmd rtc_cmds[] = {
    [RTC_SECOND]    = RTC_DEF_CMD(0x81, 0x80),
    [RTC_MINUTE]    = RTC_DEF_CMD(0x83, 0x82),
    [RTC_HOUR]      = RTC_DEF_CMD(0x85, 0x84),
    [RTC_DATE]      = RTC_DEF_CMD(0x87, 0x86),
    [RTC_MONTH]     = RTC_DEF_CMD(0x89, 0x88),
    [RTC_YEAR]      = RTC_DEF_CMD(0x8B, 0x8A),
    [RTC_DAY]       = RTC_DEF_CMD(0x8D, 0x8C),
    [RTC_WP]        = RTC_DEF_CMD(0x8F, 0x8E),
};

#define RTC_CMD(rtc_enum) rtc_cmds[rtc_enum]

static int rtc_reset_timer(struct rtc_gpio *rtc)
{
    int ret;
    rtc_reg wp = { .regs = { .wp = { .write_protect = 0 } } };
    ret = rtc_write(rtc, RTC_CMD(RTC_WP).write, wp.val);
    if (ret != 0) {
        gpio_err("rtc write wp register failed\n");
        goto end;
    }
    rtc_reg sec = { .regs = { .sec =  { .one = 0, .ten = 0 } } };
    ret = rtc_write(rtc, RTC_CMD(RTC_SECOND).write, sec.val); 
    if (ret != 0) {
        gpio_err("rtc write second register failed\n");
        goto end;
    }
    rtc_reg min = { .regs = { .min = { .one = 0, .ten = 0 } } };
    ret = rtc_write(rtc, RTC_CMD(RTC_MINUTE).write, min.val);
    if (ret != 0) {
        gpio_err("rtc write minute regiter failed\n"); 
        goto end;
    }
    rtc_reg hour = { .regs = { .hour = { .one = 0, .ten = { .val = 0 }, .is_12 = 0 } } };
    ret = rtc_write(rtc, RTC_CMD(RTC_HOUR).write, hour.val);
    if (ret != 0) {
        gpio_err("rtc write hour register faield\n");
        goto end;
    }
    rtc_reg date = { .regs = { .date = { .one = 3, .ten = 1 } } };
    ret = rtc_write(rtc, RTC_CMD(RTC_DATE).write, date.val);
    if (ret != 0) {
        gpio_err("rtc write date register failed\n");
        goto end;
    }
    rtc_reg month = { .regs = { .month = { .one = 1, .ten = 0 } } };
    ret = rtc_write(rtc, RTC_CMD(RTC_MONTH).write, month.val);
    if (ret != 0) {
        gpio_err("rtc write month register failed\n");
        goto end;
    }
    rtc_reg year = { .regs = { .year = { .one = 6, .ten = 2 } } };
    ret = rtc_write(rtc, RTC_CMD(RTC_YEAR).write, year.val);
    if (ret != 0) {
        gpio_err("rtc write year register failed\n");
        goto end;
    }
    rtc_reg day = { .regs = { .day = { .one = 3 } } };
    ret = rtc_write(rtc, RTC_CMD(RTC_DAY).write, day.val);
    if (ret != 0) {
        gpio_err("rtc write day register failed\n");
        goto end;
    }
    ret = rtc_write(rtc, 0x90, 0x1);
    if (ret != 0) {
        gpio_err("rtc write charge register failed\n");
        goto end;
    }
    ret = rtc_write(rtc, 0xc0, 0xf0);
    if (ret != 0) {
        gpio_err("rtc write init register failed\n");
        goto end;
    }
    ret = rtc_write(rtc, 0x8e, 0x80);
    if (ret != 0) {
        gpio_err("rtc write wp register failed\n");
        goto end;
    }
end:
    return ret;
}

static void rtc_print_time(rtc_reg second,
                           rtc_reg minute,
                           rtc_reg hour,
                           rtc_reg date,
                           rtc_reg month,
                           rtc_reg year,
                           rtc_reg day)
{
    printf("%04u-%02u-%02u %02u:%02u:%02u %uth\n",
           1970 + year.regs.year.one + year.regs.year.ten * 10,
           month.regs.month.one + month.regs.month.ten * 10,
           date.regs.date.one + date.regs.date.ten * 10,
           hour.regs.hour.one + hour.regs.hour.ten.val * 10,
           minute.regs.min.one + minute.regs.min.ten * 10,
           second.regs.sec.one + second.regs.sec.ten * 10,
           day.regs.day.one);
}

static int rtc_read_timer(struct rtc_gpio *rtc)
{
    int ret;
    rtc_reg second = { 0 };
    ret = rtc_read(rtc, RTC_CMD(RTC_SECOND).read, &second.val);
    if (ret != 0) {
        gpio_err("rtc read second failed\n");
        goto end;
    }
    rtc_reg minute = { 0 };
    ret = rtc_read(rtc, RTC_CMD(RTC_MINUTE).read, &minute.val);
    if (ret != 0) {
        gpio_err("rtc read minute failed\n");
        goto end;
    }
    rtc_reg hour = { 0 };
    ret = rtc_read(rtc, RTC_CMD(RTC_HOUR).read, &hour.val);
    if (ret != 0) {
        gpio_err("rtc read hour failed\n");
        goto end;
    }
    rtc_reg date = { 0 };
    ret = rtc_read(rtc, RTC_CMD(RTC_DATE).read, &date.val);
    if (ret != 0) {
        gpio_err("rtc read date failed\n");
        goto end;
    }
    rtc_reg month = { 0 };
    ret = rtc_read(rtc, RTC_CMD(RTC_MONTH).read, &month.val);
    if (ret != 0) {
        gpio_err("rtc read month failed\n");
        goto end;
    }
    rtc_reg year = { 0 };
    ret = rtc_read(rtc, RTC_CMD(RTC_YEAR).read, &year.val);
    if (ret != 0) {
        gpio_err("rtc read year failed\n");
        goto end;
    }
    rtc_reg day = { 0 };
    ret = rtc_read(rtc, RTC_CMD(RTC_DAY).read, &day.val);
    if (ret != 0) {
        gpio_err("rtc read day failed\n");
        goto end;
    }
    rtc_print_time(second, minute, hour, date, month, year, day);
end:
    return ret;
}

int real_time_clock(void)
{
    int ret;
    struct rtc_gpio *rtc = rtc_init(18, 23, 24, 25);
    ret = rtc == NULL;
    if (ret != 0) {
        gpio_err("init rtc failed\n");
        goto end;
    }
    ret = rtc_reset_timer(rtc);
    if (ret != 0) {
        gpio_err("reset timer failed\n");
        goto finalize;
    }
    for (int i = 0; i < 10; ++i) {
        (void)sleep(1);
        ret = rtc_read_timer(rtc);
        if (ret != 0) {
            gpio_err("read timer failed\n");
            goto finalize;
        }
    }
finalize:
    rtc_finalize(rtc);
end:
    return ret;    
}

int main()
{
    //led_flash(10, 1);
    //touch();
    real_time_clock();
}
