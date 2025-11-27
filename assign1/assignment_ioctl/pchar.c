 /*
 * pchar.c
 * Simple pseudo char device driver using kfifo buffer
 */

#include <linux/module.h>
#include <linux/kfifo.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/slab.h>      /* kmalloc, kfree */
#include <linux/uaccess.h>   /* copy_to_user, copy_from_user */
#include "pchar_ioctl.h"

static int pchar_open(struct inode *pinode, struct file *pfile);
static int pchar_close(struct inode *pinode, struct file *pfile);
static ssize_t pchar_write(struct file *pfile, const char __user *ubuf, size_t ubufsize, loff_t *poffset);
static ssize_t pchar_read(struct file *pfile, char __user *ubuf, size_t ubufsize, loff_t *poffset);
static long pchar_ioctl(struct file *pfile, unsigned int cmd, unsigned long param);

/* global variables */
#define MAX 32

static struct kfifo buffer;
static dev_t devno;
static struct class *pclass;
static struct cdev pchar_cdev;

static const struct file_operations pchar_fops = {
    .owner = THIS_MODULE,
    .open = pchar_open,
    .release = pchar_close,
    .write = pchar_write,
    .read = pchar_read,
    .unlocked_ioctl = pchar_ioctl,
};

static int __init pchar_init(void)
{
    int ret;
    struct device *pdevice;

    pr_info("%s: pchar_init() called.\n", THIS_MODULE->name);

    /* 1) allocate device number */
    ret = alloc_chrdev_region(&devno, 0, 1, "pchar");
    if (ret < 0) {
        pr_err("%s: alloc_chrdev_region() failed (%d)\n", THIS_MODULE->name, ret);
        return ret;
    }

    /* 2) create class (kernel 6.x requires THIS_MODULE) */
    pclass = class_create("pchar_class");
    if (IS_ERR(pclass)) {
        pr_err("%s: class_create() failed\n", THIS_MODULE->name);
        ret = PTR_ERR(pclass);
        unregister_chrdev_region(devno, 1);
        return ret;
    }

    /* 3) create device node /dev/pchar0 */
    pdevice = device_create(pclass, NULL, devno, NULL, "pchar%d", 0);
    if (IS_ERR(pdevice)) {
        pr_err("%s: device_create() failed\n", THIS_MODULE->name);
        ret = PTR_ERR(pdevice);
        class_destroy(pclass);
        unregister_chrdev_region(devno, 1);
        return ret;
    }

    /* 4) init and add cdev */
    cdev_init(&pchar_cdev, &pchar_fops);
    ret = cdev_add(&pchar_cdev, devno, 1);
    if (ret < 0) {
        pr_err("%s: cdev_add() failed (%d)\n", THIS_MODULE->name, ret);
        device_destroy(pclass, devno);
        class_destroy(pclass);
        unregister_chrdev_region(devno, 1);
        return ret;
    }

    /* 5) allocate kfifo */
    ret = kfifo_alloc(&buffer, MAX, GFP_KERNEL);
    if (ret) {
        pr_err("%s: kfifo_alloc() failed (%d)\n", THIS_MODULE->name, ret);
        cdev_del(&pchar_cdev);
        device_destroy(pclass, devno);
        class_destroy(pclass);
        unregister_chrdev_region(devno, 1);
        return ret;
    }

    pr_info("%s: pchar driver loaded. /dev/pchar0 created (fifo size=%d)\n", THIS_MODULE->name, MAX);
    return 0;
}

static void __exit pchar_exit(void)
{
    pr_info("%s: pchar_exit() called.\n", THIS_MODULE->name);

    /* free fifo, cdev and device/class, unregister region */
    kfifo_free(&buffer);
    cdev_del(&pchar_cdev);
    device_destroy(pclass, devno);
    class_destroy(pclass);
    unregister_chrdev_region(devno, 1);

    pr_info("%s: pchar driver unloaded.\n", THIS_MODULE->name);
}

static int pchar_open(struct inode *pinode, struct file *pfile)
{
    pr_info("%s: pchar_open() called.\n", THIS_MODULE->name);
    return 0;
}

static int pchar_close(struct inode *pinode, struct file *pfile)
{
    pr_info("%s: pchar_close() called.\n", THIS_MODULE->name);
    return 0;
}

