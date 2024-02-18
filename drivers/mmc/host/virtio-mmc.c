#include "virtio-mmc.h"
#include <linux/virtio.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>

struct virtmmc_data {
	struct virtio_device *vdev;
	struct mmc_host *mmc;
	struct virtqueue *vq;
	struct scatterlist sg;
	dev_t devt;
	struct cdev cdev;
	struct device *device;
	struct class *chardev_class;
};

static const struct file_operations virtio_mmc_fops = {
	.owner = THIS_MODULE,
};

static int create_dev_entry(struct virtmmc_data *data) {
	int err;

	err = alloc_chrdev_region(
		&data->devt, 
		VIRTIO_MMC_FIRST_MINOR, 
		VIRTIO_MMC_MINOR_COUNT, 
		VIRTIO_MMC_DEV_NAME
		);
	if (err) {
		printk(KERN_ERR "Failed to allocate char device region\n");
		return err;
	}

	data->chardev_class = class_create("mmcblk");
	if (IS_ERR(data->chardev_class)) {
		printk(KERN_ERR "Failed to create class\n");
		err = PTR_ERR(data->chardev_class);
		goto free_chrdev_region;
	}

	cdev_init(&data->cdev, &virtio_mmc_fops);
	data->cdev.owner = THIS_MODULE;

	int dev_major = MAJOR(data->devt);
	err = cdev_add(&data->cdev, MKDEV(dev_major, VIRTIO_MMC_FIRST_MINOR), VIRTIO_MMC_MINOR_COUNT);
	if (err) {
		printk(KERN_ERR "Failed to add cdev\n");
		goto free_cdev;
	}

	data->device = device_create(data->chardev_class, NULL, MKDEV(dev_major, VIRTIO_MMC_FIRST_MINOR), NULL, "mmcblk%d", 0);
	if (IS_ERR(data->device)) {
		printk(KERN_ERR "Failed to create device\n");
		err = PTR_ERR(data->device);
		goto free_chardev_class;
	}

	return 0;

free_cdev:
	cdev_del(&data->cdev);

free_chardev_class:
	class_destroy(data->chardev_class);

free_chrdev_region:
	unregister_chrdev_region(data->devt, VIRTIO_MMC_MINOR_COUNT);

	return err;
}

static void dealloc_dev_entry(struct virtmmc_data *data) {
	device_destroy(data->chardev_class, data->devt);
	class_destroy(data->chardev_class);
	cdev_del(&data->cdev);
	unregister_chrdev_region(data->devt, VIRTIO_MMC_MINOR_COUNT);
}

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

	err = create_dev_entry(data);
	if (err) {
		printk(KERN_ERR "Failed to create device entry\n");
		goto free_data;
	}

free_data:
	kfree(data);

	return err;
}

static void virtio_mmc_remove(struct virtio_device *vdev) {
	printk(KERN_INFO "virtio_mmc_remove\n");

	struct virtmmc_data *data = vdev->priv;

	dealloc_dev_entry(data);

	kfree(data);
}