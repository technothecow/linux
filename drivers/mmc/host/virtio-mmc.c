#include "virtio-mmc.h"
#include "asm-generic/int-ll64.h"
#include "linux/kern_levels.h"
#include "linux/mmc/host.h"
#include "linux/mmc/mmc.h"
#include "linux/printk.h"
#include "linux/scatterlist.h"
#include "linux/virtio_config.h"
#include <linux/virtio.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>

struct virtio_mmc_req {
	u32 opcode;
	u32 arg;
	u32 flags;
	u32 blocks;
	u32 blksz;
};

typedef struct virtio_mmc_data {
	struct virtio_device *vdev;
	struct mmc_host *mmc;
	struct virtqueue *vq;
	struct mmc_request *last_mrq;

	struct scatterlist sg;
	struct virtio_mmc_req req;
	u8 response;

	dev_t devt;
	struct cdev cdev;
} virtio_mmc_data;

static void virtio_mmc_print_binary(const char *name, void *data, size_t size) {
	printk(KERN_INFO "%s: ", name);
	for(int i = 0; i < size; i++) {
		unsigned char byte = ((unsigned char *)data)[i];
		for (int j = 7; j >= 0; j--) {
			printk(KERN_CONT "%d", (byte >> j) & 1);
		}
		printk(KERN_CONT " ");
	}
	printk(KERN_CONT "%d", *(u32*)data);
	printk(KERN_CONT "\n");
}

static void virtio_mmc_request(struct mmc_host *mmc, struct mmc_request *mrq) {
	struct virtio_mmc_data *data = mmc_priv(mmc);
	if(!data) {
		printk(KERN_CRIT "request: No data in mmc_priv\n");
		return;
	}

	printk(KERN_INFO "\nMMC Request details:\n");
	if(mrq->cmd) {
		virtio_mmc_print_binary("Opcode", &mrq->cmd->opcode, sizeof(u32));
		virtio_mmc_print_binary("Arg", &mrq->cmd->arg, sizeof(u32));
		virtio_mmc_print_binary("Flags", &mrq->cmd->flags, sizeof(u32));
		data->req.opcode = mrq->cmd->opcode;
		data->req.arg = mrq->cmd->arg;
	} else {
		printk(KERN_INFO "Command: NULL\n");
	}
	if(mrq->data) {
		printk(KERN_INFO "Data blocks: %u\n", mrq->data->blocks);
		printk(KERN_INFO "Data block size: %u\n", mrq->data->blksz);
		printk(KERN_INFO "Data flags: %x\n", mrq->data->flags);
		data->req.blocks = mrq->data->blocks;
		data->req.blksz = mrq->data->blksz;
		data->req.flags = mrq->data->flags;
	} else {
		printk(KERN_INFO "Data: NULL\n");
	}

	if (data->req.opcode == MMC_SEND_OP_COND) {
		printk(KERN_INFO "Sending response for MMC_SEND_OP_COND\n");
		mrq->cmd->resp[0] = 0xFFFFFFFF;
		return;
	}

	struct scatterlist sg_out_linux, sg_in_linux;
	sg_init_one(&sg_out_linux, &data->req, sizeof(struct virtio_mmc_req));
	sg_init_one(&sg_in_linux, &data->response, sizeof(u8));

	struct scatterlist *request[] = {&sg_out_linux, &sg_in_linux};

	if (virtqueue_add_sgs(data->vq, request, 1, 1, &data->response, GFP_KERNEL) < 0) {
		printk(KERN_CRIT "virtqueue_add_sgs failed\n");
		return;
	}

	printk(KERN_INFO "virtqueue_kick\n");
	virtqueue_kick(data->vq);
	data->last_mrq = mrq;
}

static void virtio_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios) {
	printk(KERN_INFO "virtio_mmc_set_ios\n");
	if(!ios) {
		printk(KERN_CRIT "virtio_mmc_set_ios: No ios\n");
		return;
	}

	// printk(KERN_INFO "Bus width: %d\n", ios->bus_width);
	// printk(KERN_INFO "Clock: %d\n", ios->clock);
	// printk(KERN_INFO "Power: %d\n", ios->power_mode);
	// printk(KERN_INFO "VDD: %d\n", ios->vdd);
}

static int virtio_mmc_get_ro(struct mmc_host *mmc) {
	printk(KERN_INFO "virtio_mmc_get_ro\n");
	return 0;
}

