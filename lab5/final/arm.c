// Name: Justin Sadler, Abin George 
// Date: 25-04-2023 
/* Sources: * 	https://0x00sec.org/t/linux-keylogger-and-notification-chains/4566 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/keyboard.h> 
#include <linux/fs.h> 
#include <linux/notifier.h>
#include <linux/slab.h> /* kmalloc() */
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
#include <linux/pwm.h>
#include <linux/gpio/driver.h> 
// NOTE: ADded min, max macros
/*
Changed globalServo to stack from heap
*/

MODULE_AUTHOR("Abin George, Justin Sadler");
MODULE_DESCRIPTION("Arm");
MODULE_LICENSE("GPL");

// Debugging purposes
#define DEBUG 0
#define DEBUG_SERVO 0

// GPIOS to control servo
#define ELBOW_GPIO 2
#define WRIST_GPIO 50 
#define GRIP_GPIO  23

// Keyboard Interrupts Definitions
#define KEY_UP 		0xF603
#define KEY_DOWN	0xF600
#define KEY_RIGHT	0xF602
#define KEY_LEFT	0xF601
#define KEY_1		0xF031	
#define KEY_2		0xF032
#define KEY_3		0xF033
#define KEY_4		0xF034
#define KEY_ESC		0xF01B
#define KEY_ENTER	0xF201
#define KEY_GRIP	0xFB67
#define KEY_UNGRIP	0xFB68


// Definitions for the Servo 
// Periods and duty cycles are in microseconds
#define PERIOD 20000
#define HS422_MIN_DUTYCYCLE	200 // duty cycle
#define HS422_MAX_DUTYCYCLE	900
#define SG90_MIN_DUTYCYCLE  200
#define SG90_MAX_DUTYCYCLE  900
#define PWM_STEP			50

// Definitions for sequence
#define TOT_SEQUENCE	4
#define TOT_MOTOR 	3
#define TIME_STAGE	100 // in milliseconds

// Useful Macros
#define MIN(X,Y) ((X) < (Y)) ? (X) : (Y)
#define MAX(X,Y) ((X) > (Y)) ? (X) : (Y)
#define SIGN(X)	 ((X) == 0) ? 0 : ((X) < 0 ? -1 : 1)


// STRUCTS 
// Servo struct
struct servo{
	int dutyTime;
	int targetDutyTime;
	struct timer_list timer;
};
	 

// Struct for sequence
struct sequence {
	int ACTIVE; //if it is active or not
	int STAGE;  //what stage we are on now
	int TOTAL;  //total number of stages assigned
	int MOTOR_PWM[TOT_MOTOR][TOT_SEQUENCE];
	int SAFETY[TOT_SEQUENCE];
	struct timer_list sequenceTimer;
};


// General Prototypes
static void arm_exit(void);
static int __init arm_init(void);

// Key Interrupts Prototypes
static int keys_pressed(struct notifier_block *, unsigned long, void *); // Callback function for the Notification Chain

// Servo Control Prototypes
static void wristServoFunction(struct timer_list* mytimer);
static void elbowServoFunction(struct timer_list* mytimer);
static void gripServoFunction(struct timer_list* mytimer);

// Sequence Prototypes
static void sequenceFun(struct timer_list* mytimer);
void unsetMotors(void);
int safetyCheck(void);
int atTargetDutyTime(struct servo * servo_ptr);
void setTargetDutyTimes(unsigned int stage);

module_init(arm_init);
module_exit(arm_exit);

// GPIOS Array
static struct gpio gpios[] = {
	{WRIST_GPIO,	GPIOF_OUT_INIT_LOW,	"Wrist Servo" }, /* default to OFF */
	{ELBOW_GPIO, 	GPIOF_OUT_INIT_LOW, "Elbow Servo" }, /* default to OFF */
	{GRIP_GPIO, 	GPIOF_OUT_INIT_LOW, "Grip Servo" }, /* default to OFF */
};

// Initializing the notifier_block
static struct notifier_block nb = {
	.notifier_call = keys_pressed
};


// Operation mode enum
enum SERVO {
	WRIST=0,
	ELBOW,	
	GRIP,
};



