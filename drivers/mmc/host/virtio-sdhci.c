// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  VirtIO SD/MMC driver
 *
 *  Author: Mikhail Krasheninnikov <krashmisha@gmail.com>
 */

#include "virtio-sdhci.h"
#include "linux/mmc/host.h"
#include "linux/virtio.h"
#include "linux/virtio_config.h"
#include "linux/completion.h"
#include "uapi/linux/virtio-sdhci.h"

struct virtio_sdhci_host {
	struct virtio_device *vdev;
	struct mmc_host *mmc;
	struct virtqueue *vq;
	struct mmc_request *current_request;

	struct virtio_mmc_request virtio_request;
	struct virtio_mmc_response virtio_response;

	struct completion request_handled;
	spinlock_t handling_request;
};

static void virtio_sdhci_vq_callback(struct virtqueue *vq)
{
	unsigned int len;
	struct mmc_host *mmc;
	struct virtio_sdhci_host *host;
	struct virtio_mmc_request *virtio_request;
	struct virtio_mmc_response *virtio_response;
	struct mmc_request *mrq;

	mmc = vq->vdev->priv;
	host = mmc_priv(mmc);
	mrq = host->current_request;
	virtio_request = &host->virtio_request;

	virtio_response = virtqueue_get_buf(vq, &len);

	if (!virtio_response) {
		return;
	}

	memcpy(mrq->cmd->resp, virtio_response->cmd_resp,
	       min(4 * (int)sizeof(u32), virtio_response->cmd_resp_len));

	if (virtio_request->flags & VIRTIO_MMC_REQUEST_DATA) {
		mrq->data->bytes_xfered =
			min((unsigned int)virtio_request->buf_len,
			    mmc->max_blk_size);

		if (!(virtio_request->flags & VIRTIO_MMC_REQUEST_WRITE)) {
			sg_copy_from_buffer(mrq->data->sg, mrq->data->sg_len,
					    virtio_response->buf,
					    mrq->data->bytes_xfered);
		}
	}

	host->current_request = NULL;
	mmc_request_done(mmc, mrq);
	complete(&host->request_handled);
}

static void virtio_sdhci_send_request_to_qemu(struct virtio_sdhci_host *data)
{
	struct scatterlist sg_out_linux, sg_in_linux;

	sg_init_one(&sg_out_linux, &data->virtio_request,
		    sizeof(struct virtio_mmc_request));
	sg_init_one(&sg_in_linux, &data->virtio_response,
		    sizeof(struct virtio_mmc_response));

	struct scatterlist *request[] = { &sg_out_linux, &sg_in_linux };

	if (virtqueue_add_sgs(data->vq, request, 1, 1, &data->virtio_response,
			      GFP_KERNEL) < 0) {
		dev_crit(&data->vdev->dev, "Failed to add sg\n");
		return;
	}

	virtqueue_kick(data->vq);
	wait_for_completion(&data->request_handled);
}

static inline size_t __calculate_len(struct mmc_data *data)
{
	size_t len = 0;

	for (int i = 0; i < data->sg_len; i++)
		len += data->sg[i].length;
	return len;
}

/* MMC layer callbacks */

