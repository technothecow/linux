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

#define VIRTIO_MMC_DEV_ID 21

static struct virtio_device_id id_table[] = {
    { 
        .device = VIRTIO_MMC_DEV_ID,
        .vendor = VIRTIO_DEV_ANY_ID,
    },
    { 0 },
};

struct virtmmc_info {
    struct virtqueue *vq;
    uint32_t in, out;
};

static void vq_callback(struct virtqueue *vq) {
    struct virtmmc_info *info = vq->vdev->priv;
    uint32_t len;

    uint32_t* res = virtqueue_get_buf(vq, &len);
    if (!res) {
        pr_alert("virtio_mmc: failed to get buffer from vq\n");
        return;
    }

    info->in = *res;
}

static void virtio_mmc_store_in(struct virtmmc_info *info, const char *buf) {
    info->in = kstrtoul(buf, 10, (unsigned long *)&info->out);

    struct scatterlist sg_in, sg_out;
    sg_init_one(&sg_in, &info->in, sizeof(info->in));
    sg_init_one(&sg_out, &info->out, sizeof(info->out));

    struct scatterlist *request[] = { &sg_out, &sg_in };

    if (virtqueue_add_sgs(info->vq, request, 1, 1, &info->in, GFP_KERNEL) < 0) {
        pr_alert("virtio_mmc: failed to add scatterlist to vq\n");
        return;
    }

    virtqueue_kick(info->vq);
}

static ssize_t virtio_mmc_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
    struct virtio_device *vdev = dev_to_virtio(dev);
    struct virtmmc_info *info = vdev->priv;

    if (strcmp(attr->attr.name, "in") == 0) {
        virtio_mmc_store_in(info, buf);
        return count;
    }

    return -EINVAL;
}

static ssize_t virtio_mmc_show(struct device *dev, struct device_attribute *attr, char *buf) {
    struct virtio_device *vdev = dev_to_virtio(dev);
    struct virtmmc_info *info = vdev->priv;

    if (strcmp(attr->attr.name, "in") == 0) {
        return sprintf(buf, "%d\n", info->in);
    }

    return -EINVAL;
}

static DEVICE_ATTR(in, 0664, virtio_mmc_show, virtio_mmc_store);

struct attribute *virtio_mmc_attrs[] = {
    &dev_attr_in.attr,
    NULL
};

struct attribute_group virtio_mmc_attr_group = {
    .name = "virtio_mmc",
    .attrs = virtio_mmc_attrs,
};

static int virtio_mmc_probe(struct virtio_device *vdev) {
    struct virtmmc_info *info;

    {
        int ret = sysfs_create_group(&vdev->dev.kobj, &virtio_mmc_attr_group);
        if (ret) {
            pr_alert("virtio_mmc: failed to create sysfs group\n");
            return ret;
        }
    }

    info = kzalloc(1, sizeof(*info));
    if(!info) {
        return -ENOMEM;
    }

    info->vq = virtio_find_single_vq(vdev, vq_callback, "input");
    if(IS_ERR(info->vq)) {
        pr_alert("virtio_mmc: failed to connect to virtqueue\n");
    }

    info->in = 0;
    info->out = 0;

    vdev->priv = info;

    return 0;
}

static void virtio_mmc_remove(struct virtio_device *vdev) {
    struct virtmmc_info *info = vdev->priv;
    kfree(info);

    sysfs_remove_group(&vdev->dev.kobj, &virtio_mmc_attr_group);

    vdev->config->reset(vdev);
    vdev->config->del_vqs(vdev);
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

static int virtio_mmc_init(void)
{
    return register_virtio_driver(&virtio_mmc_driver);
}

static void __exit virtio_mmc_exit(void)
{
    unregister_virtio_driver(&virtio_mmc_driver);
}

module_init(virtio_mmc_init);
module_exit(virtio_mmc_exit);