struct sequence * globalSequence = NULL;
struct servo * wristServo = NULL;
struct servo * elbowServo = NULL;
struct servo * gripServo = NULL;
int setServos = 0;

// Init module
static int __init arm_init(void){

	int err;

	// Keyboard Interrupts init
	printk(KERN_ALERT "Keylogger loaded\n");
	register_keyboard_notifier(&nb);

#if DEBUG
	printk(KERN_ALERT "Keylogger initialization successfull\n");
#endif

	// Servo init

	// Request GPIO lines
	err = gpio_request_array(gpios, ARRAY_SIZE(gpios));
	if(err) {
		printk(KERN_ALERT "Could not request GPIOs\n"); 
		goto fail; 
	}
	wristServo = (struct servo*) kmalloc(sizeof(struct servo*), GFP_KERNEL);
	elbowServo = (struct servo*) kmalloc(sizeof(struct servo*), GFP_KERNEL);
	gripServo  = (struct servo*) kmalloc(sizeof(struct servo*), GFP_KERNEL);
	
	wristServo->dutyTime = HS422_MIN_DUTYCYCLE;
	elbowServo->dutyTime = HS422_MIN_DUTYCYCLE;	
	gripServo->dutyTime = SG90_MIN_DUTYCYCLE;
	
	// timer setup
	timer_setup(&(wristServo->timer), wristServoFunction, 0);
	timer_setup(&(elbowServo->timer), elbowServoFunction, 0);
	timer_setup(&(gripServo->timer), gripServoFunction, 0);

	// starting timer
	mod_timer(&(wristServo->timer), jiffies+ msecs_to_jiffies(1000));
	mod_timer(&(elbowServo->timer), jiffies+ msecs_to_jiffies(1000));
	mod_timer(&(gripServo->timer), jiffies+ msecs_to_jiffies(1000));
#if DEBUG
	printk(KERN_ALERT "Servo initialization successfull\n");
#endif

	globalSequence = (struct sequence*) kmalloc(sizeof(struct sequence*), GFP_KERNEL);
	globalSequence->TOTAL = 0;
	unsetMotors();
	// timer setup
	timer_setup(&(globalSequence->sequenceTimer), sequenceFun, 0);
	
	return 0;
	
fail:
	arm_exit();
	return -1;

}


//Exit module
static void arm_exit(void){
	
	// removing all the keyboard logger resources
	unregister_keyboard_notifier(&nb);
	gpio_free_array(gpios, ARRAY_SIZE(gpios));

	if(wristServo) {
		del_timer(&(wristServo->timer));
		kfree(wristServo);
	}

	if(elbowServo) {
		del_timer(&(elbowServo->timer));
		kfree(elbowServo);
	}

	if(gripServo) {
		del_timer(&(gripServo->timer));
		kfree(gripServo);
	}
	
	if(globalSequence){
		del_timer(&(globalSequence->sequenceTimer));
		kfree(globalSequence);
	}
	

	printk(KERN_ALERT "Arm exit successfull\n");
}



