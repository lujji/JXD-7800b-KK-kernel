/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/module.h>

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/adc.h>
#include <linux/earlysuspend.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/workqueue.h>

#include <asm/gpio.h>
#include <mach/board.h>
#include <plat/key.h>

#include "usb_adc_js.h"


#define VERTICAL	0

//disable let DPAD as axis5 and axis6
//disable report L2 and R2 key
#define L2_R2		1




#define ADC_MAX		0xff
//#define HALF_ADC_MAX	(ADC_MAX / 2)
#define HALF_ADC_MAX	0x7f

#define MID_BLIND       10
#define MINEDG_BLIND	4
#define MAXEDG_BLIND	(0xff - 4)




#define AXIS_NUM 6


struct kp {
	struct input_dev *input_joystick;
	struct work_struct work_update;
	struct timer_list timer;

	int js_value[AXIS_NUM];
	int js_flag[AXIS_NUM];
	int key_value[AXIS_NUM];
	int key_valid[AXIS_NUM];
	u8 value[AXIS_NUM];


#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend joystick_early_suspend;
#endif
};



struct game_key{
	unsigned char *name;
	int code;       
	int value;
	int old_value;
	int flag;

};

static struct game_key gamekeys[] = {
	//name		code		value	old_value	flag
	{"keya",	BTN_A,		0,	0,     		0},
	{"keyb",	BTN_B,		0,	0,     		0},
	{"keyx",	BTN_X,		0,	0,     		0},
	{"keyy",	BTN_Y,		0,	0,     		0},
	{"keyl",	BTN_TL,		0,	0,     		0},
	{"keyr",	BTN_TR,		0,	0,     		0},
	{"keyl2",	BTN_TL2,	0,	0,     		0},
	{"keyr2",	BTN_TR2,	0,	0,     		0},
	{"LEFT",	KEY_LEFT,	0,	0,     		0},
	{"RIGHT",	KEY_RIGHT,	0,	0,     		0},
	{"UP",		KEY_UP,		0,	0,     		0},
	{"DOWN",	KEY_DOWN,	0,	0,     		0},
	{"keyl3",	BTN_THUMBL,	0,	0,     		0},
	{"keyr3",	BTN_THUMBR,	0,	0,     		0},
	{"START",	BTN_START,	0,	0,     		0},
	{"SELECT",	BTN_SELECT,	0,	0,     		0},
};
static int keynum = sizeof(gamekeys)/sizeof(gamekeys[0]);

int keyselect, keystart;
int keya, keyb, keyx, keyy;
int keyl1, keyr1, keyl2, keyr2, keyl3, keyr3;
int keyleft, keyright, keyup, keydown;
int axis0 = 0x7f, axis1 = 0x7f, axis2 = 0x7f, axis3 = 0x7f, axis4 = 0x7f, axis5 = 0x7f;
int joystick=0;

static void read_keys_value(void)
{
	gamekeys[0].value = keya; //A
	gamekeys[1].value = keyb; //B
	gamekeys[2].value = keyx; //X
	gamekeys[3].value = keyy; //Y
	gamekeys[4].value = keyl1; //L1
	gamekeys[5].value = keyr1; //R1
	gamekeys[6].value = keyl2; //L2
	gamekeys[7].value = keyr2; //R2
	gamekeys[8].value = keyleft; //left
	gamekeys[9].value = keyright; //right
	gamekeys[10].value = keyup; //up
	gamekeys[11].value = keydown; //down
	gamekeys[12].value = keyl3; //L3
	gamekeys[13].value = keyr3; //R3
	gamekeys[14].value = keystart; //start
	gamekeys[15].value = keyselect; //select
}

struct joystick_axis {
	unsigned char *name;
	int code;       
	int code2;       
};

