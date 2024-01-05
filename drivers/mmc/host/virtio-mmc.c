#include <linux/init.h>
#include <linux/module.h>

MODULE_LICENSE("GPL");

static int __init hello_init(void)
{
    printk(KERN_INFO "Hello, world\n");
    return 0;
}

static void __exit hello_exit(void)
{
    printk(KERN_INFO "Goodbye, cruel world. I'm leaving you today.\n Goodbye, goodbye, goodbye.\n Goodbye, all you people. There's nothing you can say\n To make me change my mind. Goodbye.\n");
}

module_init(hello_init);
module_exit(hello_exit);
