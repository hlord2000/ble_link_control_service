#include <zephyr/kernel.h>

int main(void)
{
	while (true) {		
		printk("Hello World!\n");
		k_msleep(1000);
	}
	return 0;
}
