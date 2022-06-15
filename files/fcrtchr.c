/*  fcrtchr.c - The simplest kernel module.

* Copyright (C) 2013 - 2016 Xilinx, Inc
*
*   This program is free software; you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation; either version 2 of the License, or
*   (at your option) any later version.

*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License along
*   with this program. If not, see <http://www.gnu.org/licenses/>.

*/
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/cdev.h> 	
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/dma-mapping.h>
#include <linux/uaccess.h>

#include "fcrt_define.h"
// #include "recvtask.h"

#define DEBUG_CHECK_PROBE_MODULE_CNT

///после отправки сообщения через паузу выводить статус контроллера
#define USE_FCRTSHOW
/* Standard module information, edit as appropriate */
MODULE_LICENSE("GPL");
MODULE_AUTHOR
    ("Xilinx Inc.");
MODULE_DESCRIPTION
    ("fcrtchr - loadable module template generated by petalinux-create -t modules");

#define DRIVER_NAME "fcrtchr"
#define FC_VER  "fcrt-1.0"
// #define DRIVER_NAME	"rcm-"FC_VER
#define FCS_DT_NODE "fcrt_ctrl"

#define DRIVER_VERSION "revision 1.0"

/* Simple example of how to receive command line parameters to your module.
   Delete if you don't need them */
///размер области озу для fcrt контроллера
unsigned int area_sz = 0x100000u;
///период в мсек
unsigned int ms_period = 1u;
/**
 * @details конфигурация закоротки. 0-сообщение отправляется в контроллер, закорачивается в нем и принимается драйвером; 1-сообщение принимается драйвером, закорачивается и отправляется в контроллер
 *
 */
#define LOOPBACK_IN_DEV 0
#define LOOPBACK_IN_DRV 1
unsigned int loopback_dir=LOOPBACK_IN_DEV;
char *mystr = "default";

module_param(area_sz, int, S_IRUGO);
module_param(ms_period, int, S_IRUGO);
module_param(loopback_dir, int, S_IRUGO);
module_param(mystr, charp, S_IRUGO);

#define FCRTCHR_DEV_MAX	1

static int fcrtchr_major;
static int fcrtchr_minor = 0;

struct fcrt_priv_mem
{
	u8 * base_m;
	dma_addr_t base_dma;
	u8 * cur_addr;
	dma_addr_t cur_dma;
	unsigned int total;
	fcrt_allocator alloc;
	fcrt_release rls;
	u32 bl_cntr;
};

struct fcrtchr_local
{
    int irq;
    unsigned long mem_start;
    unsigned long mem_end;
    void __iomem *base_addr;
    struct cdev c_dev;
    struct device *dev_parent;
    struct semaphore sem;
};

static irqreturn_t fcrtchr_irq(int irq, void *lp)
{
	printk("fcrtchr interrupt\n");
	return IRQ_HANDLED;
}

struct fcrt_priv_mem fcrt_mem;
void * fcrt_alloc(uint32_t align, unsigned int sz_b, dma_addr_t * dma_addr);
void fcrt_free(void * addr);

//////////////
 
static struct hrtimer g_hrtimer0;
static u64 nsec_time = (1000000u);

static u8 rcvBuf[MSG_MAX_LEN + 2];
static u8 sndBuf[MSG_MAX_LEN + 2];
 
static u32 send_msg_flag = 0;
static enum hrtimer_restart hrtimer_test_fn(struct hrtimer *hrtimer)
{
	u32 sz;
#ifdef USE_FCRTSHOW
	static u64 divider =0;
	if(send_msg_flag > 0)
	{
		divider++;
		if(divider == 100)
		{
			pr_err("#### hrtimer timeout: %d msec", divider);
			send_msg_flag = 0;
			divider = 0;
			fcrtShow(0, 0, 0);
		}
	}
#endif
	int vc = fcrtRxReady();
	if(vc > -1)
	{
		if(fcrtRecv(vc, rcvBuf, &sz) != 0)
		{
			printk(KERN_ERR"[%s]Can't read msg from vc[%d]", __func__, vc);
		}
		else
		{
			if(loopback_dir == LOOPBACK_IN_DRV)
			{
				if(fcrtSend(vc, rcvBuf, sz) != 0)
				{
					printk(KERN_ERR"[%s]Can't write msg to vc[%d]", __func__, vc);
				}
			}
			printk(KERN_INFO"msg rcvd vc[%d]", vc);
            print_hex_dump(KERN_INFO, "r_msg: ", DUMP_PREFIX_NONE, 32, 1, rcvBuf, sz,
                           true);
        }
	}
    hrtimer_forward_now(hrtimer, ns_to_ktime(nsec_time));
	return HRTIMER_RESTART;
}
 
