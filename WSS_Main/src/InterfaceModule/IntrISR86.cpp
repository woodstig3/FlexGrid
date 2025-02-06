/*
 * IntrISR86.cpp
 *
 *  Created on: Sep 23, 2024
 *      Author: Administrator
 */


#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/interrupt.h>


#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>


#define SERIAL_PORT "/dev/ttyS1" // Change to your serial device

#define IRQ_NUMBER 112 // Change to your interrupt number
static char *message = "Picture_rev_finish\n";

// Interrupt service routine
static irqreturn_t my_interrupt_handler(int irq, void *dev_id) {

	 // Handle the interrupt
	printk(KERN_INFO "Interrupt received!\n");

	// Attempt to write the message to the serial port
    if (fp != NULL) {
        kernel_write(fp, message, strlen(message), &fp->f_pos);
        printk(KERN_INFO "Message sent to serial port: %s", message);
    }
    return IRQ_HANDLED;
}

// Module initialization
static int __init my_module_init(void) {
    // Open the serial port
    fp = filp_open(SERIAL_PORT, O_WRONLY | O_NONBLOCK, 0);
    if (IS_ERR(fp)) {
        printk(KERN_ERR "Could not open serial port\n");
        return PTR_ERR(fp);
    }

    // Request the IRQ
    if (request_irq(IRQ_NUMBER, my_interrupt_handler, IRQF_TRIGGER_RISING, "lcos_done_handler", (void *)(my_interrupt_handler))) {
        printk(KERN_ERR "Failed to register IRQ\n");
        filp_close(fp, NULL);
        return -1;
    }

    printk(KERN_INFO "Module loaded and interrupt handler registered.\n");
    return 0;
}

// Module cleanup
static void __exit my_module_exit(void) {
    // Free the IRQ
    free_irq(IRQ_NUMBER, (void *)(my_interrupt_handler));
    // Close the serial port
    if (fp) {
        filp_close(fp, NULL);
    }
    printk(KERN_INFO "Module unloaded and IRQ freed.\n");
}

module_init(my_module_init);
module_exit(my_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Interrupt #86 Handler");
MODULE_AUTHOR("Glosine");