static ssize_t pchar_write(struct file *pfile, const char __user *ubuf, size_t ubufsize, loff_t *poffset)
{
    int nbytes = 0;
    int ret;

    pr_info("%s: pchar_write() called (req=%zu)\n", THIS_MODULE->name, ubufsize);

    if (kfifo_is_full(&buffer))
        return -ENOSPC;

    ret = kfifo_from_user(&buffer, ubuf, ubufsize, &nbytes);
    if (ret < 0) {
        pr_err("%s: kfifo_from_user() failed (%d)\n", THIS_MODULE->name, ret);
        return ret;
    }

    return nbytes;
}

static ssize_t pchar_read(struct file *pfile, char __user *ubuf, size_t ubufsize, loff_t *poffset)
{
    int nbytes = 0;
    int ret;

    pr_info("%s: pchar_read() called (req=%zu)\n", THIS_MODULE->name, ubufsize);

    if (kfifo_is_empty(&buffer))
        return 0;

    ret = kfifo_to_user(&buffer, ubuf, ubufsize, &nbytes);
    if (ret < 0) {
        pr_err("%s: kfifo_to_user() failed (%d)\n", THIS_MODULE->name, ret);
        return ret;
    }

    return nbytes;
}

static long pchar_ioctl(struct file *pfile, unsigned int cmd, unsigned long param)
{
    struct fifo_info info;
    int ret = 0;

    switch (cmd) {

    case FIFO_CLEAR:
        kfifo_reset(&buffer);
        pr_info("%s: ioctl - FIFO_CLEAR\n", THIS_MODULE->name);
        return 0;

    case FIFO_GET_INFO:
        info.size   = kfifo_size(&buffer);
        info.length = kfifo_len(&buffer);
        info.avail  = kfifo_avail(&buffer);

        ret = copy_to_user((void __user *)param, &info, sizeof(info));
        if (ret) {
            pr_err("%s: ioctl FIFO_GET_INFO - copy_to_user failed (%d)\n", THIS_MODULE->name, ret);
            return -EFAULT;
        }

        pr_info("%s: ioctl - FIFO_GET_INFO (size=%d length=%d avail=%d)\n",
                THIS_MODULE->name, info.size, info.length, info.avail);
        return 0;

    case FIFO_RESIZE: {
        int new_size = (int)param;
        int old_len;
        unsigned char *temp = NULL;
        int copied;

        pr_info("%s: ioctl - FIFO_RESIZE requested size=%d\n", THIS_MODULE->name, new_size);

        if (new_size <= 0) {
            pr_err("%s: ioctl FIFO_RESIZE - invalid size %d\n", THIS_MODULE->name, new_size);
            return -EINVAL;
        }

        
        old_len = kfifo_len(&buffer);
        if (old_len > 0) {
            temp = kmalloc(old_len, GFP_KERNEL);
            if (!temp) {
                pr_err("%s: ioctl FIFO_RESIZE - kmalloc failed\n", THIS_MODULE->name);
                return -ENOMEM;
            }

            
            copied = kfifo_out(&buffer, temp, old_len);
        } else {
            copied = 0;
        }

        
        kfifo_free(&buffer);

        
        ret = kfifo_alloc(&buffer, new_size, GFP_KERNEL);
        if (ret) {
            pr_err("%s: ioctl FIFO_RESIZE - kfifo_alloc failed (%d)\n", THIS_MODULE->name, ret);
            if (temp)
                kfree(temp);
            return ret;
        }

        
        if (copied > 0 && temp) {
            int copy_len = (copied < new_size) ? copied : new_size;
            kfifo_in(&buffer, temp, copy_len);
            kfree(temp);
        }

        pr_info("%s: ioctl FIFO_RESIZE - resized to %d (restored=%d)\n",
                THIS_MODULE->name, new_size, copied);
        return 0;
    }

    default:
        pr_err("%s: ioctl - invalid cmd 0x%x\n", THIS_MODULE->name, cmd);
        return -EINVAL;
    }

    /* unreachable, but keep compiler happy */
    return 0;
}

module_init(pchar_init);
module_exit(pchar_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Abhishek Shukla>");
MODULE_DESCRIPTION("Simple Pseudo Char Device Driver using kfifo buffer");
