#include<linux/init.h>
#include<linux/module.h>
#include<linux/kernel.h>
#include <linux/mutex.h>	// Kernel mutex

#include<linux/gpio.h>   
#include<linux/interrupt.h>
#include<linux/delay.h>
#include <linux/jiffies.h>

#include <linux/proc_fs.h>	/* Necessary because we use proc fs */
#include <asm/uaccess.h>	/* for copy_*_user */

#define NUM_BUTTONS 4				// # of buttons we're using							
#define PROCFS_NAME "whackamole"	// Proc file location 
#define PROCFS_MAX_SIZE 	100  	// Sample Size    
static DEFINE_MUTEX(lock);         
static char PROC_BUF[PROCFS_MAX_SIZE];       //This is statically allocated, So be aware this is the kernel memory.

// ---------- PROC FUNCTIONS ----------
ssize_t procfile_read(struct file *file, char __user *user_buffer, size_t count, loff_t *position); 			//A Read operation is requested from this file.
ssize_t procfile_write(struct file *file, const char __user *user_buffer, size_t count, loff_t *position);		//A write operation is requested from this file.
static int procfile_open(struct inode *inode, struct file *file);		//File is Opened 
static int procfile_release(struct inode *inode, struct file *file);	//A close operation is requested.

// proc fops struct
static struct proc_ops proc_fops = {
	.proc_open = procfile_open,
  	.proc_read = procfile_read,
  	.proc_write = procfile_write,
  	.proc_release = procfile_release
};

// File is opened
static int procfile_open(struct inode *inode, struct file *file)
{
	//When the file is being opened, Just print to the kernel dmesg
    printk(KERN_INFO "proc file opend.....\t");
    return 0;
}
// File is released
static int procfile_release(struct inode *inode, struct file *file)
{
	//When the file is being released, Just print to the kernel dmesg
    printk(KERN_INFO "proc file released.....\n");
    return 0;
}

// ---------------------------------------
// ---------- GPIO Declarations ----------
static unsigned int GPIO_LEDS[] = {4, 17, 22, 6};			// GPIOs for LEDs; RED=4, BLUE=17, GREEN=22, YELLOW=6
static unsigned int GPIO_BTNS[] = {18, 23, 12, 16};  		// GPIOs for buttons; RED=18, BLUE=23, GREEN=12, YELLOW=16
static unsigned int irq_numbers[4];  						// IRQ numbers for each button

// Debounce function for button presses
static int button_debounce(void) {
    static unsigned long debounce = 0;
    unsigned long j = jiffies;
    unsigned long stamp = j + HZ/4;  // Debounce time of 1/4 a second
    
    if (time_after(j, debounce)) {
        debounce = stamp;
        return 1;
    }
    return 0;
}

// static int last_button_pressed = -1;	// Variable to store last button pressed

// General button IRQ handler function
static irqreturn_t button_irq_handler(int irq, void *dev_id) {
    unsigned int led_gpio = *(unsigned int *)dev_id;
    // int btn_index = (int)(dev_id - GPIO_LEDS); // Calculate button index based on LED GPIO array position
    
    if (button_debounce()) {
        bool is_on;

        // Protect the read-modify-write sequence from concurrent access
        mutex_lock(&lock);
        is_on = gpio_get_value(led_gpio);
        gpio_set_value(led_gpio, !is_on);	// Toggle LED
        // last_button_pressed = btn_index; // Store last button pressed
        mutex_unlock(&lock);

        pr_info("LED on GPIO %d toggled\n", led_gpio);
    }
    return IRQ_HANDLED;
}


// Setup function for each button IRQ
static int setup_button_irq(int btn_index) {
    int retval;
    unsigned int btn_gpio = GPIO_BTNS[btn_index];		// Button GPIO number
    unsigned int led_gpio = GPIO_LEDS[btn_index];		// Corresponding LED GPIO number

    if (!gpio_is_valid(led_gpio) || !gpio_is_valid(btn_gpio)){
        pr_info("GPIO invalid: LED %d or Button %d\n", led_gpio, btn_gpio);
        return -ENODEV;
    }

	// Request GPIOs for buttons and LEDs
    gpio_request(btn_gpio, "GPIO_BTN");
    gpio_request(led_gpio, "GPIO_LED");
    
    // Set directions for GPIOs
    gpio_direction_input(btn_gpio);
    gpio_direction_output(led_gpio, 0);

	// Convert GPIO to IRQ number 
    irq_numbers[btn_index] = gpio_to_irq(btn_gpio);
    
    // set up threaded IRQ handler
    retval = request_threaded_irq(irq_numbers[btn_index], NULL, button_irq_handler, IRQF_TRIGGER_LOW | IRQF_ONESHOT, "MyCustomIRQProc", (void *)&GPIO_LEDS[btn_index]);

    if (retval) {
        pr_info("Unable to request IRQ: %d for Button %d\n", retval, btn_index);
        return retval;
    }

    pr_info("IRQ for Button %d on GPIO %d with LED GPIO %d setup successfully\n", btn_index, btn_gpio, led_gpio);
    return 0;
}

