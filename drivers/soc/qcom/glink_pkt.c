// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2021, The Linux Foundation. All rights reserved.
 * Ported to Linux 6.8.12 by Particle (yyt-notes/bugfix-modem-at_cmd_no_smd_dev.md).
 *
 * Out-of-tree downstream driver that exposes MPSS GLINK packet channels as
 * /dev/smd* character devices for userspace AT / data communication with the
 * integrated modem. Used as a workaround for the mainline qcom_glink_native
 * destroy_ept deadlock against wwan_core ops_lock on close.
 *
 * Cleaned up vs the original FAE port: dropped legacy TIOCM / signals path
 * (mainline rpmsg_driver has no signals callback), dropped the lazy
 * register / unregister path (enable_ch_close was always false), normalised
 * the driver-name macro.
 */

#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/rpmsg.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/idr.h>
#include <linux/of.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/completion.h>
#include <linux/refcount.h>

#define GLINK_PKT_INFO(dev, fmt, ...) \
	dev_dbg(dev, "[%s]: " fmt, __func__, ##__VA_ARGS__)
#define GLINK_PKT_ERR(dev, fmt, ...) \
	dev_err(dev, "[%s]: " fmt, __func__, ##__VA_ARGS__)

#define GLINK_PKT_IOCTL_MAGIC (0xC3)
#define GLINK_PKT_IOCTL_QUEUE_RX_INTENT \
	_IOW(GLINK_PKT_IOCTL_MAGIC, 0, unsigned int)

#define GLINK_PKT_DRV_NAME "glink_pkt"

static dev_t glink_pkt_major;
static struct class *glink_pkt_class;
static int num_glink_pkt_devs;
static DEFINE_IDA(glink_pkt_minor_ida);

struct glink_pkt_device {
	struct device dev;
	struct cdev cdev;
	struct rpmsg_driver drv;

	struct mutex lock;
	struct completion ch_open;
	refcount_t refcount;
	struct rpmsg_device *rpdev;

	spinlock_t queue_lock;
	struct sk_buff_head queue;
	wait_queue_head_t readq;
	bool fragmented_read;
	const char *dev_name;
	const char *ch_name;
	const char *edge;
	int open_tout;
	struct sk_buff *rskb;
	unsigned char *rdata;
	size_t rdata_len;
};

#define dev_to_gpdev(_dev) container_of(_dev, struct glink_pkt_device, dev)
#define cdev_to_gpdev(_cdev) container_of(_cdev, struct glink_pkt_device, cdev)
#define drv_to_rpdrv(_drv) container_of(_drv, struct rpmsg_driver, drv)
#define rpdrv_to_gpdev(_rdrv) container_of(_rdrv, struct glink_pkt_device, drv)

static ssize_t open_timeout_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t n)
{
	struct glink_pkt_device *gpdev = dev_to_gpdev(dev);
	long tmp;

	mutex_lock(&gpdev->lock);
	if (kstrtol(buf, 0, &tmp)) {
		mutex_unlock(&gpdev->lock);
		GLINK_PKT_ERR(dev, "unable to convert string to int for /dev/%s\n",
			      gpdev->dev_name);
		return -EINVAL;
	}
	gpdev->open_tout = tmp;
	mutex_unlock(&gpdev->lock);

	return n;
}

static ssize_t open_timeout_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct glink_pkt_device *gpdev = dev_to_gpdev(dev);
	ssize_t ret;

	mutex_lock(&gpdev->lock);
	ret = scnprintf(buf, PAGE_SIZE, "%d\n", gpdev->open_tout);
	mutex_unlock(&gpdev->lock);

	return ret;
}

static DEVICE_ATTR_RW(open_timeout);

static int glink_pkt_rpdev_probe(struct rpmsg_device *rpdev)
{
	struct device_driver *drv = rpdev->dev.driver;
	struct rpmsg_driver *rpdrv = drv_to_rpdrv(drv);
	struct glink_pkt_device *gpdev = rpdrv_to_gpdev(rpdrv);

	mutex_lock(&gpdev->lock);
	gpdev->rpdev = rpdev;
	mutex_unlock(&gpdev->lock);

	dev_set_drvdata(&rpdev->dev, gpdev);
	complete_all(&gpdev->ch_open);

	return 0;
}

