/*
 * linux/drivers/input/keyboard/pxa27x_keypad.c
 *
 * Driver for the pxa27x matrix keyboard controller.
 *
 * Created:	Feb 22, 2007
 * Author:	Rodolfo Giometti <giometti@linux.it>
 *
 * Based on a previous implementations by Kevin O'Connor
 * <kevin_at_koconnor.net> and Alex Osborne <bobofdoom@gmail.com> and
 * on some suggestions by Nicolas Pitre <nico@fluxnic.net>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/input/matrix_keypad.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/workqueue.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/hardware.h>
#include <plat/pxa27x_keypad.h>
#include <mach/gpio-edge.h>
#include <plat/mfp.h> 

#ifdef CONFIG_FAKE_SYSTEMOFF
#include <linux/power/fake-sysoff.h>
#endif

/*
 * Keypad Controller registers
 */
#define KPC             0x0000 /* Keypad Control register */
#define KPDK            0x0008 /* Keypad Direct Key register */
#define KPREC           0x0010 /* Keypad Rotary Encoder register */
#define KPMK            0x0018 /* Keypad Matrix Key register */
#define KPAS            0x0020 /* Keypad Automatic Scan register */

/* Keypad Automatic Scan Multiple Key Presser register 0-3 */
#define KPASMKP0        0x0028
#define KPASMKP1        0x0030
#define KPASMKP2        0x0038
#define KPASMKP3        0x0040
#define KPKDI           0x0048

/* bit definitions */
#define KPC_MKRN(n)	((((n) - 1) & 0x7) << 26) /* matrix key row number */
#define KPC_MKCN(n)	((((n) - 1) & 0x7) << 23) /* matrix key column number */
#define KPC_DKN(n)	((((n) - 1) & 0x7) << 6)  /* direct key number */

#define KPC_AS          (0x1 << 30)  /* Automatic Scan bit */
#define KPC_ASACT       (0x1 << 29)  /* Automatic Scan on Activity */
#define KPC_MI          (0x1 << 22)  /* Matrix interrupt bit */
#define KPC_IMKP        (0x1 << 21)  /* Ignore Multiple Key Press */

#define KPC_MS(n)	(0x1 << (13 + (n)))	/* Matrix scan line 'n' */
#define KPC_MS_ALL      (0xff << 13)

#define KPC_ME          (0x1 << 12)  /* Matrix Keypad Enable */
#define KPC_MIE         (0x1 << 11)  /* Matrix Interrupt Enable */
#define KPC_DK_DEB_SEL	(0x1 <<  9)  /* Direct Keypad Debounce Select */
#define KPC_DI          (0x1 <<  5)  /* Direct key interrupt bit */
#define KPC_RE_ZERO_DEB (0x1 <<  4)  /* Rotary Encoder Zero Debounce */
#define KPC_REE1        (0x1 <<  3)  /* Rotary Encoder1 Enable */
#define KPC_REE0        (0x1 <<  2)  /* Rotary Encoder0 Enable */
#define KPC_DE          (0x1 <<  1)  /* Direct Keypad Enable */
#define KPC_DIE         (0x1 <<  0)  /* Direct Keypad interrupt Enable */

#define KPDK_DKP        (0x1 << 31)
#define KPDK_DK(n)	((n) & 0xff)

#define KPREC_OF1       (0x1 << 31)
#define kPREC_UF1       (0x1 << 30)
#define KPREC_OF0       (0x1 << 15)
#define KPREC_UF0       (0x1 << 14)

#define KPREC_RECOUNT0(n)	((n) & 0xff)
#define KPREC_RECOUNT1(n)	(((n) >> 16) & 0xff)

#define KPMK_MKP        (0x1 << 31)
#define KPAS_SO         (0x1 << 31)
#define KPASMKPx_SO     (0x1 << 31)

#define KPAS_MUKP(n)	(((n) >> 26) & 0x1f)
#define KPAS_RP(n)	(((n) >> 4) & 0xf)
#define KPAS_CP(n)	((n) & 0xf)

#define KPASMKP_MKC_MASK	(0xff)

#define keypad_readl(off)	__raw_readl(keypad->mmio_base + (off))
#define keypad_writel(off, v)	__raw_writel((v), keypad->mmio_base + (off))