static struct joystick_axis axis[] = {
	//name			code		code2
	{"LAS L2R",		ABS_X,		0},
	{"LAS U2D",		ABS_Y,		0},
	{"RAS L2R",		ABS_Z,		0},
	{"left trigger",	ABS_BRAKE,	ABS_RX},
	{"right trigger",	ABS_GAS,	ABS_RY},
	{"RAS U2D",		ABS_RZ,		0},
};
static int axisnum = sizeof(axis)/sizeof(axis[0]);

static void js_report(struct kp *kp, int value, int id)
{
	int i;
	
	if (id >= axisnum) 
		return ;

	i = id;
	if (value == 0) {
		if (kp->js_flag[i]) {
			kp->js_flag[i] = 0;
			input_report_abs(kp->input_joystick, axis[i].code, 0);
			if (axis[i].code2)
				input_report_abs(kp->input_joystick, axis[i].code2, 0);
		}
	} else {
		kp->js_flag[i] = 1;
		input_report_abs(kp->input_joystick, axis[i].code, value);
		if (axis[i].code2)
			input_report_abs(kp->input_joystick, axis[i].code2, value);
	}
}




static void scan_joystick(struct kp *kp, int channel)
{
	int js_value;

	js_value = kp->value[channel];
	if (js_value >= 0) {
		if ((js_value >= HALF_ADC_MAX - MID_BLIND) && (js_value <= HALF_ADC_MAX + MID_BLIND)) {
			kp->js_value[channel] = 0;
		} else {
			if (js_value <= MINEDG_BLIND)
				js_value = -256;
			else if (js_value >= MAXEDG_BLIND)
				js_value = 255;
			else
				js_value = (js_value - HALF_ADC_MAX) * 2;
			kp->js_value[channel] = js_value;
			//printk("---------------------- %i js_value = %d -------------------\n", channel, js_value);
		}
	}
}


static void scan_joystick_absnormal(struct kp *kp, int channel)
{
	int js_value;

	js_value = kp->value[channel];
	if (js_value >= 0) {
		if ((js_value >= HALF_ADC_MAX - MID_BLIND) && (js_value <= HALF_ADC_MAX + MID_BLIND)) {
			kp->js_value[channel] = 0;
		} else {
			if (js_value <= MINEDG_BLIND)
				js_value = -170;
			else if (js_value >= MAXEDG_BLIND)
				js_value = 169;
			else
				js_value = (js_value - HALF_ADC_MAX) * 3 / 2;
			kp->js_value[channel] = js_value;
		}
	}
}

//left_h, left_v, right_h, right_v
void usb_value(u8 *value, int change)
{
	if (change) {
		value[0] = axis1;
		value[1] = axis0;
	} else {
		value[0] = axis0;
		value[1] = axis1;
	}
	value[2] = axis3;
	value[3] = axis5;
	//printk(" %s : %x %x %x %x\n", __func__, value[0], value[1], value[2], value[3]);
	return;
}
static void scan_left_joystick(struct kp *kp)
{
	//left_h, left_v, right_h, right_v
	usb_value(kp->value, 1);
	scan_joystick(kp, 0);
	scan_joystick(kp, 1);
}

static void scan_right_joystick(struct kp *kp)
{
	//left_h, left_v, right_h, right_v
	usb_value(kp->value, 1);
	scan_joystick(kp, 2);
	scan_joystick(kp, 3);

}