static int glink_pkt_rpdev_cb(struct rpmsg_device *rpdev, void *buf, int len,
			      void *priv, u32 addr)
{
	struct glink_pkt_device *gpdev = dev_get_drvdata(&rpdev->dev);
	unsigned long flags;
	struct sk_buff *skb;

	if (!gpdev) {
		GLINK_PKT_ERR(&rpdev->dev, "channel is in reset\n");
		return -ENETRESET;
	}

	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, buf, len);

	spin_lock_irqsave(&gpdev->queue_lock, flags);
	skb_queue_tail(&gpdev->queue, skb);
	spin_unlock_irqrestore(&gpdev->queue_lock, flags);

	wake_up_interruptible(&gpdev->readq);

	return 0;
}

static void glink_pkt_rpdev_remove(struct rpmsg_device *rpdev)
{
	struct device_driver *drv = rpdev->dev.driver;
	struct rpmsg_driver *rpdrv = drv_to_rpdrv(drv);
	struct glink_pkt_device *gpdev = rpdrv_to_gpdev(rpdrv);

	mutex_lock(&gpdev->lock);
	gpdev->rpdev = NULL;
	mutex_unlock(&gpdev->lock);

	dev_set_drvdata(&rpdev->dev, NULL);

	reinit_completion(&gpdev->ch_open);
	wake_up_interruptible(&gpdev->readq);
}

static int glink_pkt_open(struct inode *inode, struct file *file)
{
	struct glink_pkt_device *gpdev = cdev_to_gpdev(inode->i_cdev);
	int tout = msecs_to_jiffies(gpdev->open_tout * 1000);
	struct device *dev = &gpdev->dev;
	int ret;

	refcount_inc(&gpdev->refcount);
	get_device(dev);

	GLINK_PKT_INFO(dev, "begin for %s by %s:%d ref_cnt[%d]\n",
		       gpdev->ch_name, current->comm,
		       task_pid_nr(current), refcount_read(&gpdev->refcount));

	ret = wait_for_completion_interruptible_timeout(&gpdev->ch_open, tout);
	if (ret <= 0) {
		refcount_dec(&gpdev->refcount);
		put_device(dev);
		GLINK_PKT_INFO(dev, "timeout for %s by %s:%d\n", gpdev->ch_name,
			       current->comm, task_pid_nr(current));
		return -ETIMEDOUT;
	}
	file->private_data = gpdev;

	GLINK_PKT_INFO(dev, "end for %s by %s:%d ref_cnt[%d]\n",
		       gpdev->ch_name, current->comm,
		       task_pid_nr(current), refcount_read(&gpdev->refcount));

	return 0;
}

static int glink_pkt_release(struct inode *inode, struct file *file)
{
	struct glink_pkt_device *gpdev = cdev_to_gpdev(inode->i_cdev);
	struct device *dev = &gpdev->dev;
	struct sk_buff *skb;
	unsigned long flags;

	GLINK_PKT_INFO(dev, "for %s by %s:%d ref_cnt[%d]\n",
		       gpdev->ch_name, current->comm,
		       task_pid_nr(current), refcount_read(&gpdev->refcount));

	refcount_dec(&gpdev->refcount);
	if (refcount_read(&gpdev->refcount) == 1) {
		spin_lock_irqsave(&gpdev->queue_lock, flags);

		if (gpdev->rskb) {
			kfree_skb(gpdev->rskb);
			gpdev->rskb = NULL;
			gpdev->rdata = NULL;
			gpdev->rdata_len = 0;
		}

		while (!skb_queue_empty(&gpdev->queue)) {
			skb = skb_dequeue(&gpdev->queue);
			kfree_skb(skb);
		}
		wake_up_interruptible(&gpdev->readq);
		spin_unlock_irqrestore(&gpdev->queue_lock, flags);
	}

	put_device(dev);

	return 0;
}