#define MAX_MATRIX_KEY_NUM	(MAX_MATRIX_KEY_ROWS * MAX_MATRIX_KEY_COLS)
#define MAX_KEYPAD_KEYS		(MAX_MATRIX_KEY_NUM + MAX_DIRECT_KEY_NUM)

#ifdef CONFIG_KERNEL_DEBUG_SEC
int is_volumeUp_pressed;	
#endif
extern struct class *sec_class;
static int is_key_pressed;
static int sec_debug_mode = 0;

struct pxa27x_keypad {
	struct pxa27x_keypad_platform_data *pdata;

	struct clk *clk;
	struct input_dev *input_dev;
	void __iomem *mmio_base;

	int irq;

	unsigned short keycodes[MAX_KEYPAD_KEYS];
	int rotary_rel_code[2];

	/* state row bits of each column scan */
	uint32_t matrix_key_state[MAX_MATRIX_KEY_COLS];
	uint32_t direct_key_state;

	unsigned int direct_key_mask;
#ifdef CONFIG_CPU_PXA988
	struct work_struct	keypad_lpm_work;
	struct workqueue_struct	*keypad_lpm_wq;
	struct gpio_edge_desc	*gpio_wakeup[MAX_DIRECT_KEY_NUM];
	struct delayed_work	input_work;
#endif
};

static struct pxa27x_keypad *g_pxa27x_keypad;
static void pxa27x_keypad_build_keycode(struct pxa27x_keypad *keypad)
{
	struct pxa27x_keypad_platform_data *pdata = keypad->pdata;
	struct input_dev *input_dev = keypad->input_dev;
	unsigned short keycode;
	int i;

	for (i = 0; i < pdata->matrix_key_map_size; i++) {
		unsigned int key = pdata->matrix_key_map[i];
		unsigned int row = KEY_ROW(key);
		unsigned int col = KEY_COL(key);
		unsigned int scancode = MATRIX_SCAN_CODE(row, col,
							 MATRIX_ROW_SHIFT);

		keycode = KEY_VAL(key);
		keypad->keycodes[scancode] = keycode;
		__set_bit(keycode, input_dev->keybit);
	}

	for (i = 0; i < pdata->direct_key_num; i++) {
		keycode = pdata->direct_key_map[i];
		keypad->keycodes[MAX_MATRIX_KEY_NUM + i] = keycode;
		__set_bit(keycode, input_dev->keybit);
	}

	if (pdata->enable_rotary0) {
		if (pdata->rotary0_up_key && pdata->rotary0_down_key) {
			keycode = pdata->rotary0_up_key;
			keypad->keycodes[MAX_MATRIX_KEY_NUM + 0] = keycode;
			__set_bit(keycode, input_dev->keybit);

			keycode = pdata->rotary0_down_key;
			keypad->keycodes[MAX_MATRIX_KEY_NUM + 1] = keycode;
			__set_bit(keycode, input_dev->keybit);

			keypad->rotary_rel_code[0] = -1;
		} else {
			keypad->rotary_rel_code[0] = pdata->rotary0_rel_code;
			__set_bit(pdata->rotary0_rel_code, input_dev->relbit);
		}
	}

	if (pdata->enable_rotary1) {
		if (pdata->rotary1_up_key && pdata->rotary1_down_key) {
			keycode = pdata->rotary1_up_key;
			keypad->keycodes[MAX_MATRIX_KEY_NUM + 2] = keycode;
			__set_bit(keycode, input_dev->keybit);

			keycode = pdata->rotary1_down_key;
			keypad->keycodes[MAX_MATRIX_KEY_NUM + 3] = keycode;
			__set_bit(keycode, input_dev->keybit);

			keypad->rotary_rel_code[1] = -1;
		} else {
			keypad->rotary_rel_code[1] = pdata->rotary1_rel_code;
			__set_bit(pdata->rotary1_rel_code, input_dev->relbit);
		}
	}

	__clear_bit(KEY_RESERVED, input_dev->keybit);
}

