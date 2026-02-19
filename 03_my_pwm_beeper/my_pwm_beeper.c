// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/of.h>
#include <linux/sysfs.h>

struct my_beeper {
    struct pwm_device *pwm;
    struct pwm_state state;
    u32 freq;
};

static ssize_t freq_show(struct device *dev,
                         struct device_attribute *attr, char *buf)
{
    struct my_beeper *beeper = dev_get_drvdata(dev);
    return sysfs_emit(buf, "%u\n", beeper->freq);
}

static ssize_t freq_store(struct device *dev,
                          struct device_attribute *attr,
                          const char *buf, size_t count)
{
    struct my_beeper *beeper = dev_get_drvdata(dev);
    u32 freq;
    u64 period;

    if (kstrtou32(buf, 0, &freq))
        return -EINVAL;

    if (freq == 0) {
        beeper->state.enabled = false;
        pwm_apply_might_sleep(beeper->pwm, &beeper->state);
        beeper->freq = 0;
        return count;
    }

    period = DIV_ROUND_CLOSEST_ULL(NSEC_PER_SEC, freq);

    pwm_init_state(beeper->pwm, &beeper->state);
    beeper->state.period = period;
    beeper->state.duty_cycle = period / 2;
    beeper->state.enabled = true;

    pwm_apply_might_sleep(beeper->pwm, &beeper->state);

    beeper->freq = freq;
    return count;
}

static DEVICE_ATTR_RW(freq);

static int my_beeper_probe(struct platform_device *pdev)
{
    struct my_beeper *beeper;
    int ret;

    beeper = devm_kzalloc(&pdev->dev, sizeof(*beeper), GFP_KERNEL);
    if (!beeper)
        return -ENOMEM;

    beeper->pwm = devm_pwm_get(&pdev->dev, NULL);
    if (IS_ERR(beeper->pwm))
        return PTR_ERR(beeper->pwm);

    platform_set_drvdata(pdev, beeper);

    ret = device_create_file(&pdev->dev, &dev_attr_freq);
    if (ret)
        return ret;

    dev_info(&pdev->dev, "my pwm beeper probed\n");
    return 0;
}

static void my_beeper_remove(struct platform_device *pdev)
{
    struct my_beeper *beeper = platform_get_drvdata(pdev);

    beeper->state.enabled = false;
    pwm_apply_might_sleep(beeper->pwm, &beeper->state);

    device_remove_file(&pdev->dev, &dev_attr_freq);

}

static const struct of_device_id my_beeper_of_match[] = {
    { .compatible = "my,pwm-beeper" },
    {}
};
MODULE_DEVICE_TABLE(of, my_beeper_of_match);

static struct platform_driver my_beeper_driver = {
    .probe  = my_beeper_probe,
    .remove = my_beeper_remove,
    .driver = {
        .name = "my-pwm-beeper",
        .of_match_table = my_beeper_of_match,
    },
};

module_platform_driver(my_beeper_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Simple PWM Beeper Driver");