static ssize_t glink_pkt_read(struct file *file,
			char __user *buf, size_t count, loff_t *ppos)
{
	struct glink_pkt_device *gpdev = file->private_data;
	struct device *dev = &gpdev->dev;
	unsigned long flags;
	int use;

	if (!gpdev || refcount_read(&gpdev->refcount) == 1) {
		GLINK_PKT_ERR(dev, "invalid device handle\n");
		return -EINVAL;
	}

	if (!completion_done(&gpdev->ch_open)) {
		GLINK_PKT_ERR(dev, "%s channel in reset\n", gpdev->ch_name);
		return -ENETRESET;
	}

	GLINK_PKT_INFO(dev, "begin for %s by %s:%d ref_cnt[%d], remaining[%zu], count[%zu]\n",
		       gpdev->ch_name, current->comm,
		       task_pid_nr(current), refcount_read(&gpdev->refcount),
		       gpdev->rdata_len, count);

	spin_lock_irqsave(&gpdev->queue_lock, flags);
	if (skb_queue_empty(&gpdev->queue) && !gpdev->rskb) {
		spin_unlock_irqrestore(&gpdev->queue_lock, flags);

		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible(gpdev->readq,
					     !skb_queue_empty(&gpdev->queue) ||
					     !completion_done(&gpdev->ch_open)))
			return -ERESTARTSYS;

		if (!completion_done(&gpdev->ch_open))
			return -ENETRESET;

		spin_lock_irqsave(&gpdev->queue_lock, flags);
	}

	if (!gpdev->rskb) {
		gpdev->rskb = skb_dequeue(&gpdev->queue);
		if (!gpdev->rskb) {
			spin_unlock_irqrestore(&gpdev->queue_lock, flags);
			return -EFAULT;
		}
		gpdev->rdata = gpdev->rskb->data;
		gpdev->rdata_len = gpdev->rskb->len;
	}
	spin_unlock_irqrestore(&gpdev->queue_lock, flags);

	use = min_t(size_t, count, gpdev->rdata_len);

	if (copy_to_user(buf, gpdev->rdata, use))
		use = -EFAULT;

	if (!gpdev->fragmented_read && gpdev->rdata_len == use) {
		struct sk_buff *skb = gpdev->rskb;

		spin_lock_irqsave(&gpdev->queue_lock, flags);
		gpdev->rskb = NULL;
		gpdev->rdata = NULL;
		gpdev->rdata_len = 0;
		spin_unlock_irqrestore(&gpdev->queue_lock, flags);

		kfree_skb(skb);
	} else {
		struct sk_buff *skb = NULL;

		spin_lock_irqsave(&gpdev->queue_lock, flags);
		gpdev->rdata += use;
		gpdev->rdata_len -= use;
		if (gpdev->rdata_len == 0) {
			skb = gpdev->rskb;
			gpdev->rskb = NULL;
			gpdev->rdata = NULL;
			gpdev->rdata_len = 0;
		}
		spin_unlock_irqrestore(&gpdev->queue_lock, flags);
		if (skb)
			kfree_skb(skb);
	}

	GLINK_PKT_INFO(dev, "end for %s by %s:%d ret[%d], remaining[%zu]\n",
		       gpdev->ch_name, current->comm,
		       task_pid_nr(current), use, gpdev->rdata_len);

	return use;
}

static ssize_t glink_pkt_write(struct file *file,
			const char __user *buf, size_t count, loff_t *ppos)
{
	struct glink_pkt_device *gpdev = file->private_data;
	struct device *dev = &gpdev->dev;
	void *kbuf;
	int ret;

	if (!gpdev || refcount_read(&gpdev->refcount) == 1) {
		GLINK_PKT_ERR(dev, "invalid device handle\n");
		return -EINVAL;
	}

	GLINK_PKT_INFO(dev, "begin to %s buffer_size %zu\n", gpdev->ch_name, count);
	kbuf = vmemdup_user(buf, count);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	if (mutex_lock_interruptible(&gpdev->lock)) {
		ret = -ERESTARTSYS;
		goto free_kbuf;
	}
	if (!completion_done(&gpdev->ch_open) || !gpdev->rpdev) {
		GLINK_PKT_ERR(dev, "%s channel in reset\n", gpdev->ch_name);
		ret = -ENETRESET;
		goto unlock_ch;
	}

	if (file->f_flags & O_NONBLOCK)
		ret = rpmsg_trysend(gpdev->rpdev->ept, kbuf, count);
	else
		ret = rpmsg_send(gpdev->rpdev->ept, kbuf, count);

unlock_ch:
	mutex_unlock(&gpdev->lock);

free_kbuf:
	kvfree(kbuf);
	GLINK_PKT_INFO(dev, "finish to %s ret %d\n", gpdev->ch_name, ret);
	return ret < 0 ? ret : count;
}

static __poll_t glink_pkt_poll(struct file *file, poll_table *wait)
{
	struct glink_pkt_device *gpdev = file->private_data;
	struct device *dev = &gpdev->dev;
	__poll_t mask = 0;
	unsigned long flags;

	if (!gpdev || refcount_read(&gpdev->refcount) == 1) {
		GLINK_PKT_ERR(dev, "invalid device handle\n");
		return EPOLLERR;
	}
	if (!completion_done(&gpdev->ch_open)) {
		GLINK_PKT_ERR(dev, "%s channel in reset\n", gpdev->ch_name);
		return EPOLLHUP | EPOLLPRI;
	}

	poll_wait(file, &gpdev->readq, wait);

	mutex_lock(&gpdev->lock);

	if (!completion_done(&gpdev->ch_open) || !gpdev->rpdev) {
		GLINK_PKT_ERR(dev, "%s channel reset after wait\n", gpdev->ch_name);
		mutex_unlock(&gpdev->lock);
		return EPOLLHUP;
	}

	spin_lock_irqsave(&gpdev->queue_lock, flags);
	if (!skb_queue_empty(&gpdev->queue) || gpdev->rskb)
		mask |= EPOLLIN | EPOLLRDNORM;
	spin_unlock_irqrestore(&gpdev->queue_lock, flags);

	mutex_unlock(&gpdev->lock);

	return mask;
}

/*
 * Mainline 6.x rpmsg_driver has no signals callback, so we cannot implement
 * modem control lines (DTR/DCD/RI/etc) on top of GLINK. Only the legacy
 * QUEUE_RX_INTENT ioctl is kept as a no-op for ABI compatibility with old
 * userspace tooling; TIOCM* requests fall back to -ENOIOCTLCMD and userspace
 * gracefully degrades.
 */
static long glink_pkt_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	struct glink_pkt_device *gpdev = file->private_data;
	struct device *dev = &gpdev->dev;
	int ret = 0;

	if (!gpdev || refcount_read(&gpdev->refcount) == 1) {
		GLINK_PKT_ERR(dev, "invalid device handle\n");
		return -EINVAL;
	}
	if (mutex_lock_interruptible(&gpdev->lock))
		return -ERESTARTSYS;

	if (!completion_done(&gpdev->ch_open)) {
		GLINK_PKT_ERR(dev, "%s channel in reset\n", gpdev->ch_name);
		mutex_unlock(&gpdev->lock);
		return -ENETRESET;
	}

	switch (cmd) {
	case GLINK_PKT_IOCTL_QUEUE_RX_INTENT:
		ret = 0;
		break;
	default:
		GLINK_PKT_ERR(dev, "unrecognized ioctl command 0x%x\n", cmd);
		ret = -ENOIOCTLCMD;
	}

	mutex_unlock(&gpdev->lock);

	return ret;
}

static const struct file_operations glink_pkt_fops = {
	.owner = THIS_MODULE,
	.open = glink_pkt_open,
	.release = glink_pkt_release,
	.read = glink_pkt_read,
	.write = glink_pkt_write,
	.poll = glink_pkt_poll,
	.unlocked_ioctl = glink_pkt_ioctl,
	.compat_ioctl = glink_pkt_ioctl,
};

static ssize_t name_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct glink_pkt_device *gpdev = dev_to_gpdev(dev);

	return scnprintf(buf, RPMSG_NAME_SIZE, "%s\n", gpdev->ch_name);
}
static DEVICE_ATTR_RO(name);

static struct attribute *glink_pkt_device_attrs[] = {
	&dev_attr_name.attr,
	NULL,
};
ATTRIBUTE_GROUPS(glink_pkt_device);

static int glink_pkt_parse_devicetree(struct device_node *np,
				      struct glink_pkt_device *gpdev)
{
	const char *key;
	int ret;

	key = "qcom,glinkpkt-edge";
	ret = of_property_read_string(np, key, &gpdev->edge);
	if (ret < 0)
		goto error;

	key = "qcom,glinkpkt-ch-name";
	ret = of_property_read_string(np, key, &gpdev->ch_name);
	if (ret < 0)
		goto error;

	key = "qcom,glinkpkt-dev-name";
	ret = of_property_read_string(np, key, &gpdev->dev_name);
	if (ret < 0)
		goto error;

	key = "qcom,glinkpkt-fragmented-read";
	gpdev->fragmented_read = of_property_read_bool(np, key);

	GLINK_PKT_INFO(&gpdev->dev,
		"Parsed %s:%s /dev/%s fragmented read:%d\n",
		gpdev->edge, gpdev->ch_name, gpdev->dev_name,
		gpdev->fragmented_read);

	return 0;

error:
	GLINK_PKT_ERR(&gpdev->dev, "missing key: %s\n", key);
	return ret;
}

static void glink_pkt_release_device(struct device *dev)
{
	struct glink_pkt_device *gpdev = dev_to_gpdev(dev);

	GLINK_PKT_INFO(dev, "for %s by %s:%d ref_cnt[%d]\n",
		       gpdev->ch_name, current->comm,
		       task_pid_nr(current), refcount_read(&gpdev->refcount));

	ida_free(&glink_pkt_minor_ida, MINOR(gpdev->dev.devt));
	cdev_del(&gpdev->cdev);
	kfree(gpdev);
}

static int glink_pkt_init_rpmsg(struct glink_pkt_device *gpdev)
{
	struct rpmsg_driver *rpdrv = &gpdev->drv;
	struct device *dev = &gpdev->dev;
	struct rpmsg_device_id *match;
	char *drv_name;

	match = devm_kzalloc(dev, sizeof(*match), GFP_KERNEL);
	if (!match)
		return -ENOMEM;
	strscpy(match->name, gpdev->ch_name, RPMSG_NAME_SIZE);

	drv_name = devm_kasprintf(dev, GFP_KERNEL, "%s_%s", GLINK_PKT_DRV_NAME, gpdev->dev_name);
	if (!drv_name)
		return -ENOMEM;

	rpdrv->probe = glink_pkt_rpdev_probe;
	rpdrv->remove = glink_pkt_rpdev_remove;
	rpdrv->callback = glink_pkt_rpdev_cb;
	rpdrv->id_table = match;
	rpdrv->drv.name = drv_name;
	rpdrv->drv.owner = THIS_MODULE;

	return register_rpmsg_driver(rpdrv);
}

static int glink_pkt_create_device(struct device *parent,
				   struct device_node *np)
{
	struct glink_pkt_device *gpdev;
	struct device *dev;
	int ret, minor;

	gpdev = kzalloc(sizeof(*gpdev), GFP_KERNEL);
	if (!gpdev)
		return -ENOMEM;

	minor = ida_alloc(&glink_pkt_minor_ida, GFP_KERNEL);
	if (minor < 0) {
		kfree(gpdev);
		return minor;
	}

	dev = &gpdev->dev;

	ret = glink_pkt_parse_devicetree(np, gpdev);
	if (ret < 0) {
		GLINK_PKT_ERR(parent, "failed to parse dt ret:%d\n", ret);
		goto err_free_ida;
	}

	mutex_init(&gpdev->lock);
	refcount_set(&gpdev->refcount, 1);
	init_completion(&gpdev->ch_open);

	gpdev->open_tout = 120;

	spin_lock_init(&gpdev->queue_lock);

	gpdev->rskb = NULL;
	gpdev->rdata = NULL;
	gpdev->rdata_len = 0;

	skb_queue_head_init(&gpdev->queue);
	init_waitqueue_head(&gpdev->readq);

	device_initialize(dev);
	dev->class = glink_pkt_class;
	dev->parent = parent;
	dev->groups = glink_pkt_device_groups;
	dev_set_drvdata(dev, gpdev);