static void pxa27x_keypad_scan_matrix(struct pxa27x_keypad *keypad)
{
	struct pxa27x_keypad_platform_data *pdata = keypad->pdata;
	struct input_dev *input_dev = keypad->input_dev;
	int row, col, num_keys_pressed;
	uint32_t new_state[MAX_MATRIX_KEY_COLS];
	uint32_t kpas = keypad_readl(KPAS);

	num_keys_pressed = KPAS_MUKP(kpas);

	memset(new_state, 0, sizeof(new_state));

	if (num_keys_pressed == 0)
		goto scan;
	/* do not scan more than three keys to avoid fake key */
	if (num_keys_pressed > 3)
		return;
	if (num_keys_pressed == 1) {
		col = KPAS_CP(kpas);
		row = KPAS_RP(kpas);

		/* if invalid row/col, treat as no key pressed */
		if (col >= pdata->matrix_key_cols ||
		    row >= pdata->matrix_key_rows)
			goto scan;

		new_state[col] = (1 << row);
		goto scan;
	}

	if (num_keys_pressed > 1) {
		uint32_t kpasmkp0 = keypad_readl(KPASMKP0);
		uint32_t kpasmkp1 = keypad_readl(KPASMKP1);
		uint32_t kpasmkp2 = keypad_readl(KPASMKP2);
		uint32_t kpasmkp3 = keypad_readl(KPASMKP3);

		new_state[0] = kpasmkp0 & KPASMKP_MKC_MASK;
		new_state[1] = (kpasmkp0 >> 16) & KPASMKP_MKC_MASK;
		new_state[2] = kpasmkp1 & KPASMKP_MKC_MASK;
		new_state[3] = (kpasmkp1 >> 16) & KPASMKP_MKC_MASK;
		new_state[4] = kpasmkp2 & KPASMKP_MKC_MASK;
		new_state[5] = (kpasmkp2 >> 16) & KPASMKP_MKC_MASK;
		new_state[6] = kpasmkp3 & KPASMKP_MKC_MASK;
		new_state[7] = (kpasmkp3 >> 16) & KPASMKP_MKC_MASK;
	}
scan:
	for (col = 0; col < pdata->matrix_key_cols; col++) {
		uint32_t bits_changed;
		int code;

		bits_changed = keypad->matrix_key_state[col] ^ new_state[col];
		if (bits_changed == 0)
			continue;

		for (row = 0; row < pdata->matrix_key_rows; row++) {
			if ((bits_changed & (1 << row)) == 0)
				continue;
			is_key_pressed = new_state[col] & (1 << row); // AT + keyshort cmd
	//		printk("new_state = %d, last bits_changed = %d\n",new_state[col], bits_changed);
			code = MATRIX_SCAN_CODE(row, col, MATRIX_ROW_SHIFT);
			input_event(input_dev, EV_MSC, MSC_SCAN, code);
			input_report_key(input_dev, keypad->keycodes[code],
					 new_state[col] & (1 << row));
#ifdef CONFIG_KERNEL_DEBUG_SEC
 			 if (keypad->keycodes[code] == KEY_VOLUMEUP)
  			{
				is_volumeUp_pressed = new_state[col] & (1<<row);
//				printk("%s is_volumeUp_pressed( %d ) \n",__func__,is_volumeUp_pressed);
  			}
#endif 			
		}
	}
	input_sync(input_dev);
	memcpy(keypad->matrix_key_state, new_state, sizeof(new_state));
}

#define DEFAULT_KPREC	(0x007f007f)

static inline int rotary_delta(uint32_t kprec)
{
	if (kprec & KPREC_OF0)
		return (kprec & 0xff) + 0x7f;
	else if (kprec & KPREC_UF0)
		return (kprec & 0xff) - 0x7f - 0xff;
	else
		return (kprec & 0xff) - 0x7f;
}

static void report_rotary_event(struct pxa27x_keypad *keypad, int r, int delta)
{
	struct input_dev *dev = keypad->input_dev;

	if (delta == 0)
		return;

	if (keypad->rotary_rel_code[r] == -1) {
		int code = MAX_MATRIX_KEY_NUM + 2 * r + (delta > 0 ? 0 : 1);
		unsigned char keycode = keypad->keycodes[code];

		/* simulate a press-n-release */
		input_event(dev, EV_MSC, MSC_SCAN, code);
		input_report_key(dev, keycode, 1);
		input_sync(dev);
		input_event(dev, EV_MSC, MSC_SCAN, code);
		input_report_key(dev, keycode, 0);
		input_sync(dev);
	} else {
		input_report_rel(dev, keypad->rotary_rel_code[r], delta);
		input_sync(dev);
	}
}

