/* 
 * drivers/input/joystick/joystick.c
 *
 * Joystick driver. 
 *
 * Copyright (c) 2013  SOFTWIN Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/async.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <linux/irq.h>
#include <mach/board.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include "soc.h"


#define JOY_NAME	            	"sf_joystick"
/* ddl@rock-chips.com : 100kHz */
#define CONFIG_SENSOR_I2C_SPEED     	100000
#define JOY_IRQ	            		RK30_PIN0_PC6
#define JOY_POWER			RK30_PIN0_PB1
#define MAX				0xff

struct joystick_ts_data {
	struct i2c_client *client;
	int irq;
	struct work_struct pen_event_work;
	struct workqueue_struct *ts_workqueue;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend joystick_early_suspend;
#endif
};

static int sensor_read(struct i2c_client *client, u8 reg, u8 *read_buf, int len)
{
	struct i2c_msg msg[2];

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].buf = &reg;
	msg[0].len = 1;
	msg[0].scl_rate = CONFIG_SENSOR_I2C_SPEED;
	msg[0].read_type = 2;   /* fpga i2c:0==I2C_NO_STOP : direct use number not enum for don't want include spi_fpga.h */

	msg[1].addr =client->addr;
	msg[1].flags = client->flags|I2C_M_RD;
	msg[1].buf = read_buf;
	msg[1].len = len;
	msg[1].scl_rate = CONFIG_SENSOR_I2C_SPEED;
	msg[1].read_type = 2;   /* fpga i2c:0==I2C_NO_STOP : direct use number not enum for don't want include spi_fpga.h */

	return i2c_transfer(client->adapter, msg, 2);
}

u8 read_value[4] = {0x7f, 0x7f, 0x7f, 0x7f};
//left_h, left_v, right_h, right_v
void soc_value(u8 *value)
{
	value[0] = read_value[1];
	value[1] = read_value[0];
	value[2] = 0xff - read_value[2];
	value[3] = 0xff - read_value[3];
	//printk(" %s : %x %x %x %x\n", __func__, value[0], value[1], value[2], value[3]);
	return;
}

static void joystick_queue_work(struct work_struct *work)
{

	struct joystick_ts_data *data = container_of(work, struct joystick_ts_data, pen_event_work);
	int ret;
	u8 read_buf[4] = {0x7f, 0x7f, 0x7f, 0x7f};

	ret = sensor_read(data->client, 0x55, read_buf, 4);	
	if (ret < 0) {
		dev_err(&data->client->dev, "joystick_read_regs fail:%d!\n",ret);
	} else {
		read_value[0] = read_buf[1];
		read_value[1] = read_buf[0];
		read_value[2] = read_buf[2];
		read_value[3] = MAX - read_buf[3];
		//printk("%x %x %x %x\n", read_buf[0], read_buf[1], read_buf[2], read_buf[3]);
		//printk("%x %x %x %x\n", read_value[0], read_value[1], read_value[2], read_value[3]);
	}

	enable_irq(data->irq);
	return;
}