static void report_joystick_key(struct kp *kp)
{
	int i;

	read_keys_value();
	for (i = 0; i < keynum; i++) {
		if(gamekeys[i].value == gamekeys[i].old_value) {
			if (gamekeys[i].value == gamekeys[i].flag) {
				if(gamekeys[i].value) {
					//printk("%s press\n", gamekeys[i].name);
#if L2_R2
					input_report_key(kp->input_joystick, gamekeys[i].code, 1);
					input_mt_sync(kp->input_joystick);
#else
					if((i == 6) || (i == 7)) {
						;
					} else {
						input_report_key(kp->input_joystick, gamekeys[i].code, 1);
						input_mt_sync(kp->input_joystick);
					}
#endif
					gamekeys[i].flag = 0;

				} else {
					//printk("%s release\n", gamekeys[i].name);
#if L2_R2
					input_report_key(kp->input_joystick, gamekeys[i].code, 0);
					input_mt_sync(kp->input_joystick);
#else
					if((i == 6) || (i == 7)) {
						;
					} else {
						input_report_key(kp->input_joystick, gamekeys[i].code, 0);
						input_mt_sync(kp->input_joystick);
					}
#endif
					gamekeys[i].flag = 1;
				}
			}
		}
		gamekeys[i].old_value = gamekeys[i].value;
	}
}



static void kp_work(struct kp *kp)
{

		scan_left_joystick(kp);
#if VERTICAL
		js_report(kp, kp->js_value[0], 0); //left
		js_report(kp, kp->js_value[1], 1); //left
#else
		js_report(kp, kp->js_value[1], 0); //left
		js_report(kp, kp->js_value[0], 1); //left
#endif
		input_sync(kp->input_joystick);

		scan_right_joystick(kp);
		js_report(kp, kp->js_value[3], 5); //right
		js_report(kp, kp->js_value[2], 2); //right
		input_sync(kp->input_joystick);


		report_joystick_key(kp);
		input_sync(kp->input_joystick);


}

static void update_work_func(struct work_struct *work)
{
	struct kp *kp_data = container_of(work, struct kp, work_update);

	kp_work(kp_data);
}

static void kp_timer_sr(unsigned long data)
{
	struct kp *kp_data=(struct kp *)data;
	schedule_work(&(kp_data->work_update));
	mod_timer(&kp_data->timer,jiffies+msecs_to_jiffies(10));
}


#ifdef CONFIG_HAS_EARLYSUSPEND
static void joystick_suspend(struct early_suspend *handler)
{
	struct kp *kp;
	kp = container_of(handler, struct kp, joystick_early_suspend);
	printk("usb_suspend\n");
	keya = keyb = keyx = keyy = 0;
	keyl1 = keyr1 = keyl2 = keyr2= keyl3 = keyr3 = 0;
	axis0 = axis1 = axis2 = axis3 =axis4 = axis5 = 0x7f;
	keyleft = keyright = keyup = keydown = 0;
	keyselect = keystart = 0;
}

static void joystick_resume(struct early_suspend *handler)
{
	struct kp *kp;
	kp = container_of(handler, struct kp, joystick_early_suspend);
	printk("usb_usb_resume\n");

}
#endif