static int virtio_mmc_get_cd(struct mmc_host *mmc) {
	printk(KERN_INFO "virtio_mmc_get_cd\n");
	return 1;
}

static void virtio_mmc_enable_sdio_irq(struct mmc_host *mmc, int enable) {
	printk(KERN_INFO "virtio_mmc_enable_sdio_irq, enable = %d", enable);
}

static int virtio_mmc_start_signal_voltage_switch(struct mmc_host *mmc, struct mmc_ios *ios) {
	printk(KERN_INFO "virtio_mmc_start_signal_voltage_switch\n");
	if(!ios) {
		printk(KERN_CRIT "virtio_mmc_set_ios: No ios\n");
		return 1;
	}

	// printk(KERN_INFO "Bus width: %d\n", ios->bus_width);
	// printk(KERN_INFO "Clock: %d\n", ios->clock);
	// printk(KERN_INFO "Power: %d\n", ios->power_mode);
	// printk(KERN_INFO "VDD: %d\n", ios->vdd);
	return 0;
}

static const struct mmc_host_ops virtio_mmc_host_ops = {
	.request = virtio_mmc_request,
	.set_ios = virtio_mmc_set_ios,
	.get_ro = virtio_mmc_get_ro,
	.get_cd = virtio_mmc_get_cd,
	.enable_sdio_irq = virtio_mmc_enable_sdio_irq,
	.start_signal_voltage_switch = virtio_mmc_start_signal_voltage_switch,
};

static void virtio_mmc_vq_callback(struct virtqueue *vq) {
	printk(KERN_INFO "virtio_mmc_vq_callback\n");
	virtio_mmc_data *data = vq->vdev->priv;
	unsigned int len;

	u8 *response = virtqueue_get_buf(vq, &len);
	if(!response) {
		printk(KERN_ERR "virtio_mmc_vq_callback: No response\n");
		return;
	}

	data->response = *response;
	data->last_mrq->cmd->resp[0] = data->response;
	// data->last_mrq->cmd->error = 0;
	mmc_request_done(data->mmc, data->last_mrq);
	printk(KERN_INFO "virtio_mmc_vq_callback: request done\n");
}

static int create_host(struct virtio_device *vdev) {
	int err;

	struct mmc_host *host = mmc_alloc_host(sizeof(struct virtio_mmc_data), &vdev->dev);
	host->ops = &virtio_mmc_host_ops;
	host->f_min = 100000;
	host->f_max = 52000000;
	host->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;
	host->caps = MMC_CAP_4_BIT_DATA | MMC_CAP_SDIO_IRQ | MMC_CAP_MMC_HIGHSPEED;
	host->caps2 = MMC_CAP2_NO_SDIO | MMC_CAP2_NO_SD;

	struct virtio_mmc_data *data = mmc_priv(host);

	data->vq = virtio_find_single_vq(vdev, virtio_mmc_vq_callback, "vq_name");
	if (!data->vq) {
		printk(KERN_ERR "Failed to find virtqueue\n");
		mmc_free_host(host);
		return -ENODEV;
	}
	printk(KERN_INFO "virtio_mmc: virtqueue found\n");

	err = mmc_add_host(host);
	if (err) {
		printk(KERN_ERR "Failed to add host\n");
		mmc_free_host(host);
		return err;
	}
	
	vdev->priv = host;

	return 0;
}

static void remove_host(struct mmc_host *host) {
	mmc_remove_host(host);
	mmc_free_host(host);
}

static int virtio_mmc_probe(struct virtio_device *vdev) {
	int err;
	printk(KERN_INFO "virtio_mmc_probe\n");

	err = create_host(vdev);
	if(err) {
		printk(KERN_ERR "Failed to make host\n");
	}
	printk(KERN_INFO "virtio_mmc_probe: mmc host created\n");

	// struct virtio_mmc_data *data = mmc_priv(vdev->priv);
	

	printk(KERN_INFO "virtio_mmc_probe finished\n");
	return 0;

// remove_host:
// 	remove_host(data->mmc);

	return err;
}

static void virtio_mmc_remove(struct virtio_device *vdev) {
	printk(KERN_INFO "virtio_mmc_remove\n");

	struct mmc_host *host = vdev->priv;
	remove_host(host);
}