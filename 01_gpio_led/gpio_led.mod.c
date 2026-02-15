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
	{ 0x3b6c41ea, "kstrtouint" },
	{ 0x991fb4bf, "gpiod_set_value" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0xe783e261, "sysfs_emit" },
	{ 0x7f02188f, "__msecs_to_jiffies" },
	{ 0x15ba50a6, "jiffies" },
	{ 0xc38c83b8, "mod_timer" },
	{ 0x82ee90dc, "timer_delete_sync" },
	{ 0x95d03246, "device_remove_file" },
	{ 0x36a78de3, "devm_kmalloc" },
	{ 0x3250fd9c, "devm_gpiod_get" },
	{ 0xc6f46339, "init_timer_key" },
	{ 0x3bb3b979, "_dev_info" },
	{ 0x91b8d433, "device_create_file" },
	{ 0x3353ee2e, "dev_err_probe" },
	{ 0x61fd46a9, "platform_driver_unregister" },
	{ 0x474e54d2, "module_layout" },
};

MODULE_INFO(depends, "");

MODULE_ALIAS("of:N*T*Cmycompany,myled");
MODULE_ALIAS("of:N*T*Cmycompany,myledC*");

MODULE_INFO(srcversion, "C9BE40D561FDFA9C161871D");
