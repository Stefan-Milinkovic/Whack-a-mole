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

/**
 * IRQ handler for button presses.
 * Toggles the corresponding LED and logs the button press to the /proc file.
 *
 * @param irq The IRQ number associated with the interrupt.
 * @param dev_id Device ID used to get the button index.
 * @return IRQ_HANDLED to indicate that the IRQ has been handled successfully.
 */
static irqreturn_t button_irq_handler(int irq, void *dev_id) {
    int btn_index = (int)(size_t)dev_id;    // Convert device ID to button index
    unsigned int led_gpio = GPIO_LEDS[btn_index];   // Get GPIO for the corresponding LED

    // Checking if the button is toggled
    if (button_debounce()) {
        bool is_on; 

        mutex_lock(&lock);  // Lock mutex to protect the LED toggling and button presses
        is_on = gpio_get_value(led_gpio);   // Get current state of LED
        gpio_set_value(led_gpio, !is_on);  // Toggle LED
        snprintf(PROC_BUF, PROCFS_MAX_SIZE, "Button %d pressed", btn_index);  // Write button press info in PROC_BUF
        mutex_unlock(&lock);    // unlock the mutex

        pr_info("LED on GPIO %d toggled, Button %d pressed\n", led_gpio, btn_index);    // print previous action to the kernel
    }
    return IRQ_HANDLED;     // IRQ has been handled
}

/**
 * Function to configure GPIOs and IRQs for each button.
 * Sets up the GPIO pins for input (buttons) and output (LEDs), and requests IRQs
 *
 * @param btn_index The index of the button in the GPIO arrays.
 * @return 0 on successful setup, -ENODEV if GPIO validation fails, or a negative error code if IRQ request fails.
 */
static int setup_button_irq(int btn_index) {
    int retval;
    unsigned int btn_gpio = GPIO_BTNS[btn_index];		// Button GPIO number
    unsigned int led_gpio = GPIO_LEDS[btn_index];		// Corresponding LED GPIO number

    // Validate the GPIO numbers for the button and LED
    if (!gpio_is_valid(led_gpio) || !gpio_is_valid(btn_gpio)){
        pr_info("GPIO invalid: LED %d or Button %d\n", led_gpio, btn_gpio);
        return -ENODEV;     // Return device not found error
    }

	// Request GPIOs for buttons and LEDs
    gpio_request(btn_gpio, "GPIO_BTN");
    gpio_request(led_gpio, "GPIO_LED");
    
    // Set directions for GPIOs
    gpio_direction_input(btn_gpio);
    gpio_direction_output(led_gpio, 0);

	// Map the button GPIO to IRQ number 
    irq_numbers[btn_index] = gpio_to_irq(btn_gpio);
    
    // set up threaded IRQ handler for handling button presses
	retval = request_threaded_irq(irq_numbers[btn_index], NULL, button_irq_handler, IRQF_TRIGGER_LOW | IRQF_ONESHOT, "MyCustomIRQProc", (void *)(size_t)btn_index);
    if (retval) {
        pr_info("Unable to request IRQ: %d for Button %d\n", retval, btn_index);
        return retval;
    }

    // Print successful IRQ setup
    pr_info("IRQ for Button %d on GPIO %d with LED GPIO %d setup successfully\n", btn_index, btn_gpio, led_gpio);
    return 0;   // return success
}

// ---------- PROC OPERATIONS ----------

/**
 * Reads data from the /proc file.
 * Function is called when a process reads from the proc file. It reads the 
 * contents of the kernel buffer (PROC_BUF) into the user buffer, ensuring that the 
 * user space receives the current state or last message logged in the buffer.
 *
 * @param file Pointer to the file structure
 * @param user_buffer Buffer in user space where data will be copied.
 * @param count Size of the user buffer
 * @param position Current position in the file, used to determine if there's more data to read.
 * @return The number of bytes copied if successful, 0 if there is no more data, or a negative error code.
 */
ssize_t procfile_read(struct file *file, char __user *user_buffer, size_t count, loff_t *position) {
    int len = strlen(PROC_BUF);  // Get the length of the string in PROC_BUF

    if (*position > 0) {
        return 0;  // All data has been read, signify no more data to read
    }

    if (count < len) {
        return -EFAULT;  // Buffer provided by user space is too small for data
    }

    mutex_lock(&lock);  // Lock the mutex to protect PROC_BUF from concurrent access
    if (copy_to_user(user_buffer, PROC_BUF, len)) {
        mutex_unlock(&lock);    // Unlock the mutex if copy fails
        return -EFAULT;  // Failed to copy data to user space
    }

    PROC_BUF[0] = '\0';  // Clear the buffer after successful  read
    *position += len;   // Update the position for the next read operation
    mutex_unlock(&lock);    // unlock mutex 

    return len;  // Return the number of bytes read
}

/**
 * Handles write operations to the /proc file.
 * Function is triggered when a process writes to the proc file. It reads the 
 * command from the user buffer, and performs actions based on the command 
 * (e.g., turning LEDs on or off).
 *
 * @param file Pointer to the file structure
 * @param user_buffer Buffer in user space containing the command to process.
 * @param count Number of bytes to read from the user buffer.
 * @param position Current position in the file
 * @return The number of bytes processed if successful, or a negative error code.
 */
ssize_t procfile_write(struct file *file, const char __user *user_buffer, size_t count, loff_t *position) {
    char command[20];   // Buffer to store command from user space
    if (count > sizeof(command) - 1)
        return -EINVAL;    // Return invalid argument error if command is too long
    
    if (copy_from_user(command, user_buffer, count))
        return -EFAULT; // Return bad address error if copy from user fails
    
    command[count] = '\0'; // Null terminate the command string
    
    // Process the command and toggle the appropriate LED
    if (strcmp(command, "red_ON") == 0) {
        gpio_set_value(GPIO_LEDS[0], 1);    // Turn on the red LED
    } else if (strcmp(command, "blue_ON") == 0) {
        gpio_set_value(GPIO_LEDS[1], 1);    // Turn on the blue LED
    } else if (strcmp(command, "green_ON") == 0) {
        gpio_set_value(GPIO_LEDS[2], 1);    // Turn on the green LED
    } else if (strcmp(command, "yellow_ON") == 0) {
        gpio_set_value(GPIO_LEDS[3], 1);    // Turn on the yellow LED
    } else if (strcmp(command, "LED_OFF") == 0) {
        for (int i = 0; i < 4; i++) {
            gpio_set_value(GPIO_LEDS[i], 0);    // Turn off all LEDs
        }
    }

    return count;   // Return the number of bytes processed
}

// Initialize the module 
static int __init my_module_init(void) {
	
	// For the proc file
	memset(PROC_BUF,0,PROCFS_MAX_SIZE);	// Initialize the PROC_BUF with zeros to prepare it for use
  	proc_create(PROCFS_NAME, 0666, NULL, &proc_fops);	// Create proc file
  	pr_info("Proc init completed \n");

	// "setup_button_irq" function initializes the GPIOs
    for (int i = 0; i < NUM_BUTTONS; ++i) {
        if (setup_button_irq(i) != 0) {
            pr_info("Failed to initialize button %d\n", i);
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
        free_irq(irq_numbers[i], (void *)&GPIO_LEDS[i]);    // Free the IRQ assigned to button using the button's LED GPIO as the device ID
        gpio_free(GPIO_BTNS[i]);
        gpio_free(GPIO_LEDS[i]);
    }
    pr_info("Module exited successfully\n");
}

module_init(my_module_init);
module_exit(my_module_exit);

MODULE_AUTHOR("Stefan Milinkovic");
MODULE_LICENSE("GPL");   