// Keyboard interrupt main function
static int keys_pressed(struct notifier_block *nb, unsigned long action, void *data) {
	struct keyboard_notifier_param *param = data;
	
	// We are only interested in certain keys
	if (action == KBD_KEYSYM && param->down && param->shift == 0) {

		if(param->value == KEY_UP) {
			#if DEBUG
			printk(KERN_ALERT "UP\n");
			#endif
			wristServo->dutyTime += PWM_STEP;
			wristServo->dutyTime = MIN(HS422_MAX_DUTYCYCLE, wristServo->dutyTime);

		} else if(param->value == KEY_DOWN) {
			#if DEBUG
			printk(KERN_ALERT "DOWN\n");
			#endif

			wristServo->dutyTime -= PWM_STEP;
			wristServo->dutyTime = MAX(HS422_MIN_DUTYCYCLE, wristServo->dutyTime);

		} else if(param->value == KEY_LEFT) {
			#if DEBUG
			printk(KERN_ALERT "LEFT\n");
			#endif

			elbowServo->dutyTime -= PWM_STEP;
			elbowServo->dutyTime = MAX(HS422_MIN_DUTYCYCLE, elbowServo->dutyTime);

		} else if(param->value == KEY_RIGHT) {
			#if DEBUG
			printk(KERN_ALERT "RIGHT\n");
			#endif
			
			elbowServo->dutyTime += PWM_STEP;
			elbowServo->dutyTime = MIN(HS422_MAX_DUTYCYCLE, elbowServo->dutyTime);


		}  else if(param->value == KEY_GRIP) {
			#if DEBUG
			printk(KERN_ALERT "GRIP\n");
			#endif

			gripServo->dutyTime -= PWM_STEP;
			gripServo->dutyTime = MIN(SG90_MAX_DUTYCYCLE, gripServo->dutyTime);
		} else if(param->value == KEY_UNGRIP) {
			#if DEBUG
			printk(KERN_ALERT "UNGRIP\n");
			#endif

			gripServo->dutyTime += PWM_STEP;
			gripServo->dutyTime = MAX(SG90_MIN_DUTYCYCLE, gripServo->dutyTime);
		} else if(param->value == KEY_1) {
			#if DEBUG
			printk(KERN_ALERT "Saved state 1\n");
			#endif
			
			globalSequence->TOTAL = MAX(1, globalSequence->TOTAL);
			globalSequence->MOTOR_PWM[WRIST][0] = wristServo->dutyTime;
			globalSequence->MOTOR_PWM[ELBOW][0] = elbowServo->dutyTime;
			globalSequence->MOTOR_PWM[GRIP][0] = gripServo->dutyTime;
			globalSequence->SAFETY[0] = 1;			

		} else if(param->value == KEY_2) {
			#if DEBUG
			printk(KERN_ALERT "Saved state 2\n");
			#endif
			
			globalSequence->TOTAL = MAX(2, globalSequence->TOTAL);
			globalSequence->MOTOR_PWM[WRIST][1] = wristServo->dutyTime;
			globalSequence->MOTOR_PWM[ELBOW][1] = elbowServo->dutyTime;
			globalSequence->MOTOR_PWM[GRIP][1] = gripServo->dutyTime;
			globalSequence->SAFETY[1] = 1;


		} else if(param->value == KEY_3) {
			#if DEBUG
			printk(KERN_ALERT "Saved state 3\n");
			#endif

			globalSequence->TOTAL = MAX(3, globalSequence->TOTAL);
			globalSequence->MOTOR_PWM[WRIST][2] = wristServo->dutyTime;
			globalSequence->MOTOR_PWM[ELBOW][2] = elbowServo->dutyTime;
			globalSequence->MOTOR_PWM[GRIP][2] = gripServo->dutyTime;
			globalSequence->SAFETY[2] = 1;
			
			//globalSequence->TOTAL = (globalSequence->TOTAL < 3) ? 3 : globalSequence->TOTAL;

		} else if(param->value == KEY_4) {
			#if DEBUG
			printk(KERN_ALERT "Saved state 4\n");
			#endif

			globalSequence->TOTAL = 4;
			globalSequence->MOTOR_PWM[WRIST][3] = wristServo->dutyTime;
			globalSequence->MOTOR_PWM[ELBOW][3] = elbowServo->dutyTime;
			globalSequence->MOTOR_PWM[GRIP][3] = gripServo->dutyTime;
			globalSequence->SAFETY[3] = 1;


		}else if(param->value == KEY_ENTER) {
			int err;

			#if DEBUG
			printk(KERN_ALERT "Starting sequence of moves\n");
			#endif
			
			// do a safety check
			err = safetyCheck();
			if(err == 0){
				globalSequence->ACTIVE = 1;
				globalSequence->STAGE = 0;
				setTargetDutyTimes(0);
				mod_timer(&(globalSequence->sequenceTimer), jiffies+ msecs_to_jiffies(TIME_STAGE));
			}else{
				globalSequence->ACTIVE = 0;
				#if DEBUG
					printk(KERN_ALERT "Stages not set properly\n");
				#endif
			}

		} else if(param->value == KEY_ESC) {
			#if DEBUG
			printk(KERN_ALERT "Stopping sequence\n");
			#endif

			globalSequence->ACTIVE = 0;
			globalSequence->TOTAL = 0;
			unsetMotors();
			mod_timer(&(globalSequence->sequenceTimer), jiffies+ msecs_to_jiffies(0));
		}
	}
	return NOTIFY_OK; // We return NOTIFY_OK, as "Notification was processed correctly"
}