// ---------- PROC OPERATIONS ----------
// Reading data (game state, LED/ button status
ssize_t procfile_read(struct file *file, char __user *user_buffer, size_t count, loff_t *position) {
    int len;
    char buffer[PROCFS_MAX_SIZE];

    // Check if position is beyond the available data, to avoid repeated reads
    if (*position > 0) {
        return 0;  // All data has been read, return 0 to signify no more data to read
    }

    // Construct a string with the current LED states
    len = snprintf(buffer, PROCFS_MAX_SIZE, "LED States - RED: %d, BLUE: %d, GREEN: %d, YELLOW: %d\n",
                   gpio_get_value(GPIO_LEDS[0]),  // State of RED LED
                   gpio_get_value(GPIO_LEDS[1]),  // State of BLUE LED
                   gpio_get_value(GPIO_LEDS[2]),  // State of GREEN LED
                   gpio_get_value(GPIO_LEDS[3])); // State of YELLOW LED

    // Check buffer length against count to avoid buffer overflow
    if (count < len) {
        return -EFAULT;  // User buffer is too small for the data
    }

    // Copy the buffer to user space
    if (copy_to_user(user_buffer, buffer, len)) {
        return -EFAULT;  // Failed to copy data to user space
    }

    *position += len;  // Update the position for the next read operation
    return len;  // Return the number of bytes read
}

// Handle commands from the QT App
ssize_t procfile_write(struct file *file, const char __user *user_buffer, size_t count, loff_t *position) {
    char command[20];
    if (count > sizeof(command) - 1)
        return -EINVAL;
    
    if (copy_from_user(command, user_buffer, count))
        return -EFAULT;
    
    command[count] = '\0'; // Null terminate the string
    
    // Example command parsing
    if (strcmp(command, "red_ON") == 0) {
        gpio_set_value(GPIO_LEDS[0], 1);
    } else if (strcmp(command, "blue_ON") == 0) {
        gpio_set_value(GPIO_LEDS[1], 1);
    } else if (strcmp(command, "green_ON") == 0) {
        gpio_set_value(GPIO_LEDS[2], 1);
    } else if (strcmp(command, "yellow_ON") == 0) {
        gpio_set_value(GPIO_LEDS[3], 1);
    } else if (strcmp(command, "LED_OFF") == 0) {
        for (int i = 0; i < 4; i++) {
            gpio_set_value(GPIO_LEDS[i], 0);
        }
    }

    return count;
}

// Initialize the module 
static int __init my_module_init(void) {
	
	// For the proc file
	memset(PROC_BUF,0,PROCFS_MAX_SIZE);	// Zero Out our buffer 
  	proc_create(PROCFS_NAME, 0666, NULL, &proc_fops);	// Create proc file
  	pr_info("Proc init completed \n");

	// "setup_button_irq" function initializes the GPIOs
    for (int i = 0; i < NUM_BUTTONS; ++i) {
        if (setup_button_irq(i) != 0) {
            return -1;  // Initialization failed
        }
    }
    
    pr_info("Module initialized successfully\n");
    return 0;
}

// Exit the module
static void __exit my_module_exit(void) {
	remove_proc_entry(PROCFS_NAME, NULL);	// removing proc file
	
	// free the IRQ and GPIOs 
    for (int i = 0; i < NUM_BUTTONS; ++i) {
        free_irq(irq_numbers[i], (void *)&GPIO_LEDS[i]);
        gpio_free(GPIO_BTNS[i]);
        gpio_free(GPIO_LEDS[i]);
    }
    pr_info("Module exited successfully\n");
}

module_init(my_module_init);
module_exit(my_module_exit);

MODULE_AUTHOR("Stefan Milinkovic");
MODULE_LICENSE("GPL");   
