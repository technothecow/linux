#include "virtio-mmc.h"
#include "asm-generic/int-ll64.h"
#include "linux/completion.h"
#include "linux/kern_levels.h"
#include "linux/mmc/host.h"
#include "linux/printk.h"
#include "linux/scatterlist.h"
#include "linux/types.h"
#include "linux/virtio_config.h"
#include <linux/virtio.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>

static DECLARE_COMPLETION(request_handled);

typedef struct mmc_req {
	u32 opcode;
	u32 arg;
	u32 flags;
} mmc_req;

typedef struct virtio_mmc_req {
	mmc_req request;
	bool is_data;
	bool is_write;
	u8 buf[4096];
	size_t len;
	bool is_stop;
	mmc_req stop_req;
} virtio_mmc_req;

typedef struct virtio_mmc_resp {
	u32 response[4];
	int resp_len;
	u8 buf[4096];
} virtio_mmc_resp;

typedef struct virtio_mmc_data {
	struct virtio_device *vdev;
	struct mmc_host *mmc;
	struct virtqueue *vq;
	struct mmc_request *last_mrq;

	struct scatterlist sg;
	struct sg_mapping_iter miter;
	virtio_mmc_req req;
	virtio_mmc_resp response;

	dev_t devt;
	struct cdev cdev;
} virtio_mmc_data;

static void virtio_mmc_send_request_to_qemu(virtio_mmc_data *data)
{
	struct scatterlist sg_out_linux, sg_in_linux;
	sg_init_one(&sg_out_linux, &data->req, sizeof(struct virtio_mmc_req));
	sg_init_one(&sg_in_linux, &data->response, sizeof(struct virtio_mmc_resp));

	struct scatterlist *request[] = { &sg_out_linux, &sg_in_linux };

	if (virtqueue_add_sgs(data->vq, request, 1, 1, &data->response,
			      GFP_KERNEL) < 0) {
		printk(KERN_CRIT "virtqueue_add_sgs failed\n");
		return;
	}

	virtqueue_kick(data->vq);
	wait_for_completion(&request_handled);
}

static void virtio_mmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	virtio_mmc_data *data = mmc_priv(mmc);
	if (!data) {
		printk(KERN_CRIT "virtio_mmc_request: No data\n");
		return;
	}
	data->last_mrq = mrq;

	if (mrq->cmd) {
		data->req.request.opcode = mrq->cmd->opcode;
		data->req.request.arg = mrq->cmd->arg;
		data->req.request.flags = mrq->cmd->flags;
	} else {
		printk(KERN_INFO "Command: NULL\n");
	}
	if (mrq->data) {
		data->req.is_data = true;
		// data->req.flags = mrq->data->flags; // TODO: check if anything changes, they might be the same as cmd flags
		if (mrq->data->flags & MMC_DATA_WRITE) {
			data->req.is_write = true;
			size_t len = 0;
			for (int i = 0; i < mrq->data->sg_len; i++)
				len += mrq->data->sg[i].length;
			sg_copy_to_buffer(mrq->data->sg, mrq->data->sg_len, &data->req.buf, len);
			data->req.len = len;
		} else {
			data->req.is_write = false;
			data->req.len = mrq->data->blksz*mrq->data->blocks;
		}
	} else {
		data->req.is_data = false;
	}
	if(mrq->stop) {
		data->req.is_stop = true;
		data->req.stop_req.opcode = mrq->stop->opcode;
		data->req.stop_req.arg = mrq->stop->arg;
		data->req.stop_req.flags = mrq->stop->flags;
	} else {
		data->req.is_stop = false;
	}

	virtio_mmc_send_request_to_qemu(data);
}

static void virtio_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	printk(KERN_INFO "virtio_mmc_set_ios\n");
}

static int virtio_mmc_get_ro(struct mmc_host *mmc)
{
	printk(KERN_INFO "virtio_mmc_get_ro\n");
	return 0;
}

static int virtio_mmc_get_cd(struct mmc_host *mmc)
{
	printk(KERN_INFO "virtio_mmc_get_cd\n");
	return 1;
}

static const struct mmc_host_ops virtio_mmc_host_ops = {
	.request = virtio_mmc_request,
	.set_ios = virtio_mmc_set_ios,
	.get_ro = virtio_mmc_get_ro,
	.get_cd = virtio_mmc_get_cd,
};

static void virtio_mmc_vq_callback(struct virtqueue *vq)
{
	unsigned int len;
	struct mmc_host *host = vq->vdev->priv;
	virtio_mmc_data *data = mmc_priv(host);
	virtio_mmc_resp *response = virtqueue_get_buf(vq, &len);

	for (int i = 0; i < response->resp_len / 4; i++) {
		data->last_mrq->cmd->resp[i] = response->response[i];
	}
	// for (int i = 0; i < response->resp_len / 4; i++) {
	// 	printk(KERN_CONT "%x ", response->response[i]);
	// }
	// printk(KERN_CONT "\n");

	if (data->last_mrq->data && data->req.is_data) {
		struct mmc_request* mrq = data->last_mrq;
		if (data->req.is_write) {
			// printk(KERN_INFO "virtio_mmc_vq_callback: data write\n");
			mrq->data->bytes_xfered = mrq->data->blksz * mrq->data->blocks;
		} else {
			size_t len = 0;
			int i;

			for (i = 0; i < mrq->data->sg_len; i++) {
				len += mrq->data->sg[i].length;
			}
			// pr_info("virtio_mmc_vq_callback: data read, expected len: %zu\n", len);
			sg_copy_from_buffer(mrq->data->sg, mrq->data->sg_len, response->buf, len);
			mrq->data->bytes_xfered = len;
		}
	}

	mmc_request_done(host, data->last_mrq);
	data->last_mrq = NULL;

	complete(&request_handled);
}

static int create_host(struct virtio_device *vdev)
{
	int err;

	struct mmc_host *host =
		mmc_alloc_host(sizeof(struct virtio_mmc_data), &vdev->dev);
	vdev->priv = host;
	host->ops = &virtio_mmc_host_ops;
	host->f_min = 300000;
	host->f_max = 500000;
	host->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;
	host->caps = MMC_CAP_SD_HIGHSPEED;
	host->caps2 = MMC_CAP2_NO_SDIO | MMC_CAP2_NO_MMC | MMC_CAP2_HS400;
	// host->max_blk_count = 1;

	struct virtio_mmc_data *data = mmc_priv(host);

	data->vq =
		virtio_find_single_vq(vdev, virtio_mmc_vq_callback, "vq_name");
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

	return 0;
}

static void remove_host(struct mmc_host *host)
{
	mmc_remove_host(host);
	mmc_free_host(host);
}

static int virtio_mmc_probe(struct virtio_device *vdev)
{
	int err;

	err = create_host(vdev);
	if (err) {
		printk(KERN_ERR "Failed to make host\n");
	}

	// struct virtio_mmc_data *data = mmc_priv(vdev->priv);
	init_completion(&request_handled);

	return 0;

	// remove_host:
	// 	remove_host(data->mmc);

	return err;
}

static void virtio_mmc_remove(struct virtio_device *vdev)
{

	struct mmc_host *host = vdev->priv;
	remove_host(host);
}