static int __devinit joystick_probe(struct platform_device *pdev)
{
	struct kp *kp;
	int i, ret;

	kp = kzalloc(sizeof(struct kp), GFP_KERNEL);
	if (!kp) {
		kfree(kp);
		return -ENOMEM;
	}
	for (i=0; i<AXIS_NUM; i++) {
		kp->js_value[i] = 0;
		kp->js_flag[i] = 0;
	}

	//register joystick
	kp->input_joystick = input_allocate_device();
	if (!kp->input_joystick) {
		printk("---------- allocate input_joystick fail ------------\n");
		kfree(kp);
		input_free_device(kp->input_joystick);
		return -ENOMEM;
	}

	for (i = 0; i < keynum; i++)
		set_bit(gamekeys[i].code, kp->input_joystick->keybit);
	//for hot key
	set_bit(EV_REP, kp->input_joystick->evbit);
	set_bit(EV_KEY, kp->input_joystick->evbit);
	set_bit(EV_ABS, kp->input_joystick->evbit);
	set_bit(EV_SYN, kp->input_joystick->evbit);
	input_set_abs_params(kp->input_joystick, ABS_X, -256, 255, 0, 0);
	input_set_abs_params(kp->input_joystick, ABS_Y, -256, 255, 0, 0);
	input_set_abs_params(kp->input_joystick, ABS_Z, -256, 255, 0, 0);
	input_set_abs_params(kp->input_joystick, ABS_RZ, -256, 255, 0, 0);
	input_set_abs_params(kp->input_joystick, ABS_BRAKE, 0, 255, 0, 0);
	input_set_abs_params(kp->input_joystick, ABS_GAS, 0, 255, 0, 0);
	input_set_abs_params(kp->input_joystick, ABS_RX, 0, 255, 0, 0);
	input_set_abs_params(kp->input_joystick, ABS_RY, 0, 255, 0, 0);

	//kp->input_joystick->name = "Microsoft X-Box 360";
	//kp->input_joystick->name = "PLAYSTATION(R)3";
	//kp->input_joystick->name = "OUYA Game Controller";
	//kp->input_joystick->name = "NVIDIA Controller";
	kp->input_joystick->name = "USB joystick";
	kp->input_joystick->id.bustype = BUS_ISA;
	kp->input_joystick->phys = "input/joystick";
	kp->input_joystick->dev.parent = &pdev->dev;
	kp->input_joystick->id.vendor = 0x0001;
	kp->input_joystick->id.product = 0x0001;
	kp->input_joystick->id.version = 0x100;
	kp->input_joystick->rep[REP_DELAY]=0xffffffff;
	kp->input_joystick->rep[REP_PERIOD]=0xffffffff;
	kp->input_joystick->keycodesize = sizeof(unsigned short);
	kp->input_joystick->keycodemax = 0x1ff;
	ret = input_register_device(kp->input_joystick);
	if (ret < 0) {
		printk(KERN_ERR "register input_joystick device fail\n");
		kfree(kp);
		input_free_device(kp->input_joystick);
		return -EINVAL;
	}

	platform_set_drvdata(pdev, kp);


	//struct device *dev = &pdev->dev;

	INIT_WORK(&(kp->work_update), update_work_func);
	setup_timer(&kp->timer, kp_timer_sr, (unsigned long)kp) ;
	mod_timer(&kp->timer, jiffies+msecs_to_jiffies(100));
#ifdef CONFIG_HAS_EARLYSUSPEND
	kp->joystick_early_suspend.suspend = joystick_suspend;
	kp->joystick_early_suspend.resume = joystick_resume;
	kp->joystick_early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;;
	register_early_suspend(&kp->joystick_early_suspend);
#endif

	return 0;
}

static int joystick_remove(struct platform_device *pdev)
{
	struct kp *kp = platform_get_drvdata(pdev);

	input_unregister_device(kp->input_joystick);
	input_free_device(kp->input_joystick);

#ifdef CONFIG_HAS_EARLYSUSPEND
    unregister_early_suspend(&kp->joystick_early_suspend);
#endif 
	kfree(kp);
	return 0;
}

static struct platform_driver joystick_driver = {
	.probe      = joystick_probe,
	.remove     = __devexit_p(joystick_remove),
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend    = joystick_suspend,
	.resume     = joystick_resume,
#endif
	.driver     = {
                .owner  = THIS_MODULE,
		.name = "usb-joystick",
	},
};

static struct platform_device joystick_device = {
	.name = "usb-joystick",
	.id = 0,
};

static int __devinit joystick_init(void)
{
	printk(KERN_INFO "USB joystick Driver init.\n");
	platform_device_register(&joystick_device);
	return platform_driver_register(&joystick_driver);
}

static void __exit joystick_exit(void)
{
	printk(KERN_INFO "USB joystick Driver exit.\n");
	platform_device_unregister(&joystick_device);
	platform_driver_unregister(&joystick_driver);

}

module_init(joystick_init);
//late_initcall(joystick_init);
module_exit(joystick_exit);

MODULE_AUTHOR("Liudk");
MODULE_DESCRIPTION("Joystick Driver");
MODULE_LICENSE("GPL");