// wristServo control
static void wristServoFunction(struct timer_list* timer){
	
	gpio_set_value(WRIST_GPIO, 1);
	udelay(wristServo->dutyTime);
	gpio_set_value(WRIST_GPIO, 0);


	#if DEBUG_SERVO
	printk(KERN_ALERT "WRIST PWM is %d\n", wristServo->dutyTime);
	#endif

	// resetting timer
	mod_timer(&(wristServo->timer), jiffies+ usecs_to_jiffies(PERIOD-wristServo->dutyTime));

}

// elbowServo control
static void elbowServoFunction(struct timer_list* timer){
	
	gpio_set_value(ELBOW_GPIO, 1);
	udelay(elbowServo->dutyTime);
	gpio_set_value(ELBOW_GPIO, 0);


	#if DEBUG_SERVO
	printk(KERN_ALERT "ELBOW PWM is %d\n", elbowServo->dutyTime);
	#endif

	// resetting timer
	mod_timer(&(elbowServo->timer), jiffies+ usecs_to_jiffies(PERIOD-elbowServo->dutyTime));
}


// elbowServo control
static void gripServoFunction(struct timer_list* timer){
	
	gpio_set_value(GRIP_GPIO, 1);
	udelay(gripServo->dutyTime);
	gpio_set_value(GRIP_GPIO, 0);


	#if DEBUG_SERVO
	printk(KERN_ALERT "GRIP PWM is %d\n", gripServo->dutyTime);
	#endif

	// resetting timer
	mod_timer(&(gripServo->timer), jiffies+ usecs_to_jiffies(PERIOD-gripServo->dutyTime));
}


// Sequence main function
static void sequenceFun(struct timer_list* mytimer){
	
	if(globalSequence->ACTIVE == 1){
		
		if(atTargetDutyTime(wristServo) && atTargetDutyTime(elbowServo) && atTargetDutyTime(wristServo)){
			globalSequence->STAGE = (globalSequence->STAGE + 1) % globalSequence->TOTAL;
			setTargetDutyTimes(globalSequence->STAGE);
			mod_timer(&(globalSequence->sequenceTimer), jiffies+ msecs_to_jiffies(TIME_STAGE * 10));
		} else {
			wristServo->dutyTime += SIGN(wristServo->targetDutyTime - wristServo->dutyTime) * PWM_STEP;
			elbowServo->dutyTime += SIGN(elbowServo->targetDutyTime - elbowServo->dutyTime) * PWM_STEP;
			gripServo->dutyTime  += SIGN(gripServo ->targetDutyTime - gripServo ->dutyTime) * PWM_STEP;
			mod_timer(&(globalSequence->sequenceTimer), jiffies+ msecs_to_jiffies(TIME_STAGE));
			
		}
		
		#if DEBUG
			printk(KERN_ALERT "STAGE IS %d\n", globalSequence->STAGE);
		#endif


	}
	//else do nothing

}


// Used for Safety
void unsetMotors(void){
	int i;

	for(i=0 ; i<TOT_SEQUENCE; i++){
		globalSequence->SAFETY[i] = -1;
	}

}

// Safety Check
int safetyCheck(void){
	int i;

	if(globalSequence->TOTAL < 2) {
		return -1;
	}

	for(i=0 ; i<globalSequence->TOTAL; i++){
		if(globalSequence->SAFETY[i] < 0){
			printk(KERN_ALERT "ERROR: Position %d is undefined\n", i + 1);
			return -1;
		}

	}

	return 0;
	
}

void setTargetDutyTimes(unsigned int stage) {
	if(stage >= globalSequence->TOTAL) {
		printk(KERN_ALERT "Error: Invalid stage %u!", stage);
		return;
	}
	wristServo->targetDutyTime = globalSequence->MOTOR_PWM[WRIST][stage];
	elbowServo->targetDutyTime = globalSequence->MOTOR_PWM[ELBOW][stage];
	gripServo->targetDutyTime  = globalSequence->MOTOR_PWM[GRIP][stage];
}

int atTargetDutyTime(struct servo * servo_ptr) {
	return (servo_ptr->dutyTime == servo_ptr->targetDutyTime);
}