static void pxa27x_keypad_scan_rotary(struct pxa27x_keypad *keypad)
{
	struct pxa27x_keypad_platform_data *pdata = keypad->pdata;
	uint32_t kprec;

	/* read and reset to default count value */
	kprec = keypad_readl(KPREC);
	keypad_writel(KPREC, DEFAULT_KPREC);

	if (pdata->enable_rotary0)
		report_rotary_event(keypad, 0, rotary_delta(kprec));

	if (pdata->enable_rotary1)
		report_rotary_event(keypad, 1, rotary_delta(kprec >> 16));
}

static int get_sec_debug_mode(char *str)
{
	get_option(&str, &sec_debug_mode);
	printk("get_sec_debug_mode, sec_debug_mode : %d\n", sec_debug_mode);
	return 1;
}

int sec_debug_mode_get()
{
	return sec_debug_mode;
}

__setup("androidboot.debug_level=",get_sec_debug_mode);

static void pxa27x_keypad_scan_direct(struct pxa27x_keypad *keypad)
{
	struct pxa27x_keypad_platform_data *pdata = keypad->pdata;
	struct input_dev *input_dev = keypad->input_dev;
	unsigned int new_state;
	uint32_t kpdk, bits_changed;
	int i;

	kpdk = keypad_readl(KPDK);

	if (pdata->enable_rotary0 || pdata->enable_rotary1)
		pxa27x_keypad_scan_rotary(keypad);

	/*
	 * The KPDR_DK only output the key pin level, so it relates to board,
	 * and low level may be active.
	 */
	if (pdata->direct_key_low_active)
		new_state = ~KPDK_DK(kpdk) & keypad->direct_key_mask;
	else
		new_state = KPDK_DK(kpdk) & keypad->direct_key_mask;
	
	bits_changed = keypad->direct_key_state ^ new_state;
//	printk("kpdk=%x, KPDK_DK(kpdk) = %x, keypad->direct_key_mask is %x  keypad->direct_key_state is  %x\n",kpdk, KPDK_DK(kpdk),keypad->direct_key_mask,keypad->direct_key_state);
#ifdef CONFIG_KERNEL_DEBUG_SEC
	if( (bits_changed ==0x20) && is_volumeUp_pressed)
	{
		if(sec_debug_mode_get()== 0x494d)
		{
			printk("%s, kernel panic!!!!!!!!!!!!!!!!!!!!!!!!!!\n", __func__);  
			panic("__forced_upload");
		}
	}
#endif

	if (bits_changed == 0)
		return;

	for (i = 0; i < pdata->direct_key_num; i++) {
		if (bits_changed & (1 << i)) {
			int code = MAX_MATRIX_KEY_NUM + i;
			is_key_pressed = new_state & (1 << i); // AT + keyshort cmd
//			printk("new_state = %d, new_state & (1 << i) = %d, last bits_changed = %d\n",new_state, new_state & (1 << i),bits_changed);
			input_event(input_dev, EV_MSC, MSC_SCAN, code);
			input_report_key(input_dev, keypad->keycodes[code],
					 new_state & (1 << i));
		}
	}
	input_sync(input_dev);
	keypad->direct_key_state = new_state;
}

static void clear_wakeup_event(struct pxa27x_keypad *keypad)
{
	struct pxa27x_keypad_platform_data *pdata = keypad->pdata;

	if (pdata->clear_wakeup_event)
		(pdata->clear_wakeup_event)();
}

static irqreturn_t pxa27x_keypad_irq_handler(int irq, void *dev_id)
{
	struct pxa27x_keypad *keypad = dev_id;
	unsigned long kpc = keypad_readl(KPC);
	clear_wakeup_event(keypad);
#ifdef CONFIG_FAKE_SYSTEMOFF
	/*
	 * fake_sysoff_block_onkey block the slot while the state machine
	 * is sysprepare;
	 * fake_sysoff_status_query block the slot while the state machine
	 * is sysoff, that's the same meaning of fake off status;
	 * The above requirements both come from Android level.
	 */
	if (fake_sysoff_block_onkey() || fake_sysoff_status_query())
		return IRQ_HANDLED;
#endif
#ifdef CONFIG_CPU_PXA988
	kpc = kpc;
	queue_work(keypad->keypad_lpm_wq, &keypad->keypad_lpm_work);
#else
	if (kpc & KPC_DI)
		pxa27x_keypad_scan_direct(keypad);

	if (kpc & KPC_MI)
		pxa27x_keypad_scan_matrix(keypad);

#endif
	return IRQ_HANDLED;
}