static irqreturn_t joystick_interrupt(int irq, void *dev_id)
{
	struct joystick_ts_data *joystick_ts = dev_id;

	disable_irq_nosync(joystick_ts->irq);
	if (!work_pending(&joystick_ts->pen_event_work)) 
		queue_work(joystick_ts->ts_workqueue, &joystick_ts->pen_event_work);

	return IRQ_HANDLED;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void joystick_suspend(struct early_suspend *h)
{
	struct joystick_ts_data *joystick_ts;
	joystick_ts = container_of(h, struct joystick_ts_data, joystick_early_suspend);

        gpio_set_value(JOY_POWER,1);
	disable_irq_nosync(joystick_ts->irq);
}

static void joystick_resume(struct early_suspend *h)
{
	struct joystick_ts_data *joystick_ts;
	joystick_ts = container_of(h, struct joystick_ts_data, joystick_early_suspend);

        gpio_set_value(JOY_POWER,0);
	enable_irq(joystick_ts->irq);
}
#endif

static int  joystick_probe(struct i2c_client *client ,const struct i2c_device_id *id)
{
	struct joystick_ts_data *joystick_ts;
	int err = 0;
	int ret = 0;

	ret = gpio_request(JOY_POWER, NULL);
	if (ret != 0) {
		printk(" request joystick power pin fail\n");
		gpio_free(JOY_POWER);
		return -ENOMEM;
	}

        gpio_direction_output(JOY_POWER, 0);
	dev_info(&client->dev, "joystick_ts_probe!\n");
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)){
		dev_err(&client->dev, "Must have I2C_FUNC_I2C.\n");
		return -ENODEV;
	}

	joystick_ts = kzalloc(sizeof(*joystick_ts), GFP_KERNEL);	
	if (!joystick_ts)	{
		return -ENOMEM;
	}

	joystick_ts->client = client;
	joystick_ts->irq = JOY_IRQ;


	ret = gpio_request(JOY_IRQ, NULL);
	if (ret != 0) {
		printk(" request joystick irq pin fail\n");
		gpio_free(JOY_IRQ);
		return -ENOMEM;
	}
	if (!joystick_ts->irq) {
		err = -ENODEV;
		dev_err(&joystick_ts->client->dev, "no IRQ?\n");
		goto exit_no_irq_fail;
	}else{
		joystick_ts->irq = gpio_to_irq(joystick_ts->irq);
	}

	INIT_WORK(&joystick_ts->pen_event_work, joystick_queue_work);
	joystick_ts->ts_workqueue = create_singlethread_workqueue("joystick_ts");
	if (!joystick_ts->ts_workqueue) {
		err = -ESRCH;
		goto exit_create_singlethread;
	}

	ret = request_irq(joystick_ts->irq, joystick_interrupt, IRQF_TRIGGER_FALLING, client->dev.driver->name, joystick_ts);
	if (ret < 0) {
		dev_err(&client->dev, "irq %d busy?\n", joystick_ts->irq);
		goto exit_irq_request_fail;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	joystick_ts->joystick_early_suspend.suspend =joystick_suspend;
	joystick_ts->joystick_early_suspend.resume =joystick_resume;
	joystick_ts->joystick_early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;;
	register_early_suspend(&joystick_ts->joystick_early_suspend);
#endif
	i2c_set_clientdata(client, joystick_ts);

	return 0;

exit_irq_request_fail:
	cancel_work_sync(&joystick_ts->pen_event_work);
	destroy_workqueue(joystick_ts->ts_workqueue);
exit_create_singlethread:
exit_no_irq_fail:
	kfree(joystick_ts);
	return err;
}


static int __devexit joystick_remove(struct i2c_client *client)
{
	struct joystick_ts_data *joystick_ts = i2c_get_clientdata(client);

	free_irq(joystick_ts->irq, joystick_ts);
	kfree(joystick_ts);
	cancel_work_sync(&joystick_ts->pen_event_work);
	destroy_workqueue(joystick_ts->ts_workqueue);
	i2c_set_clientdata(client, NULL);
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&joystick_ts->joystick_early_suspend);
#endif 
	return 0;
}

static struct i2c_device_id joystick_idtable[] = {
	{JOY_NAME, 0},
	{ }
};

MODULE_DEVICE_TABLE(i2c, joystick_idtable);

static struct i2c_driver joystick_driver  = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= JOY_NAME
	},
	.id_table	= joystick_idtable,
	.probe      	= joystick_probe,
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend	= joystick_suspend,
	.resume     	= joystick_resume,
#endif
	.remove		=  __devexit_p(joystick_remove),

};

static int __init joystick_ts_init(void)
{
	return i2c_add_driver(&joystick_driver);
}

static void __exit joystick_ts_exit(void)
{
	i2c_del_driver(&joystick_driver);
}

module_init(joystick_ts_init);
module_exit(joystick_ts_exit);

MODULE_DESCRIPTION("softwin joystick driver");
MODULE_AUTHOR("softwin");
MODULE_LICENSE("GPL");
