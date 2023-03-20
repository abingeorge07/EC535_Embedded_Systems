// Name: Justin Sadler, Abin George 
// Date: 17-03-2023 
/*
Sources:
	https://perma.cc/4GWJ-A7C7
	https://www.kernel.org/doc/html/v4.19/driver-api/gpio/consumer.html
	https://www.kernel.org/doc/html/v4.19/driver-api/gpio/legacy.html
	Linux Device Drivers 3rd Edition: Chapter 10 (Interrupt Handling)
	https://embetronicx.com/tutorials/linux/device-drivers/linux-device-driver-tutorial-part-13-interrupt-example-program-in-linux-kernel/
	https://www.kernel.org/doc/html/v4.19/core-api/kernel-api.html#c.free_irq

 */


/* Necessary includes for device drivers */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h> /* printk() */
#include <linux/slab.h> /* kmalloc() */
#include <linux/fs.h> /* everything... */
#include <linux/errno.h> /* error codes */
#include <linux/types.h> /* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h> /* O_ACCMODE */
#include <asm/system_misc.h> /* cli(), *_flags */
#include <linux/uaccess.h>
#include <asm/uaccess.h> /* copy_from/to_user */
#include <linux/gpio.h> // legacy GPIO interface
#include <linux/gpio/consumer.h> // Gpio consumer interface
#include <linux/interrupt.h>


// GPIO Numbers
#define RED_LED  67 
#define YELLOW_LED 68 
#define GREEN_LED  44 
#define BTN0  26
#define BTN1  46 

#define DEBUG 1


MODULE_AUTHOR("Abin George, Justin Sadler");
MODULE_DESCRIPTION("Traffic Light Driver");
MODULE_LICENSE("GPL");

/* Declaration of mytraffic.c functions */
static int mytraffic_open(struct inode *inode, struct file *filp);
static int mytraffic_release(struct inode *inode, struct file *filp);
static ssize_t mytraffic_read(struct file *filp,
		char *buf, size_t count, loff_t *f_pos);
static ssize_t mytraffic_write(struct file *filp,
		const char *buf, size_t count, loff_t *f_pos);
static void mytraffic_exit(void);
static int mytraffic_init(void);

static irqreturn_t btn0_handler(int irq, void * dev_id);
static irqreturn_t btn1_handler(int irq, void * dev_id);

/* Structure that declares the usual file */
/* access functions */
struct file_operations mytraffic_fops = {
	read: mytraffic_read,
	write: mytraffic_write,
	open: mytraffic_open,
	release: mytraffic_release
};

// Operation mode enum
enum OperationalMode {
	NORMAL,
	FLASHING_RED,
	FLASHING_YELLOW
};


/* Declaration of the init and exit functions */
module_init(mytraffic_init);
module_exit(mytraffic_exit);

/* Global variables of the driver */
/* Major number */
static int mytraffic_major = 61;
// Mode of traffic light
static enum OperationalMode mode;
// 
static int pedestrian_called = 0;

// BTN0 irq
static int btn0_irq;
// BTN1 irq
static int btn1_irq;

// GPIO Array
static struct gpio gpios[] = {
	{RED_LED,	 	GPIOF_OUT_INIT_LOW,	"Red LED" }, /* default to OFF */
	{YELLOW_LED, 	GPIOF_OUT_INIT_LOW, "Yellow LED" }, /* default to OFF */
	{GREEN_LED, 	GPIOF_OUT_INIT_LOW, "Green LED"   }, /* default to OFF */
	{BTN0, 		GPIOF_DIR_IN,  		"BTN0"  }, 
	{BTN1, 		GPIOF_DIR_IN,  		"BTN1"  }
};


static int mytraffic_init(void)
{
	int result;
	int err;


	/* Registering device */
	result = register_chrdev(mytraffic_major, "mytraffic", &mytraffic_fops);
	if (result < 0)
	{
		printk(KERN_ALERT
			"mytraffic: cannot obtain major number %d\n", mytraffic_major);
		return result;
	}

	// Request GPIO lines
	err = gpio_request_array(gpios, ARRAY_SIZE(gpios));
	if(err) {
		printk(KERN_ALERT "Could not request GPIOs\n"); 
		goto fail; 
	}

	// Map the GPIOs to Interrupt Request numbers
	btn0_irq = gpio_to_irq(BTN0);
	if(btn0_irq < 0) {
		printk(KERN_ALERT "BTN0 can't be mapped to an interrupt line\n");
		goto fail;
	}

	btn1_irq = gpio_to_irq(BTN1);
	if(btn1_irq < 0) {
		printk(KERN_ALERT "BTN1 can't be mapped to an interrupt line\n");
		goto fail;
	}

	
	// Install interrupt handlers
	err = request_irq(btn0_irq, btn0_handler, 
			IRQF_TRIGGER_RISING, "btn0", NULL);

	if(err) {
		printk(KERN_ALERT "Couldn't install interrupt handler for BTN0\n");
		goto fail;
	}

	err = request_irq(btn1_irq, btn1_handler, 
			IRQF_TRIGGER_RISING, "btn1", NULL);

	if(err) {
		printk(KERN_ALERT "Couldn't install interrupt handler for BTN1\n");
		goto fail;
	}

	printk(KERN_ALERT "Inserting mytraffic module\n"); 
	return 0;

fail: 
	mytraffic_exit(); 
	return result;
}

static void mytraffic_exit(void)
{
	/* Freeing the major number */
	unregister_chrdev(mytraffic_major, "mytraffic");

	free_irq(btn0_irq, NULL);
	gpio_set_value(RED_LED, 0);
	gpio_set_value(GREEN_LED, 0);
	gpio_set_value(YELLOW_LED, 0);
	gpio_free_array(gpios, ARRAY_SIZE(gpios));

	printk(KERN_ALERT "Removing mytraffic module\n");

}

static int mytraffic_open(struct inode *inode, struct file *filp)
{

	printk(KERN_DEBUG "open called: process id %d, command %s\n",
		current->pid, current->comm);
	/* Success */
	return 0;
}

static int mytraffic_release(struct inode *inode, struct file *filp)
{
	printk(KERN_DEBUG "release called: process id %d, command %s\n",
		current->pid, current->comm);
	/* Success */
	return 0;
}

static ssize_t mytraffic_read(struct file *filp, char *buf, 
							size_t count, loff_t *f_pos)
{
	return 0;
}

static ssize_t mytraffic_write(struct file *filp, const char *buf,
							size_t count, loff_t *f_pos)
{
	return 0;
}

static irqreturn_t btn0_handler(int irq, void * dev_id) {

#if DEBUG
	printk(KERN_INFO "Switching flashing modes\n");
#endif

	switch(mode) {
		case NORMAL:
			mode = FLASHING_RED;
			break;
		case FLASHING_RED:
			mode = FLASHING_RED;
			break;
		case FLASHING_YELLOW:
			mode = NORMAL;
			break;
	}
	return IRQ_HANDLED;
}

static irqreturn_t btn1_handler(int irq, void * dev_id) {

#if DEBUG
	printk(KERN_INFO "Pedestrian Called");
#endif
	pedestrian_called = 1;
	return IRQ_HANDLED;
}
