#include "virtio-mmc.h"
#include "linux/device/class.h"
#include "linux/slab.h"
#include <linux/fs.h>
#include <linux/device.h>

struct virtmmc_data {
	struct virtio_device *vdev;
	struct mmc_host *mmc;
	struct virtqueue *vq;
	struct scatterlist sg;
	struct class* class;
};

// static int virtio_mmc_open(struct inode *inode, struct file *filp) {
//     // Open implementation
//     return 0;
// }

// static int virtio_mmc_release(struct inode *inode, struct file *filp) {
//     // Release implementation
//     return 0;
// }

// static struct file_operations virtio_mmc_fops = {
//     .open = virtio_mmc_open,
//     .release = virtio_mmc_release,
// };


static int virtio_mmc_probe(struct virtio_device *vdev) {
	int err;
	printk(KERN_INFO "virtio_mmc_probe\n");

	struct virtmmc_data *data;

	data = kcalloc(1, sizeof(*data), GFP_KERNEL);
	if (!data) {
		printk(KERN_ERR "Failed to allocate memory for virtmmc_data\n");
		return -ENOMEM;
	}
	printk(KERN_INFO "virtio_mmc_probe: data allocated\n");

	vdev->priv = data;

    data->class = class_create("virtmmc");
    if (IS_ERR(data->class)) {
        printk(KERN_ERR "Failed to create virtmmc class\n");
        err = PTR_ERR(data->class);
		goto free_data;
    }
	printk(KERN_INFO "virtio_mmc_probe: class created\n");

    // Create the device node
    if (!device_create(data->class, NULL, MKDEV(0, 0), NULL, "virtmmc")) {
        printk(KERN_ERR "Failed to create virtmmc device\n");
        err = -EFAULT;
		goto destroy_class;
    }
	printk(KERN_INFO "virtio_mmc_probe: device created\n");

	return 0;

destroy_class:
	class_destroy(data->class);
free_data:
	kfree(data);

	return err;
}

static void virtio_mmc_remove(struct virtio_device *vdev) {
	printk(KERN_INFO "virtio_mmc_remove\n");

	struct virtmmc_data *data = vdev->priv;

	device_destroy(data->class, MKDEV(0, 0));
	class_destroy(data->class);

	kfree(data);

	vdev->priv = NULL;
}