static void pxa27x_keypad_config(struct pxa27x_keypad *keypad)
{
	struct pxa27x_keypad_platform_data *pdata = keypad->pdata;
	unsigned int mask = 0, direct_key_num = 0;
	unsigned long kpc = 0;

	/* enable matrix keys with automatic scan */
	if (pdata->matrix_key_rows && pdata->matrix_key_cols) {
		kpc |= KPC_ASACT | KPC_MIE | KPC_ME | KPC_MS_ALL;
		kpc |= KPC_MKRN(pdata->matrix_key_rows) |
		       KPC_MKCN(pdata->matrix_key_cols);
	}

	/* enable rotary key, debounce interval same as direct keys */
	if (pdata->enable_rotary0) {
		mask |= 0x03;
		direct_key_num = 2;
		kpc |= KPC_REE0;
	}

	if (pdata->enable_rotary1) {
		mask |= 0x0c;
		direct_key_num = 4;
		kpc |= KPC_REE1;
	}

	if (pdata->direct_key_num > direct_key_num)
		direct_key_num = pdata->direct_key_num;

	/*
	 * Direct keys usage may not start from KP_DKIN0, check the platfrom
	 * mask data to config the specific.
	 */
	if (pdata->direct_key_mask)
		keypad->direct_key_mask = pdata->direct_key_mask;
	else
		keypad->direct_key_mask = ((1 << direct_key_num) - 1) & ~mask;

	/* enable direct key */
	if (direct_key_num)
		kpc |= KPC_DE | KPC_DIE | KPC_DKN(direct_key_num);

	keypad_writel(KPC, kpc | KPC_RE_ZERO_DEB);
	keypad_writel(KPREC, DEFAULT_KPREC);
	keypad_writel(KPKDI, pdata->debounce_interval);
}

static int pxa27x_keypad_open(struct input_dev *dev)
{
	struct pxa27x_keypad *keypad = input_get_drvdata(dev);

	/* Enable unit clock */
	clk_prepare_enable(keypad->clk);
	pxa27x_keypad_config(keypad);

	return 0;
}

static void pxa27x_keypad_close(struct input_dev *dev)
{
	struct pxa27x_keypad *keypad = input_get_drvdata(dev);

	/* Disable clock unit */
	clk_disable_unprepare(keypad->clk);
}

static void pxa27x_keypad_wq_func(struct work_struct *work)
{
	struct pxa27x_keypad *keypad = g_pxa27x_keypad;
	u32 kpc;
	int retry = 5;

	keypad_writel(KPC, keypad_readl(KPC) | KPC_AS);
	do {
		mdelay(2);
		kpc = keypad_readl(KPC);
		retry--;
	} while ((kpc & KPC_AS) && retry);

	if (!retry)
		keypad_writel(KPC, kpc & ~KPC_AS);

	if (kpc & KPC_ME)
		pxa27x_keypad_scan_matrix(keypad);

	if (kpc & KPC_DE)
		pxa27x_keypad_scan_direct(keypad);
}

#ifdef CONFIG_CPU_PXA988
static int direct_mfp;

static void pxa27x_keypad_delay_wq(struct work_struct *work)
{
	struct pxa27x_keypad *keypad = g_pxa27x_keypad;
	struct input_dev *input_dev = keypad->input_dev;
	int i;
	for (i = 0; i < MAX_DIRECT_KEY_NUM; i++) {
		if (keypad->pdata->direct_wakeup_pad[i] == direct_mfp) {
			int code = MAX_MATRIX_KEY_NUM + i;
			input_event(input_dev, EV_MSC, MSC_SCAN, code);
			clear_bit(keypad->keycodes[code],input_dev->key);
			input_report_key(input_dev, keypad->keycodes[code], 1);
			input_report_key(input_dev, keypad->keycodes[code], 0);
			input_sync(input_dev);
		}
	}
	return;
}

void trigger_keypad_wakeup(int mfp, void *data)
{
	struct pxa27x_keypad *keypad = g_pxa27x_keypad;
	direct_mfp = mfp;
	schedule_delayed_work(&keypad->input_work,
				msecs_to_jiffies(10));
	return;
}
#endif
#ifdef CONFIG_PM
static int pxa27x_keypad_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pxa27x_keypad *keypad = platform_get_drvdata(pdev);
#ifdef CONFIG_CPU_PXA988
	u8 i;
