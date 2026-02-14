#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/timer.h>

struct myled_data {
    struct gpio_desc *gpiod_r;
    struct gpio_desc *gpiod_g;
    struct gpio_desc *gpiod_b;
    struct timer_list timer;
    bool state;
    unsigned int period_ms;
    unsigned int color;
};

static ssize_t period_ms_show(struct device *dev,
                              struct device_attribute *attr, char *buf)
{
    struct myled_data *led = dev_get_drvdata(dev);
    return sysfs_emit(buf, "%u\n", led->period_ms);
}

static ssize_t period_ms_store(struct device *dev,
                               struct device_attribute *attr,
                               const char *buf, size_t count)
{
    struct myled_data *led = dev_get_drvdata(dev);
    unsigned int v;

    if (kstrtouint(buf, 0, &v))
        return -EINVAL;

    /* 防止 0 或太小导致疯狂定时器 */
    if (v < 10)
        v = 10;

    led->period_ms = v;

    /* 立刻用新周期重置下一次触发 */
    mod_timer(&led->timer, jiffies + msecs_to_jiffies(led->period_ms));

    return count;
}

static DEVICE_ATTR_RW(period_ms);

static ssize_t rgb_color_show(struct device *dev,
                              struct device_attribute *attr, char *buf)
{
    struct myled_data *led = dev_get_drvdata(dev);
    return sysfs_emit(buf, "%u\n", led->color);
}

static ssize_t rgb_color_store(struct device *dev,
                               struct device_attribute *attr,
                               const char *buf, size_t count)
{
    struct myled_data *led = dev_get_drvdata(dev);
    unsigned int v;

    if (kstrtouint(buf, 0, &v))
        return -EINVAL;

    /* 防止 0 或太小导致疯狂定时器 */
    if (v >= 0 && v <= 7) {
        led->color = v;
    } else {
        led->color = 0;
    }

    /* 立即根据当前 state 更新 GPIO */
    if (led->state) {
        gpiod_set_value(led->gpiod_r, led->color & 1);
        gpiod_set_value(led->gpiod_g, led->color & 2);
        gpiod_set_value(led->gpiod_b, led->color & 4);
    }

    return count;
}

static DEVICE_ATTR_RW(rgb_color);


static void led_timer_func(struct timer_list *t)
{
    struct myled_data *led = from_timer(led, t, timer);

    led->state = !led->state;
    gpiod_set_value(led->gpiod_r,
        (led->color & 1) ? led->state : 0);

    gpiod_set_value(led->gpiod_g,
        (led->color & 2) ? led->state : 0);

    gpiod_set_value(led->gpiod_b,
        (led->color & 4) ? led->state : 0);
    mod_timer(&led->timer, jiffies + msecs_to_jiffies(led->period_ms));
}

static int myled_probe(struct platform_device *pdev)
{
    struct myled_data *led;
    int ret;
    
    led = devm_kzalloc(&pdev->dev, sizeof(*led), GFP_KERNEL);
    if (!led)
        return -ENOMEM;
    led->period_ms = 500;
    led->color     = 0;

    led->gpiod_r = devm_gpiod_get(&pdev->dev, "led-r", GPIOD_OUT_LOW);
    if (IS_ERR(led->gpiod_r))
        return dev_err_probe(&pdev->dev, PTR_ERR(led->gpiod_r),
                            "failed to get led-r gpio\n");

    led->gpiod_g = devm_gpiod_get(&pdev->dev, "led-g", GPIOD_OUT_LOW);
    if (IS_ERR(led->gpiod_g))
        return dev_err_probe(&pdev->dev, PTR_ERR(led->gpiod_g),
                            "failed to get led-g gpio\n");

    led->gpiod_b = devm_gpiod_get(&pdev->dev, "led-b", GPIOD_OUT_LOW);
    if (IS_ERR(led->gpiod_b))
        return dev_err_probe(&pdev->dev, PTR_ERR(led->gpiod_b),
                            "failed to get led-b gpio\n");

    timer_setup(&led->timer, led_timer_func, 0);
    mod_timer(&led->timer, jiffies + msecs_to_jiffies(led->period_ms));

    platform_set_drvdata(pdev, led);

    dev_info(&pdev->dev, "GPIO 13 19 26 LED blink driver loaded\n");



    ret = device_create_file(&pdev->dev, &dev_attr_period_ms);
    if (ret)
        return ret;

    ret = device_create_file(&pdev->dev, &dev_attr_rgb_color);
    if (ret) {
        device_remove_file(&pdev->dev, &dev_attr_period_ms);
        return ret;
    }
    return 0;
}

static void myled_remove(struct platform_device *pdev)
{
    struct myled_data *led = platform_get_drvdata(pdev);

    del_timer_sync(&led->timer);
    gpiod_set_value(led->gpiod_r, 0);
    gpiod_set_value(led->gpiod_g, 0);
    gpiod_set_value(led->gpiod_b, 0);

    device_remove_file(&pdev->dev, &dev_attr_period_ms);
    device_remove_file(&pdev->dev, &dev_attr_rgb_color);
}

static const struct of_device_id myled_of_match[] = {
    { .compatible = "mycompany,myled" },
    { }
};
MODULE_DEVICE_TABLE(of, myled_of_match);

static struct platform_driver myled_driver = {
    .probe = myled_probe,
    .remove = myled_remove,
    .driver = {
        .name = "myled",
        .of_match_table = myled_of_match,
    },
};

module_platform_driver(myled_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("You");
MODULE_DESCRIPTION("GPIO25 LED Blink Driver");
