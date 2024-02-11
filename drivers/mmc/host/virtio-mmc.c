#include "virtio-mmc.h"
#include "linux/device/class.h"
#include "linux/kobject.h"
#include "linux/slab.h"
#include <linux/fs.h>
#include <linux/device.h>

struct virtmmc_data {
	struct virtio_device *vdev;
	struct mmc_host *mmc;
	struct virtqueue *vq;
	struct scatterlist sg;
};

static int virtio_mmc_open(struct inode *inode, struct file *filp) {
    // Open implementation
    return 0;
}

static int virtio_mmc_release(struct inode *inode, struct file *filp) {
    // Release implementation
    return 0;
}

static struct file_operations virtio_mmc_fops = {
    .open = virtio_mmc_open,
    .release = virtio_mmc_release,
};


static int virtio_mmc_probe(struct virtio_device *vdev) {
	int err;
	printk(KERN_INFO "virtio_mmc_probe\n");

	struct virtmmc_data *data;

	data = kcalloc(1, sizeof(*data), GFP_KERNEL);
	if (!data) {
		printk(KERN_ERR "Failed to allocate memory for virtmmc_data\n");
		return -ENOMEM;
	}
	vdev->priv = data;
	printk(KERN_INFO "virtio_mmc_probe: data allocated\n");

	err = register_chrdev(100, "virtio-mmc", &virtio_mmc_fops);
	if (err < 0) {
		printk(KERN_ERR "Failed to register char device\n");
		goto free_data;
	}
	printk(KERN_INFO "virtio_mmc_probe: char device registered\n");

	err = device_add(&vdev->dev);
	if (err) {
		printk(KERN_ERR "Failed to add device\n");
		goto unregister_chrdev;
	}
	printk(KERN_INFO "virtio_mmc_probe: device added\n");

	return 0;

remove_device:
	device_del(&vdev->dev);
unregister_chrdev:
	unregister_chrdev(100, "virtio-mmc");
free_data:
	kfree(data);

	return err;
}

static void virtio_mmc_remove(struct virtio_device *vdev) {
	printk(KERN_INFO "virtio_mmc_remove\n");

	struct virtmmc_data *data = vdev->priv;

	device_del(&vdev->dev);

	unregister_chrdev(100, "virtio-mmc");

	kfree(data);

	vdev->priv = NULL;
}