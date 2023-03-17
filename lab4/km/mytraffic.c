// Name: Justin Sadler, Abin George // Date: 17-03-2023 
/*
Sources:
	https://perma.cc/4GWJ-A7C7
	https://www.kernel.org/doc/html/v4.19/driver-api/gpio/consumer.html
	Linux Device Drivers 3rd Edition: Chapter 10 (Interrupt Handling)
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
#include <linux/gpio/consumer.h> // Gpio consumer interface

MODULE_LICENSE("Dual BSD/GPL");

// GPIO Numbers
#define RED_LED_GPIO  67 
#define YELLOW_LED_GPIO 68 
#define GREEN_LED_GPIO  44 
#define BTN0_GPIO  26
#define BTN1_GPIO  46 

#define DEBUG 1

/* Declaration of memory.c functions */
static int mytraffic_open(struct inode *inode, struct file *filp);
static int mytraffic_release(struct inode *inode, struct file *filp);
static ssize_t mytraffic_read(struct file *filp,
		char *buf, size_t count, loff_t *f_pos);
static ssize_t mytraffic_write(struct file *filp,
		const char *buf, size_t count, loff_t *f_pos);
static void mytraffic_exit(void);
static int mytraffic_init(void);

/* Structure that declares the usual file */
/* access functions */
struct file_operations mytraffic_fops = {
	read: mytraffic_read,
	write: mytraffic_write,
	open: mytraffic_open,
	release: mytraffic_release
};

/* Declaration of the init and exit functions */
module_init(mytraffic_init);
module_exit(mytraffic_exit);

/* Global variables of the driver */
/* Major number */
static int mytraffic_major = 61;

/* Buffer to store data */
static char *mytraffic_buffer;
/* length of the current message */
static int mytraffic_len;
static int capacity = 128;
static int bite = 4;

// GPIO Descriptors
struct gpio_desc 
	* RED_LED, 
	* YELLOW_LED,
	* GREEN_LED, 
	* BTN0, 
	* BTN1
; 


static int mytraffic_init(void)
{
	int result;

	/* Registering device */
	result = register_chrdev(mytraffic_major, "mytraffic", &mytraffic_fops);
	if (result < 0)
	{
		printk(KERN_ALERT
			"mytraffic: cannot obtain major number %d\n", mytraffic_major);
		return result;
	}

	/* Allocating mytraffic for the buffer */
	mytraffic_buffer = kmalloc(capacity, GFP_KERNEL); 
	if (!mytraffic_buffer)
	{ 
		printk(KERN_ALERT "Insufficient kernel memory\n"); 
		result = -ENOMEM;
		goto fail; 
	} 
	memset(mytraffic_buffer, 0, capacity);
	mytraffic_len = 0;

	// Get GPIO Descriptors
	RED_LED = gpio_to_desc(RED_LED_GPIO);
	YELLOW_LED = gpio_to_desc(YELLOW_LED_GPIO);
	GREEN_LED = gpio_to_desc(GREEN_LED_GPIO);
	BTN0 = gpio_to_desc(BTN0_GPIO);
	BTN1 = gpio_to_desc(BTN1_GPIO);

	// Set directions of LED GPIOs as output and set initial value to be 0
	result = gpiod_direction_output(RED_LED, 0); 
	if(result < 0) {
		printk(KERN_ALERT
				"mytraffic: cannot set direction of red LED\n");
	}

	result = gpiod_direction_output(YELLOW_LED, 0);
	if(result < 0) {
		printk(KERN_ALERT
				"mytraffic: cannot set direction of yellow LED\n");
	}

	result = gpiod_direction_output(GREEN_LED, 0);
	if(result < 0) {
		printk(KERN_ALERT
				"mytraffic: cannot set direction of green LED\n");
	}


	result = gpiod_direction_input(BTN0);
	if(result < 0) {
		printk(KERN_ALERT
				"mytraffic: cannot set direction of BTN0\n");
	}

	result = gpiod_direction_input(BTN1);
	if(result < 0) {
		printk(KERN_ALERT
				"mytraffic: cannot set direction of BTN1\n");
	}

#if DEBUG
	gpiod_set_value(RED_LED, 1);
#endif
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

	/* Freeing buffer memory */
	if (mytraffic_buffer)
	{
		kfree(mytraffic_buffer);
	}

#if DEBUG
	gpiod_set_value(RED_LED, 0);
#endif

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
	int temp;
	char tbuf[256], *tbptr = tbuf;

	/* end of buffer reached */
	if (*f_pos >= mytraffic_len)
	{
		return 0;
	}

	/* do not go over then end */
	if (count > mytraffic_len - *f_pos)
		count = mytraffic_len - *f_pos;

	/* do not send back more than a bite */
	if (count > bite) count = bite;

	/* Transfering data to user space */ 
	if (copy_to_user(buf, mytraffic_buffer + *f_pos, count))
	{
		return -EFAULT;
	}

	tbptr += sprintf(tbptr,								   
		"read called: process id %d, command %s, count %d, chars ",
		current->pid, current->comm, count);

	for (temp = *f_pos; temp < count + *f_pos; temp++)					  
		tbptr += sprintf(tbptr, "%c", mytraffic_buffer[temp]);

	printk(KERN_DEBUG "%s\n", tbuf);

	/* Changing reading position as best suits */ 
	*f_pos += count; 
	return count; 
}

static ssize_t mytraffic_write(struct file *filp, const char *buf,
							size_t count, loff_t *f_pos)
{
	int temp;
	char tbuf[256], *tbptr = tbuf;

	/* end of buffer reached */
	if (*f_pos >= capacity)
	{
		printk(KERN_DEBUG
			"write called: process id %d, command %s, count %d, buffer full\n",
			current->pid, current->comm, count);
		return -ENOSPC;
	}

	/* do not eat more than a bite */
	if (count > bite) count = bite;

	/* do not go over the end */
	if (count > capacity - *f_pos)
		count = capacity - *f_pos;

	if (copy_from_user(mytraffic_buffer + *f_pos, buf, count))
	{
		return -EFAULT;
	}

	tbptr += sprintf(tbptr,								   
		"write called: process id %d, command %s, count %d, chars ",
		current->pid, current->comm, count);

	for (temp = *f_pos; temp < count + *f_pos; temp++)					  
		tbptr += sprintf(tbptr, "%c", mytraffic_buffer[temp]);

	printk(KERN_DEBUG "%s\n", tbuf);

	*f_pos += count;
	mytraffic_len = *f_pos;

	return count;
}

