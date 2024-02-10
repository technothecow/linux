#include "linux/device.h"
#include "linux/kstrtox.h"
#include "linux/mod_devicetable.h"
#include "linux/printk.h"
#include "linux/scatterlist.h"
#include "linux/slab.h"
#include "linux/sysfs.h"
#include "linux/virtio.h"
#include "linux/virtio_config.h"
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("VirtIO MMC driver");
MODULE_AUTHOR("mi");

#define VIRTIO_MMC_DEV_ID 42

static struct virtio_device_id id_table[] = {
    { 
        .device = VIRTIO_MMC_DEV_ID,
        .vendor = VIRTIO_DEV_ANY_ID,
    },
    { 0 },
};

struct virtmmc_info {
    struct virtqueue *vq;
};

static int virtio_mmc_probe(struct virtio_device *vdev) {
    // Add dummy /dev/mmcblk0
    printk(KERN_INFO "virtio_mmc_probe\n");
    struct device *dev = &vdev->dev;
    dev_t devno = MKDEV(0, 0);
    struct device *dummy_dev = device_create(dev->class, dev, devno, NULL, "mmcblk0");
    printk(KERN_INFO "dummy_dev: %p\n", dummy_dev);
    if (IS_ERR(dummy_dev)) {
        dev_err(dev, "Failed to create dummy device\n");
        return PTR_ERR(dummy_dev);
    }

    return 0;
}

static void virtio_mmc_remove(struct virtio_device *vdev) {
    // Remove dummy /dev/mmcblk0
    printk(KERN_INFO "virtio_mmc_remove\n");
    struct device *dev = &vdev->dev;
    device_destroy(dev->class, MKDEV(0, 0));

    // Undo any other operations performed in the probe function

    // Add your code here to undo any other operations performed in the probe function

}

static struct virtio_driver virtio_mmc_driver = {
    .driver = {
        .name = "virtio_mmc",
        .owner = THIS_MODULE,
    },
    .id_table = id_table,
    .probe = virtio_mmc_probe,
    .remove = virtio_mmc_remove,
};

module_virtio_driver(virtio_mmc_driver);
MODULE_DEVICE_TABLE(virtio, id_table);
