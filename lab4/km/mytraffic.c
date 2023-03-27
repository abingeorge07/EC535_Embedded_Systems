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
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/delay.h>

// GPIO Numbers
#define RED_LED  67 
#define YELLOW_LED 68 
#define GREEN_LED  44 
#define BTN0  26
#define BTN1  46 

#define DEBUG 0


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


// LED display functions
void displayFun(struct timer_list* timer);
void normal_disp(void);
void red_disp(void);
void yellow_disp(void);
void pedestrian_disp(void);


/* Structure that declares the usual file */
/* access functions */
struct file_operations mytraffic_fops = {
	read: mytraffic_read,
	write: mytraffic_write,
	open: mytraffic_open,
	release: mytraffic_release
};

// Mode of traffic light
// Operation mode enum
enum OperationalMode {
	NORMAL,
	FLASHING_RED,
	FLASHING_YELLOW,
	PEDESTRIAN,
};


/* Declaration of the init and exit functions */
module_init(mytraffic_init);
module_exit(mytraffic_exit);
static unsigned capacity = 256;
static unsigned bite = 256;
module_param(capacity, uint, S_IRUGO);
module_param(bite, uint, S_IRUGO);

/* Major number */
static int mytraffic_major = 61;
// if pedestrian is called
static int pedestrian_called = 0;

// BTN0 irq
static int btn0_irq;
// BTN1 irq
static int btn1_irq;

// GPIO Array
static struct gpio gpios[] = {
	{RED_LED,	GPIOF_OUT_INIT_LOW,	"Red LED" }, /* default to OFF */
	{YELLOW_LED, 	GPIOF_OUT_INIT_LOW, "Yellow LED" }, /* default to OFF */
	{GREEN_LED, 	GPIOF_OUT_INIT_LOW, "Green LED"   }, /* default to OFF */
	{BTN0, 		GPIOF_DIR_IN,  		"BTN0"  }, 
	{BTN1, 		GPIOF_DIR_IN,  		"BTN1"  },
};

// Global variables all stored in a struct
struct global{
  enum OperationalMode mode;
    int freq;
    int time ;
    struct timer_list timer;
  int counter;  // coutner in cycle
  int status; // if LED is on or off
};

static struct global* globalVar = NULL;



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

	globalVar = (struct global*) kmalloc(sizeof(struct global), GFP_KERNEL);
	globalVar-> freq = 1;
	globalVar-> time = 1000; // in milliseconds
	globalVar -> mode = NORMAL;
	globalVar -> counter = 0;
	globalVar -> status = 0;
	timer_setup(&(globalVar->timer), displayFun, 0);
	mod_timer(&(globalVar->timer), jiffies+ msecs_to_jiffies(globalVar->time));

	

#if DEBUG
	printk(KERN_ALERT "Started timer");
#endif
	return 0;

fail: 
	mytraffic_exit(); 
	return result;
}

static void mytraffic_exit(void)
{
	/* Freeing the major number */
	unregister_chrdev(mytraffic_major, "mytraffic");
	if(globalVar) {
		kfree(globalVar);
	}

	free_irq(btn0_irq, NULL);
	free_irq(btn1_irq, NULL);
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
        char kernelBuf[128];
	char * bufPtr = kernelBuf;

	bufPtr += sprintf(bufPtr, "[MODE]: ");
	switch(globalVar->mode) {
		case NORMAL:
			bufPtr += sprintf(bufPtr, "Normal\n");
			break;
		case FLASHING_RED:
			bufPtr += sprintf(bufPtr, "Flashing Red\n");
			break;
		case FLASHING_YELLOW:
			bufPtr += sprintf(bufPtr, "Flashing yellow\n");
			break;
	}

	bufPtr += sprintf(bufPtr, "[Current Cycle Rate]: %d HZ\n", globalVar->freq);

	bufPtr += sprintf(bufPtr, "[Current Status (RED/YELLOW/Green)]: %d%d%d\n", 
		gpio_get_value(RED_LED), gpio_get_value(YELLOW_LED), gpio_get_value(GREEN_LED));

	bufPtr += sprintf(bufPtr, "[Pedestrian Present?]: %d\n", pedestrian_called);


	if(copy_to_user(buf, kernelBuf, strlen(kernelBuf) + 1)) {
		return -EFAULT;
	}
     
	return 0;
}

static ssize_t mytraffic_write(struct file *filp, const char *buf,
							size_t count, loff_t *f_pos)
{

	// new frequency values
	long int freqNew; 
	// checks if string to integer conversion works
	int err;
	char mytimer_buffer[32];

	/* end of buffer reached */
	if (*f_pos >= capacity)
	{
		return -ENOSPC;
	}

	/* do not eat more than a bite */
	if (count > bite) count = bite;

	/* do not go over the end */
	if (count > capacity - *f_pos)
		count = capacity - *f_pos;

	if (copy_from_user(mytimer_buffer + *f_pos, buf, count))
	{
		return -EFAULT;
	}

	// this ensures that the previous buffer will not have an impact on the current buffer
        mytimer_buffer[count] = '\0';


	// converts the string to an integer
	err = kstrtol(mytimer_buffer, 10, &freqNew);

	if(err != 0){
		printk(KERN_ALERT "Error occured in conversion\n");
	}

	globalVar->freq = freqNew;
	globalVar->time = 1000/freqNew;

	#if DEBUG
		printk(KERN_ALERT "FREQ = %ld\n", freqNew);
	#endif

	*f_pos = 0;
    return count;
}