static void virtio_sdhci_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct virtio_sdhci_host *host;
	struct virtio_mmc_request *virtio_req;
	struct mmc_data *mrq_data;

	host = mmc_priv(mmc);

	spin_lock(&host->handling_request);
	WARN_ON(host->current_request != NULL);

	host->current_request = mrq; // Saving the request for the callback

	virtio_req = &host->virtio_request;
	memset(virtio_req, 0, sizeof(struct virtio_mmc_request));

	virtio_req->request.opcode = mrq->cmd->opcode;
	virtio_req->request.arg = mrq->cmd->arg;

	mrq_data = mrq->data;
	if (mrq_data) {
		virtio_req->flags |= VIRTIO_MMC_REQUEST_DATA;

		virtio_req->buf_len = __calculate_len(mrq->data);

		virtio_req->flags |= ((mrq_data->flags & MMC_DATA_WRITE) ?
					      VIRTIO_MMC_REQUEST_WRITE :
					      0);
		if (virtio_req->flags & VIRTIO_MMC_REQUEST_WRITE) {
			sg_copy_to_buffer(mrq_data->sg, mrq_data->sg_len,
					  virtio_req->buf, virtio_req->buf_len);
		}
	}

	if (mrq->stop) {
		virtio_req->flags |= VIRTIO_MMC_REQUEST_STOP;

		virtio_req->stop_req.opcode = mrq->stop->opcode;
		virtio_req->stop_req.arg = mrq->stop->arg;
	}

	if (mrq->sbc) {
		virtio_req->flags |= VIRTIO_MMC_REQUEST_SBC;

		virtio_req->sbc_req.opcode = mrq->sbc->opcode;
		virtio_req->sbc_req.arg = mrq->sbc->arg;
	}

	virtio_sdhci_send_request_to_qemu(host);
	spin_unlock(&host->handling_request);
}

static void virtio_sdhci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
}

static int virtio_sdhci_get_ro(struct mmc_host *mmc)
{
	return 0;
}

static int virtio_sdhci_get_cd(struct mmc_host *mmc)
{
	return 1;
}

static const struct mmc_host_ops virtio_sdhci_host_ops = {
	.request = virtio_sdhci_request,
	.set_ios = virtio_sdhci_set_ios,
	.get_ro = virtio_sdhci_get_ro,
	.get_cd = virtio_sdhci_get_cd,
};

static inline void __fill_host_attr(struct mmc_host *host)
{
	host->ops = &virtio_sdhci_host_ops;
	host->f_min = 300000;
	host->f_max = 500000;
	host->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;
	host->caps = MMC_CAP_SD_HIGHSPEED;
	host->caps2 = MMC_CAP2_NO_SDIO | MMC_CAP2_NO_MMC | MMC_CAP2_HS400;
	host->max_blk_size = 4096;
}

static int create_host(struct virtio_device *vdev)
{
	pr_info("virtio_mmc: Creating host\n");
	int err;
	struct mmc_host *mmc;
	struct virtio_sdhci_host *host;

	mmc = mmc_alloc_host(sizeof(struct virtio_sdhci_host), &vdev->dev);
	if (!mmc) {
		pr_err("virtio_mmc: Failed to allocate host\n");
		return -ENOMEM;
	}

	__fill_host_attr(mmc);

	vdev->priv = mmc;

	host = mmc_priv(mmc);
	host->vdev = vdev;

	spin_lock_init(&host->handling_request);
	init_completion(&host->request_handled);

	host->vq =
		virtio_find_single_vq(vdev, virtio_sdhci_vq_callback, "vq_name");
	if (!host->vq) {
		pr_err("virtio_mmc: Failed to find virtqueue\n");
		mmc_free_host(mmc);
		return -ENODEV;
	}

	pr_info("virtio_mmc: Adding host\n");
	err = mmc_add_host(mmc);
	if (err) {
		pr_err("virtio_mmc: Failed to add host\n");
		mmc_free_host(mmc);
		return err;
	}

	return 0;
}

static int virtio_sdhci_probe(struct virtio_device *vdev)
{
	int err;

	err = create_host(vdev);
	if (err)
		pr_err("virtio_mmc: Failed to make host\n");

	return 0;
}

static void remove_mmc_host(struct mmc_host *host)
{
	mmc_remove_host(host);
	mmc_free_host(host);
}

static void virtio_sdhci_remove(struct virtio_device *vdev)
{
	pr_crit("virtio_mmc: Removing device\n");
	struct mmc_host *mmc = vdev->priv;
	struct virtio_sdhci_host *host = mmc_priv(mmc);

	pr_crit("virtio_mmc: Marking completion\n");
	complete(&host->request_handled);
	pr_crit("virtio_mmc: Removing host\n");
	remove_mmc_host(mmc);
	pr_crit("virtio_mmc: Resetting device\n");
	virtio_reset_device(vdev);
	pr_crit("virtio_mmc: Deleting virtqueues\n");
	vdev->config->del_vqs(vdev);
}