#endif
	/*
	 * If the keypad is used a wake up source, clock can not be disabled.
	 * Or it can not detect the key pressing.
	 */
	if (device_may_wakeup(&pdev->dev))
		enable_irq_wake(keypad->irq);
	else {
		clk_disable_unprepare(keypad->clk);
#ifdef CONFIG_CPU_PXA988
		for (i = 0; i < MAX_DIRECT_KEY_NUM; i++) {
			if (keypad->gpio_wakeup[i])
				mmp_gpio_edge_add(keypad->gpio_wakeup[i]);
		}
#endif
	}
	return 0;
}

static int pxa27x_keypad_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pxa27x_keypad *keypad = platform_get_drvdata(pdev);
	struct input_dev *input_dev = keypad->input_dev;
#ifdef CONFIG_CPU_PXA988
	u8 i;
#endif
	/*
	 * If the keypad is used as wake up source, the clock is not turned
	 * off. So do not need configure it again.
	 */
	if (device_may_wakeup(&pdev->dev)) {
		disable_irq_wake(keypad->irq);
	} else {
		mutex_lock(&input_dev->mutex);

		if (input_dev->users) {
			/* Enable unit clock */
			clk_prepare_enable(keypad->clk);
			pxa27x_keypad_config(keypad);
#ifdef CONFIG_CPU_PXA988
			for (i = 0; i < MAX_DIRECT_KEY_NUM; i++) {
				if (keypad->gpio_wakeup[i])
					mmp_gpio_edge_del(
						keypad->gpio_wakeup[i]);
			}
#endif
		}

		mutex_unlock(&input_dev->mutex);
	}
	return 0;
}

static const struct dev_pm_ops pxa27x_keypad_pm_ops = {
	.suspend	= pxa27x_keypad_suspend,
	.resume		= pxa27x_keypad_resume,
};
#endif

// AT + KEYSHORT cmd
static ssize_t keys_read(struct device *dev, struct device_attribute *attr, char *buf)
{
	int count;

//	printk("is_key_pressed = %d\n",is_key_pressed);
	if((is_key_pressed != 0) && is_key_pressed != 16) 
	{	
		count = sprintf(buf,"%s\n","PRESS");
	}
	else
	{
		count = sprintf(buf,"%s\n","RELEASE");
	}

       return count;
}
static DEVICE_ATTR(sec_key_pressed, S_IRUGO, keys_read, NULL);
static int __devinit pxa27x_keypad_probe(struct platform_device *pdev)
{
	struct pxa27x_keypad_platform_data *pdata = pdev->dev.platform_data;
	struct pxa27x_keypad *keypad;
	struct input_dev *input_dev;
	struct resource *res;
	int irq, error;
#ifdef CONFIG_CPU_PXA988
	u8 i;
	struct gpio_edge_desc *c;
#endif

	if (pdata == NULL) {
		dev_err(&pdev->dev, "no platform data defined\n");
		return -EINVAL;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "failed to get keypad irq\n");
		return -ENXIO;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "failed to get I/O memory\n");
		return -ENXIO;
	}

	keypad = kzalloc(sizeof(struct pxa27x_keypad), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!keypad || !input_dev) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		error = -ENOMEM;
		goto failed_free;
	}

	keypad->pdata = pdata;
	keypad->input_dev = input_dev;
	keypad->irq = irq;

	res = request_mem_region(res->start, resource_size(res), pdev->name);
	if (res == NULL) {
		dev_err(&pdev->dev, "failed to request I/O memory\n");
		error = -EBUSY;
		goto failed_free;
	}

	keypad->mmio_base = ioremap(res->start, resource_size(res));
	if (keypad->mmio_base == NULL) {
		dev_err(&pdev->dev, "failed to remap I/O memory\n");
		error = -ENXIO;
		goto failed_free_mem;
	}

	keypad->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(keypad->clk)) {
		dev_err(&pdev->dev, "failed to get keypad clock\n");
		error = PTR_ERR(keypad->clk);
		goto failed_free_io;
	}

	input_dev->name = pdev->name;
	input_dev->id.bustype = BUS_HOST;
	input_dev->open = pxa27x_keypad_open;
	input_dev->close = pxa27x_keypad_close;
	input_dev->dev.parent = &pdev->dev;

	input_dev->keycode = keypad->keycodes;
	input_dev->keycodesize = sizeof(keypad->keycodes[0]);
	input_dev->keycodemax = ARRAY_SIZE(keypad->keycodes);

	input_set_drvdata(input_dev, keypad);

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS)
		| BIT_MASK(EV_ABS);
	input_set_capability(input_dev, EV_MSC, MSC_SCAN);

	pxa27x_keypad_build_keycode(keypad);

	if ((pdata->enable_rotary0 && keypad->rotary_rel_code[0] != -1) ||
	    (pdata->enable_rotary1 && keypad->rotary_rel_code[1] != -1)) {
		input_dev->evbit[0] |= BIT_MASK(EV_REL);
	}

	/* Register the input device */
	error = input_register_device(input_dev);
	if (error) {
		dev_err(&pdev->dev, "failed to register input device\n");
		goto failed_put_clk;
	}