int rtsk_init_task(u64 nsec_period)
{
	pr_err("#### hr_timer_test module init...\n");
	
	nsec_time = nsec_period;
	struct hrtimer *hrtimer = &g_hrtimer0;
	hrtimer_init(hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	hrtimer->function = hrtimer_test_fn;
	hrtimer_start(hrtimer, ns_to_ktime(nsec_period),
		      HRTIMER_MODE_REL_PINNED);
    
	return 0;
}
 
void hr_timer_test_exit (void)
{
	struct hrtimer *hrtimer = &g_hrtimer0;
	hrtimer_cancel(hrtimer);
    pr_err("#### hr_timer_test module exit...\n");
}
#define DST_ID_DEFAULT	0xDEDABABAu
#define BBNUM_DEF		2u
#define ASM_ID_FIRST_NUM	0x10000000u
#define NEXT_ASM_ID(N)	(ASM_ID_FIRST_NUM + (N))
#define FCRT_TX_FLG	0//(FCRT_FLAG_PHY_A | FCRT_FLAG_PHY_B)
#define FCRT_RX_FLG	0 //(FCRT_FLAG_PHY_A | FCRT_FLAG_PHY_B)
#define FCRT_TX_MSG_SZ	0x400u
#define FCRT_RX_MSG_SZ	0x400u
#define FCRT_PRD		0
#define FCRT_PRIO		0
#define FCRT_TX_QDEPTH  5
#define FCRT_RX_QDEPTH  5
/////////////////
///индекс вк для отправки
#define DEF_TX_VC			0
static FCRT_TX_DESC txd[] = {
	{.asm_id = NEXT_ASM_ID(0), .dst_id = DST_ID_DEFAULT, .flags = FCRT_TX_FLG, .max_size = FCRT_TX_MSG_SZ, .period = FCRT_PRD, .priority = FCRT_PRIO, .q_depth = FCRT_TX_QDEPTH},

	// {.asm_id = NEXT_ASM_ID(1), .dst_id = DST_ID_DEFAULT, .flags = FCRT_TX_FLG, .max_size = FCRT_TX_MSG_SZ, .period = FCRT_PRD, .priority = FCRT_PRIO, .q_depth = FCRT_TX_QDEPTH},

	// {.asm_id = NEXT_ASM_ID(2), .dst_id = DST_ID_DEFAULT, .flags = FCRT_TX_FLG, .max_size = FCRT_TX_MSG_SZ, .period = FCRT_PRD, .priority = FCRT_PRIO, .q_depth = FCRT_TX_QDEPTH},

	// {.asm_id = NEXT_ASM_ID(3), .dst_id = DST_ID_DEFAULT, .flags = FCRT_TX_FLG, .max_size = FCRT_TX_MSG_SZ, .period = FCRT_PRD, .priority = FCRT_PRIO, .q_depth = FCRT_TX_QDEPTH},

	// {.asm_id = NEXT_ASM_ID(4), .dst_id = DST_ID_DEFAULT, .flags = FCRT_TX_FLG, .max_size = FCRT_TX_MSG_SZ, .period = FCRT_PRD, .priority = FCRT_PRIO, .q_depth = FCRT_TX_QDEPTH},
};

///индекс вк для приема
#define DEF_RX_VC			0
static FCRT_RX_DESC rxd[] = {
	{.asm_id = NEXT_ASM_ID(0), .flags = FCRT_RX_FLG, .max_size = FCRT_RX_MSG_SZ, .q_depth = FCRT_RX_QDEPTH},

	// {.asm_id = NEXT_ASM_ID(101), .flags = FCRT_RX_FLG, .max_size = FCRT_RX_MSG_SZ, .q_depth = FCRT_RX_QDEPTH},

	// {.asm_id = NEXT_ASM_ID(102), .flags = FCRT_RX_FLG, .max_size = FCRT_RX_MSG_SZ, .q_depth = FCRT_RX_QDEPTH},

	// {.asm_id = NEXT_ASM_ID(103), .flags = FCRT_RX_FLG, .max_size = FCRT_RX_MSG_SZ, .q_depth = FCRT_RX_QDEPTH},

	// {.asm_id = NEXT_ASM_ID(104), .flags = FCRT_RX_FLG, .max_size = FCRT_RX_MSG_SZ, .q_depth = FCRT_RX_QDEPTH},
};

static FCRT_CTRL_CFG cfg = {
	.bbNum = BBNUM_DEF, .fc_id = DST_ID_DEFAULT
};

static void fcrt_txdesc_out(struct device * dev, FCRT_TX_DESC * dp)
{
    dev_info(dev,
             "TX[asm: %x]\tdst_id: %x\tprio: %d\tperiod: %d\tmaxsz: %x\tq_depth: %d\tflags: "
             "%x",
             dp->asm_id, dp->dst_id, dp->priority, dp->period, dp->max_size, dp->q_depth,
             dp->flags);
}

static void fcrt_rxdesc_out(struct device * dev, FCRT_RX_DESC * dp)
{
    dev_info(dev,
             "RX[asm: %x]\tmaxsz: %x\tq_depth: %d\tflags: "
             "%x", dp->asm_id, dp->max_size, dp->q_depth, dp->flags);
}

static void fcrt_conf_out(struct device * dev, FCRT_INIT_PARAMS * params)
{
	int i;
	dev_info(dev, "[%s]-----------", __func__);
	dev_info(dev, "CtrlRegs: %p\tVCnum: %d\tCtrlDesc. fc_id: %x\tbbNum: %d", params->regs, params->nVC, params->ctrl_d->fc_id, params->ctrl_d->bbNum);
	dev_info(dev, "---VC descriptors---");
	for(i = 0; i < params->nVC; i++)
	{
		fcrt_txdesc_out(dev, &params->txd[i]);
		fcrt_rxdesc_out(dev, &params->rxd[i]);
	}
}

/**
 * @brief выделяет блок последовательной когерентной памяти с указанным выравниванием
 * 
 * @param align выравнивание в байтах
 * @param sz_b размер в байтах
 * @param dma_addr физ адрес для доступа дма
 * @return void* указатель на блок или NULL
 */

void * fcrt_alloc(u32 sz_b, u32 align, dma_addr_t * dma_addr)
{
    uint8_t *cur_addr_aligned =
        (uint8_t *)ALIGN((platform_addr_type)fcrt_mem.cur_addr, align);
    dma_addr_t cur_dma_aligned = (dma_addr_t)ALIGN((dma_addr_t)fcrt_mem.cur_dma, align);

#if 1
	printk(KERN_INFO"[%s]---- mem alloc ----------", __func__);
    printk(KERN_INFO "mem_base. virt: %p; dma: %p", (void *)fcrt_mem.base_m,
           (void *)fcrt_mem.base_dma);
    printk(KERN_INFO "alloc cntr: %d", fcrt_mem.bl_cntr);
	printk(KERN_INFO"align: d%d(x%x); size: d%d(x%x)", align, align, sz_b, sz_b);
    printk(KERN_INFO "cur_addr. virt: %p\tdma: %p", fcrt_mem.cur_addr,
           (void *)fcrt_mem.cur_dma);
    printk(KERN_INFO "cur_addr aligned. virt: %p\tdma: %p", cur_addr_aligned,
           cur_dma_aligned);
#endif
	if(cur_addr_aligned + sz_b >= &fcrt_mem.base_m[fcrt_mem.total])
		return NULL;
	void * buf = (void*)cur_addr_aligned;
	*dma_addr = cur_dma_aligned;
	fcrt_mem.cur_addr = cur_addr_aligned + sz_b;
	fcrt_mem.cur_dma = cur_dma_aligned + sz_b;
	fcrt_mem.bl_cntr++;
#if 1
	printk(KERN_INFO"Allocated. v.addr: %p; dma addr: %p",  buf, (void*)*dma_addr);
	printk(KERN_INFO"--------------------");
#endif
	return buf;
}

/**
 * @brief освободить блок последовательной памяти
 * 
 * @param addr  указатель на блок
 */
void fcrt_free(void * addr)
{
	if(fcrt_mem.bl_cntr > 0)
	{
		fcrt_mem.bl_cntr--;
	}
	else
	{
		printk(KERN_WARNING"[%s]All blocks already released. Ignore", __func__);
	}
}

static int fcrt_init_priv_mem(struct device *dev, struct fcrt_priv_mem *mem,
                              unsigned int sz)
{
    struct fcrtchr_local *lp = dev_get_drvdata(dev);
    mem->bl_cntr = 0;
    mem->alloc = fcrt_alloc;
    mem->rls = fcrt_free;
	fcrt_mem.total = sz;

#if 1
    dev_info(dev, "[%s]Alloc memory size: 0x%x bytes", __func__, mem->total);
#endif
    if (dma_set_mask_and_coherent(dev, 0x7FFFFFFF))
    {
        dev_err(dev, "Couldnt set dma and coherent mask to 31 bits");
        return -ENOMEM;
    }
    // отдельную потоковую дма область.
    mem->base_m = dmam_alloc_coherent(dev, mem->total, &mem->base_dma, GFP_KERNEL);
    if (mem->base_m == NULL)
    {
        printk(KERN_ALERT "[%s]could not alloc fcrt private memory. Size: %x", __func__,
               mem->total);
        return -ENOMEM;
    }
	mem->cur_addr = mem->base_m;
	mem->cur_dma = mem->base_dma;
#if 1
    dev_info(dev, "[%s]Coherent mem. Base virt: %p; Base phys: %p; size: 0x%x", __func__,
             mem->base_m, (void *)mem->base_dma, mem->total);
#endif
    return 0;
}

static void fcrt_release_priv_mem(struct device * dev, struct fcrt_priv_mem * mem)
{
	if(mem->base_m == NULL)
		return;

	dmam_free_coherent(dev, mem->total, mem->base_m, mem->base_dma);
	mem->total = 0;
	mem->base_m = NULL;
	mem->base_dma  = 0;
	return;
}
//////////////////////////////

ssize_t fcrtchr_read(struct file *flip, char __user *buf, size_t count,
				loff_t *f_pos)
{
	struct fcrtchr_local *dev = flip->private_data;
	ssize_t rv = 0;
	unsigned int msg_sz;
	u32 vc;

	if (down_interruptible(&dev->sem))	
		return -ERESTARTSYS;

	printk(KERN_ERR "[%s] size to read: %lu", __func__, count);
	vc = fcrtRxReady();
	if(vc < 0)
	{
		printk(KERN_ERR "[%s]No data", __func__);
        rv = -EAGAIN;
		goto out;
	}
	if(fcrtRecv(vc, rcvBuf, &msg_sz) != 0)
    {
        rv = -EFAULT;
        goto out;
    }
	if(copy_to_user(buf, rcvBuf, msg_sz) != 0)
	{
		printk(KERN_ERR"[%s]could not copy message to buffer", __func__);
		rv = -ENOMEM;
		goto out;
	}
	rv = msg_sz;
#if 1
	printk(KERN_ERR "[%s]Msg size: %d", __func__, msg_sz);
	print_hex_dump(KERN_INFO, "msg: ", DUMP_PREFIX_NONE, 32, 1, buf, rv, true);
#endif

out:
	up(&dev->sem);
	return rv;
}

ssize_t fcrtchr_write(struct file *flip, const char __user *buf, size_t count,
					loff_t *f_pos)
{
	struct fcrtchr_local *dev = flip->private_data;
	ssize_t rv = -ENOMEM;

	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;

#if 1
	printk(KERN_ALERT "[%s]bytes to write: %lu; msg: ", __func__, count);
	print_hex_dump(KERN_ALERT, "usr_data: ", DUMP_PREFIX_NONE, 32, 1, buf, count, true);
#endif
	if(count > MSG_MAX_LEN)
	{
		printk(KERN_ERR"[%s]Message max size is: %d", __func__, MSG_MAX_LEN);
		rv = -ENOMEM;
		goto out;
	}
	if(copy_from_user(sndBuf, buf, count) != 0)
	{
		printk(KERN_ERR"[%s]Could not copy message for write", __func__);
		rv = -EFAULT;
		goto out;
	}
	if(fcrtSend(DEF_TX_VC, (void*)sndBuf, (unsigned int)count) != 0)
	{
		printk(KERN_ALERT"[%s]fcrtSend fails. VC[%d]", __func__, txd[0].asm_id);
		rv = -ENOMEM;
		goto out;
	}
	rv = count;
	send_msg_flag = 1;

out:
	up(&dev->sem);
	return rv;
}
int fcrtchr_open(struct inode *inode, struct file *flip)
{
	struct fcrtchr_local *dev;

	dev = container_of(inode->i_cdev, struct fcrtchr_local, c_dev); 
	flip->private_data = dev;

	if ((flip->f_flags & O_ACCMODE) == O_WRONLY) { 
		if (down_interruptible(&dev->sem))
			return -ERESTARTSYS;

		up(&dev->sem);
	}
	

	printk(KERN_INFO "fcrtchr: device is opend\n");
	return 0;
}

int fcrtchr_release(struct inode *inode, struct file *flip)
{
	return 0;
}

struct file_operations fcrtchr_fops = {		
	.owner = THIS_MODULE,			
	.read = fcrtchr_read,
	.write = fcrtchr_write,
	.open = fcrtchr_open,
	.release = fcrtchr_release,
};

static int fcrtchr_setup_cdev(struct fcrtchr_local *dev, int index)
{
	int ret, devno = MKDEV(fcrtchr_major, fcrtchr_minor + index);	

	cdev_init(&dev->c_dev, &fcrtchr_fops);
    dev->c_dev.owner = THIS_MODULE;

    ret = cdev_add(&dev->c_dev, devno, 1);
	return ret;
}

static int fcrtchr_probe(struct platform_device *pdev)
{
#ifdef DEBUG_CHECK_PROBE_MODULE_CNT
	static int init_cnt = 0;
	if(init_cnt > 0)
	{

		printk(KERN_ALERT"[%s]init cnt: %d", __func__, init_cnt);
		return -EEXIST;
	}
	init_cnt++;
#endif
    struct resource *r_irq; /* Interrupt resources */
    struct resource *r_mem; /* IO mem resources */
    struct device *dev = &pdev->dev;
    struct fcrtchr_local *lp = NULL;
	FCRT_INIT_PARAMS param;

    int rc = 0;
    dev_info(dev, "Device Tree Probing\n");
    /* Get iospace for the device */
    r_mem = platform_get_resource_byname(pdev, IORESOURCE_MEM, (FCS_DT_NODE));
    if (!r_mem)
    {
        dev_err(dev, "invalid address\n");
        return -ENODEV;
    }
    lp = (struct fcrtchr_local *)kzalloc(sizeof(struct fcrtchr_local), GFP_KERNEL);
    if (!lp)
    {
        dev_err(dev, "Cound not allocate fcrtchr device\n");
        return -ENOMEM;
    }
    lp->mem_start = r_mem->start;
    lp->mem_end = r_mem->end;
	lp->dev_parent = dev;
	
	// lp->dev.groups = qcom_lp_groups;

    // lp->base = devm_memremap(&lp->dev, lp->addr, lp->size, MEMREMAP_WC);
    // if (IS_ERR(lp->base))
    // {
    //     dev_err(&pdev->dev, "failed to remap lp region\n");
    //     ret = PTR_ERR(lp->base);
    //     goto put_device;
    // }

    if (!devm_request_mem_region(dev, lp->mem_start, resource_size(r_mem),
                                 // lp->mem_end - lp->mem_start + 1,
                                 DRIVER_NAME))
    {
        dev_err(dev, "Couldn't lock memory region at %p\n", (void *)lp->mem_start);
        rc = -EBUSY;
        goto error1;
    }

    lp->base_addr = devm_ioremap_nocache(dev, lp->mem_start, resource_size(r_mem));
    if (!lp->base_addr)
    {
        dev_err(dev, "fcrtchr: Could not allocate iomem\n");
        rc = -EIO;
        goto error2;
    }
    dev_info(dev, "fcrtchr at %p mapped to %p, size=%x\n",
             lp->mem_start, lp->base_addr,
             resource_size(r_mem));
    /* Get IRQ for the device */
    /*
    r_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
    if (!r_irq) {
        dev_info(dev, "no IRQ found\n");
        dev_info(dev, "fcrtchr at 0x%08x mapped to 0x%08x\n",
            (unsigned int __force)lp->mem_start,
            (unsigned int __force)lp->base_addr);
        goto error2;
        return 0;
    }
    lp->irq = r_irq->start;
    rc = request_irq(lp->irq, &fcrtchr_irq, 0, DRIVER_NAME, lp);
    if (rc) {
        dev_err(dev, "testmodule: Could not allocate interrupt %d.\n",
            lp->irq);
        goto error3;
    }
    */
	//setup device for cdev
	if(fcrtchr_setup_cdev(lp, 0) != 0)
	{
        dev_err(dev, "failed to setup cdev");
		rc = -EIO;
		goto error4;
	}
	else
		printk(KERN_INFO"setup cdev OK. major: %d; minor: %d", fcrtchr_major, fcrtchr_minor);
	sema_init(&lp->sem, 1);
	
	if(fcrt_init_priv_mem(dev, &fcrt_mem, area_sz) != 0)
	{
		printk(KERN_ERR"[%s]fcrt_init priv mem fails", __func__);
		rc = -ENOMEM;
		goto error5;
	}
	else
		printk(KERN_INFO"init priv mem is OK");

	param.fcrt_alloc = fcrt_mem.alloc;
	param.regs = lp->base_addr;
	param.dev  = dev;
	param.nVC = sizeof(txd) / sizeof(txd[0]);
	param.ctrl_d = &cfg;
	param.ctrl_d->fc_id = DST_ID_DEFAULT;
	param.ctrl_d->bbNum = BBNUM_DEF;
	param.txd = txd;
	param.rxd = rxd;
	printk(KERN_INFO"1");
	fcrt_conf_out(dev, &param);
	dev_info(dev, "[%s]Try to init FCRT dev", __func__);
#ifdef FCRT_INIT_LONG_PARAM
	rc = fcrtInit(lp->base_addr, &cfg, txd, rxd, param.nVC, param.fcrt_alloc);
#else
	rc = fcrtInit(&param);
#endif
	if(rc != 0)
	{
		printk(KERN_ALERT"[%s]fcrtInit fails", __func__);
		goto error6;
	}
	printk(KERN_INFO "fcrtch: major = %d minor = %d\n", fcrtchr_major, fcrtchr_minor);

	dev_set_drvdata(dev, lp);
	rtsk_init_task(ms_period * 1000 * 1000);
    return 0;
///////////////////////////
error6:
	cdev_del(&lp->c_dev);
error5:
error4:
    // free_irq(lp->irq, lp);
error3:
	devm_iounmap(dev, lp->base_addr);
	lp->base_addr = NULL;
error2:
    devm_release_mem_region(dev, lp->mem_start, resource_size(r_mem));
error1:
    kfree(lp);
    dev_set_drvdata(dev, NULL);
    return rc;
}

static int fcrtchr_remove(struct platform_device *pdev)
{
#if 1
	dev_info(&pdev->dev, "[%s]", __func__);
#endif
	struct device *dev = &pdev->dev;
	struct fcrtchr_local *lp = dev_get_drvdata(dev);
	hr_timer_test_exit();
	cdev_del(&lp->c_dev);
	// free_irq(lp->irq, lp);
	kfree(lp);
	dev_set_drvdata(dev, NULL);
	return 0;
}


#define CONFIG_OF
#ifdef CONFIG_OF
static struct of_device_id fcrtchr_of_match[] = {
	{ .compatible = "vendor,fcrtchr", },
	{ .compatible = "grek,"FC_VER },
        { .compatible = "st,m25p80"},
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, fcrtchr_of_match);
#else
# define fcrtchr_of_match
#endif


static struct platform_driver fcrtchr_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table	= fcrtchr_of_match,
	},
	.probe		= fcrtchr_probe,
	.remove		= fcrtchr_remove,
};

static int __init fcrtchr_init(void)
{
	dev_t dev;
	int rv;
#ifdef DEBUG_CHECK_PROBE_MODULE_CNT
	static int init_cnt = 0;
	if(init_cnt > 0)
	{

		printk(KERN_ALERT"[%s]init cnt: %d", __func__, init_cnt);
		return -EEXIST;
	}
	init_cnt++;
#endif

    rv = alloc_chrdev_region(&dev, fcrtchr_minor, FCRTCHR_DEV_MAX, "fcrtchr");
    if (rv)
    {
        printk(KERN_WARNING "fcrtchr: can't get major %d\n", fcrtchr_major);
        return rv;
    }
	fcrtchr_major = MAJOR(dev);
	printk(KERN_INFO"[%s]fcrtchr_major: %x", __func__, fcrtchr_major);
	printk(KERN_INFO"FCRTchrModule parameters were\nFCRT priv area sz: 0x%08x\nchk rx msg period: %d(msec)\ndata loopback dir: %s", area_sz, ms_period, (loopback_dir == LOOPBACK_IN_DEV) ? "loop data in device": "loop data in driver");

	return platform_driver_register(&fcrtchr_driver);
}


static void __exit fcrtchr_exit(void)
{
	dev_t devno = MKDEV(fcrtchr_major, fcrtchr_minor);
	unregister_chrdev_region(devno, FCRTCHR_DEV_MAX); 
	platform_driver_unregister(&fcrtchr_driver);
	printk(KERN_ALERT "FCRTchr. Goodbye module world.\n");
}

module_init(fcrtchr_init);
module_exit(fcrtchr_exit);
