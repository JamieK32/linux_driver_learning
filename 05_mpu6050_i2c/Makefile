# 指定内核源码路径（默认使用当前运行内核）
KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

# 生成两个模块
obj-m += mpu6050_core.o
obj-m += mpu6050_i2c.o

# 默认目标
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# 清理
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean