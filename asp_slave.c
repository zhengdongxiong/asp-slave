// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * GXMicro lijiangriver i2c client driver
 *
 * Copyright(C) 2022 GXMicro (ShangHai) Corp.
 *
 * Author:
 *      Zheng DongXiong <zhengdongxiong@gxmicro.cn>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>


#define DRV_NAME		"alt-slave"
#define ALERT_SLAVE_MINOR	169

/* reserve */
#define CPU_MSG_LEN		(16 + 2)
#define DDR_MSG_LEN		(16 + 2)
#define MSG_LEN			(16 + 2)

/* read函数传输数据 */
struct alt_msg_recv {
	uint8_t cpu_msg[MSG_LEN];
	uint8_t ddr_msg[MSG_LEN];
}__packed;

enum aspeed_state {
	ASPEED_RECV_CPU_MSG,
	ASPEED_RECV_DDR_MSG,
	ASPEED_IDLE,
	ASPEED_INVAL_CMD,
};

#define COMPLETE_FLAG		(BIT(ASPEED_IDLE) - 1)

struct aspeed_dev {
	struct miscdevice miscdev;
	struct completion recv_complete;

	spinlock_t lock;
	enum aspeed_state state;

	struct asp_msg_recv *msg;
	uint8_t msg_index;
};

#if 0
static inline int asp_xfer_msg(struct aspeed_dev *asp_dev, void __user *arg)
{
#if 0
	asp_dev->user_read = true;
#endif

	wait_for_completion(&asp_dev->recv_complete);

	return copy_to_user(arg, asp_dev->msg, sizeof(struct asp_msg_recv));
}

static long asp_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	struct aspeed_dev *asp_dev = container_of(fp->private_data, struct aspeed_dev, miscdev);

	switch (cmd) {
	case ASP_IOC_R:
		return asp_xfer_msg(asp_dev, (void __user *)arg);
	default :
		return -EINVAL;
	}
}
#endif
static ssize_t asp_read(struct file *fp, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct aspeed_dev *asp_dev = container_of(fp->private_data, struct aspeed_dev, miscdev);

	return copy_to_user(user_buf, asp_dev->msg, count);
}


static const struct file_operations misc_fops = {
	.owner = THIS_MODULE,
	.read = asp_read,
};

static void aspeed_recv_msg(struct aspeed_dev *asp_dev, uint8_t value)
{
	uint8_t *msg;

	if (asp_dev->state == ASPEED_IDLE) {
		if (likely(value < ASPEED_IDLE))
			asp_dev->state = value;
		else
			asp_dev->state = ASPEED_INVAL_CMD;
		return ;
	}

	switch (asp_dev->state) {
	case ASPEED_RECV_CPU_MSG :
		msg = asp_dev->msg->cpu_msg;
		break;
	case ASPEED_RECV_DDR_MSG :
		msg = asp_dev->msg->ddr_msg;
		break;
	default :
		return ;
	}

	msg[asp_dev->msg_index++ % MSG_LEN] = value;

	return ;
}

#ifdef DEBUG
static void dump_msg_8bytes(struct i2c_client *client)
{
	struct aspeed_dev *asp_dev = i2c_get_clientdata(client);
	struct asp_msg_recv *msg = asp_dev->msg;

	dev_info(&client->dev, "CPU info, len 0x%02x, msg 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
		msg->cpu_msg[0], msg->cpu_msg[1], msg->cpu_msg[2], msg->cpu_msg[3],
		msg->cpu_msg[4], msg->cpu_msg[5], msg->cpu_msg[6], msg->cpu_msg[7]);

	dev_info(&client->dev, "DDR info, len 0x%02x, msg 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
		msg->ddr_msg[0], msg->ddr_msg[1], msg->ddr_msg[2], msg->ddr_msg[3],
		msg->ddr_msg[4], msg->ddr_msg[5], msg->ddr_msg[6], msg->ddr_msg[7]);
}
#endif

static int asp_slave_cb(struct i2c_client *client, enum i2c_slave_event event, u8 *val)
{
	struct aspeed_dev *asp_dev = i2c_get_clientdata(client);
	unsigned long flags;

	spin_lock_irqsave(&asp_dev->lock, flags);

	switch (event) {
	case I2C_SLAVE_WRITE_RECEIVED :
		aspeed_recv_msg(asp_dev, *val);
		break;
	case I2C_SLAVE_STOP :
#ifdef DEBUG
		dump_msg_8bytes(client);
#endif
		fallthrough;
	case I2C_SLAVE_WRITE_REQUESTED :
		asp_dev->msg_index = 0;
		asp_dev->state = ASPEED_IDLE;
		fallthrough;
	/* not support I2C_SLAVE_READ_REQUESTED, I2C_SLAVE_READ_PROCESSED */
	default :
		break;
	}


out_slave_cb :
	spin_unlock_irqrestore(&asp_dev->lock, flags);

	dev_dbg(&client->dev, "event 0x%02x, value 0x%02x\n", event, *val);

	return 0;
}

static int asp_slave_probe(struct i2c_client *client)
{
	struct aspeed_dev *asp_dev;
	int ret = 0;

	asp_dev = devm_kzalloc(&client->dev, sizeof(struct aspeed_dev), GFP_KERNEL);
	if (!asp_dev)
		return -ENOMEM;

	asp_dev->msg = devm_kzalloc(&client->dev, sizeof(struct asp_msg_recv), GFP_KERNEL);
	if (!asp_dev->msg)
		return -ENOMEM;

	spin_lock_init(&asp_dev->lock);

	asp_dev->state = ASPEED_IDLE;
	i2c_set_clientdata(client, asp_dev);

	client->flags |= I2C_CLIENT_SLAVE;

	ret = i2c_slave_register(client, asp_slave_cb);
	if (ret)
		goto probe_out;

	asp_dev->miscdev.name = DRV_NAME;
	asp_dev->miscdev.minor = ASPEED_SLAVE_MINOR;
	asp_dev->miscdev.fops = &misc_fops;

	ret = misc_register(&asp_dev->miscdev);
	if (ret)
		goto err_misc_register;

	dev_info(&client->dev, "aspeed slave addr 0x%02x, register misc %s\n", client->addr, asp_dev->miscdev.name);

	return 0;

err_misc_register:
	i2c_slave_unregister(client);
probe_out:
	return ret;
}

static int asp_slave_remove(struct i2c_client *client)
{
	struct aspeed_dev *asp_dev = i2c_get_clientdata(client);

	misc_deregister(&asp_dev->miscdev);

	i2c_slave_unregister(client);

	i2c_set_clientdata(client, NULL);

	return 0;
}


static const struct i2c_device_id asp_slave_id[] = {
	{ DRV_NAME, 0 },
	{ /* END OF LIST */ }
};
MODULE_DEVICE_TABLE(i2c, asp_slave_id);

static const struct of_device_id asp_slave_of_table[] = {
	{ .compatible = "gxmicro,asp-slave", },
	{ /* END OF LIST */ }
};
MODULE_DEVICE_TABLE(of, asp_slave_of_table);

static struct i2c_driver asp_slave_driver = {
	.driver = {
		.name = DRV_NAME,
		//.of_match_table = asp_slave_of_table,
	},
	.probe_new = asp_slave_probe,
	.remove = asp_slave_remove,
	.id_table = asp_slave_id,
};
module_i2c_driver(asp_slave_driver);


MODULE_DESCRIPTION("GXMicro lijiangriver i2c client driver");
MODULE_AUTHOR("Zheng DongXiong <zhengdongxiong@gxmicro.cn>");
MODULE_VERSION("v0.1");
MODULE_LICENSE("GPL v2");