static irqreturn_t btn0_handler(int irq, void * dev_id) {

#if DEBUG
	printk(KERN_ALERT "Switching flashing modes\n");
#endif

	switch(globalVar->mode) {
		case NORMAL:
			globalVar->mode = FLASHING_RED;
			break;
		case FLASHING_RED:
			globalVar->mode = FLASHING_YELLOW;
			break;
		case FLASHING_YELLOW:
			globalVar->mode = NORMAL;
			break;
         	case PEDESTRIAN:
		        break;
	}
	// resets the traffic light
	gpio_set_value(GREEN_LED, 0);
	gpio_set_value(RED_LED, 0);
	gpio_set_value(YELLOW_LED, 0);

	globalVar->counter = 0;
	globalVar->status = 0;
	displayFun(&(globalVar->timer));
	return IRQ_HANDLED;
}

static irqreturn_t btn1_handler(int irq, void * dev_id) {

#if DEBUG
	printk(KERN_ALERT "Pedestrian Called");
#endif

	if(globalVar->mode == NORMAL || globalVar -> mode == PEDESTRIAN){
		pedestrian_called = 1;

		// resets the traffic light
		//gpio_set_value(GREEN_LED, 0);
		//gpio_set_value(RED_LED, 0);
		//gpio_set_value(YELLOW_LED, 0);

		//globalVar->counter = 0;
		//globalVar->status = 0;
		//displayFun(&(globalVar->timer));	
	}


	return IRQ_HANDLED;
}



void displayFun(struct timer_list* timer){
  
	#if DEBUG
	printk(KERN_ALERT "Reached Display Function");
	#endif

	if(pedestrian_called && globalVar->counter > 5 && globalVar-> mode == NORMAL) {
	         globalVar->counter = 0;
		 globalVar->mode = PEDESTRIAN;
	         pedestrian_disp();
	}

	else if(pedestrian_called && globalVar->mode == PEDESTRIAN){
	        globalVar->counter = 0;
		 globalVar->mode = PEDESTRIAN;
	         pedestrian_disp();
	}

	else{
		switch(globalVar -> mode){
			
			case NORMAL:
				normal_disp();
				break;

			case FLASHING_RED:
				red_disp();
				break;
			
			case FLASHING_YELLOW:
				yellow_disp();
				break;
		        case PEDESTRIAN:
			        pedestrian_disp();
                                break;
		}  
	}
}


void normal_disp(void){

#if DEBUG
  printk(KERN_ALERT "NORMAL");
#endif

  if(globalVar->counter > 5)
    globalVar -> counter = 0;
  
  if(globalVar->counter <3){
    
  if(globalVar->status == 0){
      gpio_set_value(GREEN_LED, 1);
      globalVar -> status = 1;
       
  }else{
      gpio_set_value(GREEN_LED, 0);
      globalVar -> status = 0;
      (globalVar -> counter)++;
    }
       
    
  }else if(globalVar -> counter < 4){
    
  if(globalVar->status == 0){
      gpio_set_value(YELLOW_LED, 1);
      globalVar -> status = 1;
   
  }else{
      gpio_set_value(YELLOW_LED, 0);
      (globalVar -> counter)++;
      globalVar -> status = 0;
    }
  }
  else{
    
    if(globalVar->status == 0){
      gpio_set_value(RED_LED, 1);
      globalVar -> status = 1;
    }else{
      gpio_set_value(RED_LED, 0);
      globalVar -> status = 0;
      (globalVar -> counter)++;
    }

  }



  mod_timer(&(globalVar->timer), jiffies+ msecs_to_jiffies(globalVar->time));

  
}

void red_disp(void){

#if DEBUG
  printk(KERN_ALERT "RED");
#endif

	if(globalVar->status == 0){
		gpio_set_value(RED_LED, 1);
		globalVar -> status = 1;
    }else{
		gpio_set_value(RED_LED, 0);
		globalVar -> status = 0;
    }

	mod_timer(&(globalVar->timer), jiffies+ msecs_to_jiffies(globalVar->time));

}



void yellow_disp(void){

#if DEBUG
  printk(KERN_ALERT "YELLOW");
#endif

	if(globalVar->status == 0){
		gpio_set_value(YELLOW_LED, 1);
		globalVar -> status = 1;
    }else{
		gpio_set_value(YELLOW_LED, 0);
		globalVar -> status = 0;
    }

	mod_timer(&(globalVar->timer), jiffies+ msecs_to_jiffies(globalVar->time));

}


void pedestrian_disp(void){

	#if DEBUG
		printk(KERN_ALERT "PEDESTRIAN");
	#endif

	
	if(globalVar->counter < 5){
		if(globalVar->status == 0){
		        pedestrian_called = 0;
			gpio_set_value(RED_LED, 1);
			gpio_set_value(YELLOW_LED, 1);
			globalVar->status = 1;
		}
		else{
			gpio_set_value(RED_LED, 0);
			gpio_set_value(YELLOW_LED, 0);
			globalVar->status = 0;
			(globalVar->counter)++;
		}
		
	}
	else{
	// probably wont run but just a safety net
	//	pedestrian_called = 0;
		globalVar->mode = NORMAL;
		globalVar->status = 0;
	}

	if(globalVar->counter > 4){
	  //	pedestrian_called = 0;
		globalVar->mode = NORMAL;
		globalVar->status = 0;
		globalVar->counter = 0;
	}

	mod_timer(&(globalVar->timer), jiffies+ msecs_to_jiffies(globalVar->time));

}
