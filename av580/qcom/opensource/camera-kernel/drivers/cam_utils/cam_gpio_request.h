/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PONG618_CAM_GPIO_REQUEST_H
#define PONG618_CAM_GPIO_REQUEST_H
#include <linux/gpio.h>
/* Driver-owned board request descriptor; never substitutes a kernel type. */
struct cam_gpio_request {
    unsigned int gpio;
    unsigned long flags;
    const char *label;
};
static inline void cam_gpio_free_requests(const struct cam_gpio_request *array,
                                          size_t num)
{
    while (num--)
        gpio_free((array++)->gpio);
}
#endif
