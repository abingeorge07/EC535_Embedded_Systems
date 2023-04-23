/* Sources:
* 	https://0x00sec.org/t/linux-keylogger-and-notification-chains/4566
 */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/keyboard.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/notifier.h>

// Module Info
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

// Prototype
static void __exit keylog_exit(void);
static int __init keylog_init(void);
static int keys_pressed(struct notifier_block *, unsigned long, void *); // Callback function for the Notification Chain

module_init(keylog_init);
module_exit(keylog_exit);


// Initializing the notifier_block
static struct notifier_block nb = {
	.notifier_call = keys_pressed
};


static int keys_pressed(struct notifier_block *nb, unsigned long action, void *data) {
	struct keyboard_notifier_param *param = data;
	
	// We are only interested in certain keys
	if (action == KBD_KEYSYM && param->down && param->shift == 0) {

		if(param->value == KEY_UP) {
			printk(KERN_ALERT "UP\n");
		} else if(param->value == KEY_DOWN) {
			printk(KERN_ALERT "DOWN\n");
		} else if(param->value == KEY_LEFT) {
			printk(KERN_ALERT "LEFT\n");
		} else if(param->value == KEY_RIGHT) {
			printk(KERN_ALERT "RIGHT\n");
		} else if(param->value == KEY_1) {
			printk(KERN_ALERT "Saved state 1\n");
		} else if(param->value == KEY_2) {
			printk(KERN_ALERT "Saved state 2\n");
		} else if(param->value == KEY_3) {
			printk(KERN_ALERT "Saved state 3\n");
		} else if(param->value == KEY_4) {
			printk(KERN_ALERT "Saved state 4\n");
		}else if(param->value == KEY_ENTER) {
			printk(KERN_ALERT "Starting sequence of moves\n");
		} else if(param->value == KEY_ESC) {
			printk(KERN_ALERT "Stopping sequence\n");
		}
	}
	return NOTIFY_OK; // We return NOTIFY_OK, as "Notification was processed correctly"
}

static int __init keylog_init(void) {
	printk(KERN_ALERT "Keylogger loaded\n");
	register_keyboard_notifier(&nb);
	return 0;
}

static void __exit keylog_exit(void) {
	unregister_keyboard_notifier(&nb);
	printk(KERN_INFO "Keylogger unloaded\n");
}

MODULE_LICENSE("GPL");

