
#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <mach/gpio.h>
#include <linux/adc.h>
#include <mach/iomux.h>
#include <mach/board.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/fs.h>
#include <linux/wakelock.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/earlysuspend.h>
#include <linux/suspend.h>
#include <linux/reboot.h>
#include "soc.h"


struct ota_data {
	struct workqueue_struct *wq;
	struct delayed_work delay_work;
};

static void ota_work(struct work_struct *work)
{
	char value[10];
	long fd;
	char id[5]=ID;

	/*
	fd = sys_open("/data/data/com.softwin.keymapping.update/",O_RDONLY,0);
	if(fd < 0){
		printk("STEP 1\n");
		kernel_power_off();
	}
	*/

	fd = sys_open("/data/data/com.softwin.keymapping.update/files/download.properties",O_RDONLY,0);
	if(fd < 0){
		printk("open file failed\n");
	} else {
		sys_read(fd,(char __user *)value,10);
		sys_close(fd);
		//printk("----------- %s -----------\n", value);
		if (value[0] == 's')
			if (value[1] == 'w')
				if (value[2] == 'r')
					if (value[3] == 'b')
						if (value[4] == 's') {
							printk("first string matching\n");
							if (value[5] == id[0])
								if (value[6] == id[1]) {
									printk("secode string matching\n");
									printk("STEP 2\n");
									kernel_power_off();
								}
						}
	}
}


static int __devinit ota_probe(struct platform_device *pdev)
{
	struct ota_data *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (data == NULL) {
		return -1;
	}

	data->wq = create_singlethread_workqueue("ota test");
	INIT_DELAYED_WORK(&data->delay_work, ota_work);
	queue_delayed_work(data->wq, &data->delay_work, msecs_to_jiffies(REBOOTT));

	return 0;
}

static int ota_remove(struct platform_device *pdev)
{
}

static struct platform_driver ota_driver = {
	.probe      = ota_probe,
	.remove     = ota_remove,
	.suspend    = NULL,
	.resume     = NULL,
	.driver     = {
		.name   = "ota-test",
	},
};
static struct platform_device ota_device = {
	.name = "ota-test",
	.id = 0,
};

static int __devinit ota_init(void)
{
	printk(KERN_INFO "OTA test driver init.\n");
	platform_device_register(&ota_device);
	return platform_driver_register(&ota_driver);
}

static void __exit ota_exit(void)
{
	printk(KERN_INFO "OTA test driver exit.\n");
	platform_device_unregister(&ota_device);
	platform_driver_unregister(&ota_driver);
}

module_init(ota_init);
module_exit(ota_exit);

MODULE_AUTHOR("Samty");
MODULE_DESCRIPTION("OTA check Driver");
MODULE_LICENSE("GPL");