	cdev_init(&gpdev->cdev, &glink_pkt_fops);
	gpdev->cdev.owner = THIS_MODULE;

	dev->devt = MKDEV(MAJOR(glink_pkt_major), minor);
	dev_set_name(dev, "%s", gpdev->dev_name);

	ret = cdev_add(&gpdev->cdev, dev->devt, 1);
	if (ret) {
		GLINK_PKT_ERR(parent, "cdev_add failed for %s ret:%d\n",
			      gpdev->dev_name, ret);
		goto err_free_ida;
	}

	dev->release = glink_pkt_release_device;
	ret = device_add(dev);
	if (ret) {
		GLINK_PKT_ERR(parent, "device_add failed for %s ret:%d\n",
			      gpdev->dev_name, ret);
		goto err_del_cdev;
	}

	ret = device_create_file(dev, &dev_attr_open_timeout);
	if (ret)
		GLINK_PKT_ERR(parent, "device_create_file failed for %s\n",
			      gpdev->dev_name);

	ret = glink_pkt_init_rpmsg(gpdev);
	if (ret)
		goto err_del_device;

	return 0;

err_del_device:
	device_del(dev);
err_del_cdev:
	cdev_del(&gpdev->cdev);
err_free_ida:
	ida_free(&glink_pkt_minor_ida, minor);
	kfree(gpdev);
	return ret;
}

static void glink_pkt_remove_devices(struct device *parent)
{
	struct device *dev;
	struct glink_pkt_device *gpdev;

	while (1) {
		dev = device_find_child_by_name(parent, NULL);
		if (!dev)
			break;
		gpdev = dev_to_gpdev(dev);
		unregister_rpmsg_driver(&gpdev->drv);
		device_unregister(dev);
		put_device(dev);
	}
}

static void glink_pkt_deinit(void)
{
	class_destroy(glink_pkt_class);
	unregister_chrdev_region(MAJOR(glink_pkt_major), num_glink_pkt_devs);
}

static int glink_pkt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *cn;
	int ret;

	num_glink_pkt_devs = of_get_child_count(dev->of_node);
	if (num_glink_pkt_devs == 0)
		return -ENODEV;

	ret = alloc_chrdev_region(&glink_pkt_major, 0, num_glink_pkt_devs,
				  GLINK_PKT_DRV_NAME);
	if (ret < 0) {
		GLINK_PKT_ERR(dev, "alloc_chrdev_region failed ret:%d\n", ret);
		return ret;
	}

	glink_pkt_class = class_create(GLINK_PKT_DRV_NAME);
	if (IS_ERR(glink_pkt_class)) {
		ret = PTR_ERR(glink_pkt_class);
		GLINK_PKT_ERR(dev, "class_create failed ret:%d\n", ret);
		goto error_deinit;
	}

	for_each_child_of_node(dev->of_node, cn) {
		ret = glink_pkt_create_device(dev, cn);
		if (ret) {
			of_node_put(cn);
			GLINK_PKT_ERR(dev, "failed to create device for %pOF\n", cn);
			glink_pkt_remove_devices(dev);
			goto error_deinit;
		}
	}

	GLINK_PKT_INFO(dev, "G-Link Packet Port Driver Initialized\n");
	return 0;

error_deinit:
	glink_pkt_deinit();
	return ret;
}

static int glink_pkt_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	glink_pkt_remove_devices(dev);
	glink_pkt_deinit();

	return 0;
}

static const struct of_device_id glink_pkt_match_table[] = {
	{ .compatible = "qcom,glinkpkt" },
	{}
};
MODULE_DEVICE_TABLE(of, glink_pkt_match_table);

static struct platform_driver glink_pkt_driver = {
	.probe = glink_pkt_probe,
	.remove = glink_pkt_remove,
	.driver = {
		.name = GLINK_PKT_DRV_NAME,
		.of_match_table = glink_pkt_match_table,
	},
};

module_platform_driver(glink_pkt_driver);

MODULE_DESCRIPTION("MSM G-Link Packet Port (Linux 6.8.12 port)");
MODULE_LICENSE("GPL v2");