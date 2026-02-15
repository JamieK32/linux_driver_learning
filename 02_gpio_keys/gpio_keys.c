#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/delay.h>

struct mykeys_data {
    struct gpio_desc *gpiod;
    int irq;
    struct input_dev *input;
};

static irqreturn_t mykeys_irq_thread(int irq, void *dev_id)
{
    struct mykeys_data *data = dev_id;
    int value;

    /* 简单消抖 */
    msleep(10);

    value = gpiod_get_value(data->gpiod);

    input_report_key(data->input, KEY_ENTER, !value);
    input_sync(data->input);

    return IRQ_HANDLED;
}

static int mykeys_probe(struct platform_device *pdev)
{
    struct mykeys_data *data;
    int ret;

    data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    /* 获取 GPIO */
    data->gpiod = devm_gpiod_get(&pdev->dev, "key", GPIOD_IN);
    if (IS_ERR(data->gpiod))
        return dev_err_probe(&pdev->dev, PTR_ERR(data->gpiod),
                             "failed to get key gpio\n");

    /* GPIO 转 IRQ */
    data->irq = gpiod_to_irq(data->gpiod);
    if (data->irq < 0)
        return data->irq;

    /* 申请 input 设备 */
    data->input = devm_input_allocate_device(&pdev->dev);
    if (!data->input)
        return -ENOMEM;

    data->input->name = "my-gpio-key";
    data->input->phys = "my-gpio-key/input0";
    data->input->id.bustype = BUS_HOST;

    __set_bit(EV_KEY, data->input->evbit);
    __set_bit(KEY_ENTER, data->input->keybit);

    ret = input_register_device(data->input);
    if (ret)
        return ret;

    /* 申请中断 */
    ret = devm_request_threaded_irq(&pdev->dev,
                                    data->irq,
                                    NULL,
                                    mykeys_irq_thread,
                                    IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_ONESHOT,
                                    "my-gpio-key",
                                    data);
    if (ret)
        return ret;

    platform_set_drvdata(pdev, data);

    dev_info(&pdev->dev, "GPIO key driver loaded\n");

    return 0;
}

static const struct of_device_id mykeys_of_match[] = {
    { .compatible = "mycompany,mykeys" },
    { }
};
MODULE_DEVICE_TABLE(of, mykeys_of_match);

static struct platform_driver mykeys_driver = {
    .probe  = mykeys_probe,
    .driver = {
        .name = "mykeys",
        .of_match_table = mykeys_of_match,
    },
};

module_platform_driver(mykeys_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("You");
MODULE_DESCRIPTION("Industrial GPIO Key Driver with Input Subsystem");
