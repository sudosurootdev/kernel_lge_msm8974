#include <linux/module.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kthread.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/syscalls.h>
#include <linux/fcntl.h>
#include <linux/gpio.h>
#include <asm/uaccess.h>

#include <linux/jiffies.h>
#include <linux/completion.h>
#include <asm/ioctls.h>
#include <mach/moca_kernel_probe.h>

#define MODULE_NAME "moca_kernel_probe"
#define MAX_TIME_INDEX	10		//Event time log array index 
#define EVENT_NUM		6		//The number of event time for comparison
#define EVENT_PERIOD	60		//Maximum period to trigger MOCA

#define KERNEL_EVENT_NOTI_LEN 		81U
#define KERNEL_IOCTL_MAGIC			'K'
#define KERNEL_IOCTL_MAXNR			0x02
#define KERNEL_EVENT_NOTI			_IOR(KERNEL_IOCTL_MAGIC, 0x01, unsigned int)


long lge_moca_kernel_probe_ioctl(struct file *file, const unsigned int cmd, unsigned long arg);

static struct class *kernel_probe_class;

static unsigned int event_time[MAX_TIME_INDEX];		//Report jiffies time
static unsigned int top_index;						// Top index of event_time[]
moca_km_enum_type event_type;
struct completion km_ioctl_wait_completion;

bool irq_debug = false;

struct kernel_probe_context 
{
	dev_t 			dev_num;
	struct device 	*dev;
	struct cdev 	*cdev;
};

struct kernel_probe_context *gkernel_probe_context;

const struct file_operations kernel_probe_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = lge_moca_kernel_probe_ioctl,
};

long lge_moca_kernel_probe_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	
	if(_IOC_TYPE(cmd) != KERNEL_IOCTL_MAGIC) 
	{ 
		printk("magic err\n"); 
		return -EINVAL;
	} 
	
	if(_IOC_NR(cmd) >= KERNEL_IOCTL_MAXNR) 
	{
		printk("NR err\n");
		return -EINVAL;
	}


	switch(cmd)
	{
		case KERNEL_EVENT_NOTI:
			irq_debug = true;

			printk("[MOCA] %s: Wait for completion_interruptible\n",__func__);

			ret = wait_for_completion_interruptible(&km_ioctl_wait_completion);

			if(event_type == IRQ_EVENT)
			{
				irq_debug = false;
				
				if(copy_to_user((void *)arg, &event_type, sizeof(event_type)))
				{
					ret = -EFAULT;
				}
				
				memset(event_time, 0, sizeof(event_time));
				event_type = NO_EVENT;
			}
			
			init_completion(&km_ioctl_wait_completion);
			
			break;
			
		default:
			break;
				
	}

	return ret;
}

static int report_irq_time(void)
{
	unsigned int temp_top;
	unsigned int temp_tail;

	temp_top = top_index;
	temp_tail = top_index;
	event_time[top_index] = jiffies_to_msecs(jiffies)/1000;
	top_index = (top_index+1)%MAX_TIME_INDEX;
	
	if(temp_top < EVENT_NUM)
	{
		temp_tail = temp_top + MAX_TIME_INDEX - EVENT_NUM;
		if(event_time[temp_tail] == 0)		//The number of event time logs is not enough.
		{
			return 0;
		}
	}
	else
	{
		temp_tail = temp_top - EVENT_NUM;
	}
	
	if(event_time[temp_top]-event_time[temp_tail] > EVENT_PERIOD)		//Events occur sometimes.
	{
		return 0;
	}

	return 1;
}

void kernel_event_monitor(moca_km_enum_type type)
{
	if(report_irq_time() == 0)
	{
		return;
	}

	event_type = type;
	complete(&km_ioctl_wait_completion);
}

EXPORT_SYMBOL(kernel_event_monitor);

/**
 * Module Init.
 */
static int __init moca_kernel_probe_init(void)
{
	int ret;
	u32 size = (u32)sizeof(struct kernel_probe_context);

	gkernel_probe_context = (void *)kzalloc(size, GFP_KERNEL);
	if (gkernel_probe_context == NULL) {
		printk( " Context kzalloc err.\n");
		return -ENOMEM;
	}
	
	kernel_probe_class = class_create(THIS_MODULE, MODULE_NAME);

	ret = alloc_chrdev_region(&gkernel_probe_context->dev_num, 0, 1, MODULE_NAME);
	if (ret) {
		printk("alloc_chrdev_region err.\n");
		return -ENODEV;
	}

	gkernel_probe_context->dev = device_create(kernel_probe_class, NULL, gkernel_probe_context->dev_num, gkernel_probe_context, MODULE_NAME);
	if (IS_ERR(gkernel_probe_context->dev)) {
		printk("device_create err.\n");
		return -ENODEV;
	}

	gkernel_probe_context->cdev = cdev_alloc();
	if (gkernel_probe_context->cdev == NULL) {
		printk("cdev_alloc err.\n");
		return -ENODEV;
	}
	cdev_init(gkernel_probe_context->cdev, &kernel_probe_fops);
	gkernel_probe_context->cdev->owner = THIS_MODULE;

	ret = cdev_add(gkernel_probe_context->cdev, gkernel_probe_context->dev_num, 1);
	if (ret)
		printk( "cdev_add err=%d\n", -ret);
	else
		printk( "MOCA Kernel Probe module init OK!!..\n");

	top_index = 0;
	irq_debug = false;
	event_type = NO_EVENT;
	memset(event_time, 0, sizeof(event_time));
	init_completion(&km_ioctl_wait_completion);

	return ret;
}

/**
 * Module Exit.
 */
static void __exit moca_kernel_probe_exit(void)
{
	cdev_del(gkernel_probe_context->cdev);
	device_destroy(kernel_probe_class, gkernel_probe_context->dev_num);
	unregister_chrdev_region(gkernel_probe_context->dev_num, 1);

	kfree((const void*)gkernel_probe_context);

	printk( "MOCA Kernel Probe Module exit OK!!.\n");
}



module_init(moca_kernel_probe_init);
module_exit(moca_kernel_probe_exit);