#ifdef CONFIG_CPU_PXA988
	INIT_WORK(&keypad->keypad_lpm_work, pxa27x_keypad_wq_func);
	keypad->keypad_lpm_wq = create_workqueue("keypad_rx_lpm_wq");
	if (unlikely(keypad->keypad_lpm_wq == NULL))
		pr_info("[keypad] warning: create work queue failed\n");

	INIT_DELAYED_WORK(&keypad->input_work, pxa27x_keypad_delay_wq);
	for (i = 0; i < MAX_DIRECT_KEY_NUM; i++) {
		if (pdata->direct_wakeup_pad[i]) {
			c = kzalloc(sizeof(struct gpio_edge_desc), GFP_KERNEL);
			c->mfp = pdata->direct_wakeup_pad[i];
			c->handler = trigger_keypad_wakeup;
			c->type = MFP_LPM_EDGE_BOTH; 
			keypad->gpio_wakeup[i] = c;
		}
	}
#endif

	g_pxa27x_keypad = keypad;
	platform_set_drvdata(pdev, keypad);
	device_set_wakeup_capable(&pdev->dev, 1);
	error = request_irq(irq, pxa27x_keypad_irq_handler, 0,
			    pdev->name, keypad);
	if (error) {
		dev_err(&pdev->dev, "failed to request IRQ\n");
		goto failed_input_device;
	}
#if 1
	{struct device *dev_t;
	
	dev_t = device_create(sec_class, NULL, 0, "%s", "sec_key");
	
	if(device_create_file(dev_t, &dev_attr_sec_key_pressed) < 0)
		 printk("Failed to create device file(%s)!\n", dev_attr_sec_key_pressed.attr.name);
	}
#endif
	return 0;

failed_input_device:
	input_unregister_device(keypad->input_dev);
failed_put_clk:
	clk_put(keypad->clk);
failed_free_io:
	iounmap(keypad->mmio_base);
failed_free_mem:
	release_mem_region(res->start, resource_size(res));
failed_free:
	input_free_device(input_dev);
	kfree(keypad);
	return error;
}

static int __devexit pxa27x_keypad_remove(struct platform_device *pdev)
{
	struct pxa27x_keypad *keypad = platform_get_drvdata(pdev);
	struct resource *res;
#ifdef CONFIG_CPU_PXA988
	u8 i;
#endif
	free_irq(keypad->irq, pdev);
	clk_put(keypad->clk);

	input_unregister_device(keypad->input_dev);
	iounmap(keypad->mmio_base);

#ifdef CONFIG_CPU_PXA988
	for (i = 0; i < MAX_DIRECT_KEY_NUM; i++) {
		if (keypad->gpio_wakeup[i]) {
			mmp_gpio_edge_del(keypad->gpio_wakeup[i]);
			kfree(keypad->gpio_wakeup[i]);
			keypad->gpio_wakeup[i] = NULL;
		}
	}
#endif
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, resource_size(res));

	platform_set_drvdata(pdev, NULL);
	kfree(keypad);

	return 0;
}

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:pxa27x-keypad");

static struct platform_driver pxa27x_keypad_driver = {
	.probe		= pxa27x_keypad_probe,
	.remove		= __devexit_p(pxa27x_keypad_remove),
	.driver		= {
		.name	= "pxa27x-keypad",
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
		.pm	= &pxa27x_keypad_pm_ops,
#endif
	},
};
module_platform_driver(pxa27x_keypad_driver);

MODULE_DESCRIPTION("PXA27x Keypad Controller Driver");
MODULE_LICENSE("GPL");
