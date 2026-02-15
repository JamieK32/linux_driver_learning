#include <linux/module.h>
#include <linux/export-internal.h>
#include <linux/compiler.h>

MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};



static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0xfa474811, "__platform_driver_register" },
	{ 0xf9a482f9, "msleep" },
	{ 0x30708b69, "gpiod_get_value" },
	{ 0x41cc93f6, "input_event" },
	{ 0x36a78de3, "devm_kmalloc" },
	{ 0x3250fd9c, "devm_gpiod_get" },
	{ 0x2bc19084, "gpiod_to_irq" },
	{ 0x1940079d, "devm_input_allocate_device" },
	{ 0xcab72f31, "input_register_device" },
	{ 0x3ce80115, "devm_request_threaded_irq" },
	{ 0x3bb3b979, "_dev_info" },
	{ 0x3353ee2e, "dev_err_probe" },
	{ 0x61fd46a9, "platform_driver_unregister" },
	{ 0x474e54d2, "module_layout" },
};

MODULE_INFO(depends, "");

MODULE_ALIAS("of:N*T*Cmycompany,mykeys");
MODULE_ALIAS("of:N*T*Cmycompany,mykeysC*");

MODULE_INFO(srcversion, "95EA1604252540470114E38");
