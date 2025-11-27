// Dealing with Multiple device instances
// Waiting Queue -- When device buffer is full, block the writer process.
// When data is read from device buffer, wakeup blocked writer process.

#include <linux/module.h>
#include <linux/kfifo.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/mutex.h>

// device operations
static int pchar_open(struct inode *pinode, struct file *pfile);
static int pchar_close(struct inode *pinode, struct file *pfile);
static ssize_t pchar_write(struct file *pfile, const char __user *ubuf, size_t ubufsize, loff_t *poffset);
static ssize_t pchar_read(struct file *pfile, char __user *ubuf, size_t ubufsize, loff_t *poffset);

// device & its related info -- device private struct
#define MAX 32
typedef struct pchar_device
{
    struct kfifo buffer;     // the device buffer
    struct cdev cdev;        // cdev struct for the device
    wait_queue_head_t wr_wq; // to block writer process, when buffer is full.
    struct mutex lock;
} pchar_device_t;

// number of devices -- flexible via module param
static int devcnt = 4;
module_param(devcnt, int, 0444);

// devices private struct dynamic array
static pchar_device_t *devices;
// other global variables
static dev_t devno;
static int major;
static struct class *pclass;
static struct file_operations pchar_fops = {
    .owner = THIS_MODULE,
    .open = pchar_open,
    .release = pchar_close,
    .write = pchar_write,
    .read = pchar_read,
};

static int __init pchar_init(void)
{
    int ret, i;
    struct device *pdevice;

    pr_info("%s: pchar_init() called.\n", THIS_MODULE->name);
    // allocate devices private struct dynamic array
    devices = (pchar_device_t *)kmalloc(devcnt * sizeof(pchar_device_t), GFP_KERNEL);
    if (devices == NULL)
    {
        ret = -ENOMEM;
        pr_err("%s: kmalloc() failed.\n", THIS_MODULE->name);
        goto kmalloc_failed;
    }
    pr_info("%s: kmalloc() allocated private struct for %d devices.\n", THIS_MODULE->name, devcnt);

    // allocate device number
    ret = alloc_chrdev_region(&devno, 0, devcnt, "pchar");
    if (ret < 0)
    {
        pr_err("%s: alloc_chrdev_region() failed.\n", THIS_MODULE->name);
        goto alloc_chrdev_region_failed;
    }
    major = MAJOR(devno);
    pr_info("%s: alloc_chrdev_region() allocated device num for %d devices from %d/%d.\n",
            THIS_MODULE->name, devcnt, major, MINOR(devno));

    // create device class
    pclass = class_create("pchar_class");
    if (IS_ERR(pclass))
    {
        ret = -1;
        pr_err("%s: class_create() failed.\n", THIS_MODULE->name);
        goto class_create_failed;
    }
    pr_info("%s: class_create() created pchar device class.\n", THIS_MODULE->name);

    // create device files
    for (i = 0; i < devcnt; i++)
    {
        dev_t devnum = MKDEV(major, i);
        pdevice = device_create(pclass, NULL, devnum, NULL, "pchar%d", i);
        if (IS_ERR(pdevice))
        {
            ret = -1;
            pr_err("%s: device_create() failed to created device file pchar%d.\n", THIS_MODULE->name, i);
            goto device_create_failed;
        }
        pr_info("%s: device_create() created device file pchar%d.\n", THIS_MODULE->name, i);
    }

    // init cdev for each device and add it in kernel
    for (i = 0; i < devcnt; i++)
    {
        dev_t devnum = MKDEV(major, i);
        cdev_init(&devices[i].cdev, &pchar_fops);
        ret = cdev_add(&devices[i].cdev, devnum, 1);
        if (ret < 0)
        {
            pr_err("%s: cdev_add() failed for pchar%d.\n", THIS_MODULE->name, i);
            goto cdev_add_failed;
        }
        pr_info("%s: cdev_add() added pchar%d cdev in kernel.\n", THIS_MODULE->name, i);
    }

    // alloc device buffers -- kfifos
    for (i = 0; i < devcnt; i++)
    {
        ret = kfifo_alloc(&devices[i].buffer, MAX, GFP_KERNEL);
        if (ret < 0)
        {
            pr_err("%s: kfifo_alloc() failed for pchar%d buffer.\n", THIS_MODULE->name, i);
            goto kfifo_alloc_failed;
        }
        pr_info("%s: kfifo_alloc() allocated buffer for pchar%d.\n", THIS_MODULE->name, i);
    }

    // initialize waiting queues
    for (i = 0; i < devcnt; i++)
    {
        init_waitqueue_head(&devices[i].wr_wq);
        pr_info("%s: init_waitqueue_head() initialized waiting queue for pchar%d.\n", THIS_MODULE->name, i);
    }

    // mutex
    for (i = 0; i < devcnt; i++)
    {
         mutex_init(&devices[i].lock);
         pr_info("%s: mutex_init() initialized for pchar%d.\n", THIS_MODULE->name, i);
    }

    return 0;

kfifo_alloc_failed:
    for (i = i - 1; i >= 0; i--)
        kfifo_free(&devices[i].buffer);
    i = devcnt;
cdev_add_failed:
    for (i = i - 1; i >= 0; i--)
        cdev_del(&devices[i].cdev);
    i = devcnt;
device_create_failed:
    for (i = i - 1; i >= 0; i--)
    {
        dev_t devnum = MKDEV(major, i);
        device_destroy(pclass, devnum);
    }
    class_destroy(pclass);
class_create_failed:
    unregister_chrdev_region(devno, devcnt);
alloc_chrdev_region_failed:
    kfree(devices);
kmalloc_failed:
    return ret;
}

static void __exit pchar_exit(void)
{
    int i;
    pr_info("%s: pchar_exit() called.\n", THIS_MODULE->name);
    for (i = devcnt - 1; i >= 0; i--)
    {
         mutex_destroy(&devices[i].lock);
         pr_info("%s: mutex_destroy() destroyed mutex for pchar%d.\n", THIS_MODULE->name, i);
    }
    // dealloc device buffers
    for (i = devcnt - 1; i >= 0; i--)
    {
        kfifo_free(&devices[i].buffer);
        pr_info("%s: kfifo_free() released device buffers pchar%d.\n", THIS_MODULE->name, i);
    }

    // delete cdev from kernel
    for (i = devcnt - 1; i >= 0; i--)
    {
        cdev_del(&devices[i].cdev);
        pr_info("%s: cdev_del() deleted pchar%d cdev from kernel.\n", THIS_MODULE->name, i);
    }

    // destroy device files
    for (i = devcnt - 1; i >= 0; i--)
    {
        dev_t devnum = MKDEV(major, i);
        device_destroy(pclass, devnum);
        pr_info("%s: device_destroy() destroyed device file pchar%d.\n", THIS_MODULE->name, i);
    }

    // destroy device class
    class_destroy(pclass);
    pr_info("%s: class_destroy() destroyed device class.\n", THIS_MODULE->name);

    // release device number
    unregister_chrdev_region(devno, devcnt);
    pr_info("%s: unregister_chrdev_region() released device numbers.\n", THIS_MODULE->name);

    // release devices private struct dynamic array
    kfree(devices);
    pr_info("%s: kfree() released private struct for %d devices.\n", THIS_MODULE->name, devcnt);
}

static int pchar_open(struct inode *pinode, struct file *pfile)
{
    pfile->private_data = container_of(pinode->i_cdev, pchar_device_t, cdev);
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
    pchar_device_t *dev = (pchar_device_t *)pfile->private_data;
    int nbytes, ret;
    pr_info("%s: pchar_write() called.\n", THIS_MODULE->name);
    
    // if buffer is full, block the writer process
    // the process will wake up when given cond is true i.e. kfifo is not full
    ret = wait_event_interruptible(dev->wr_wq, !kfifo_is_full(&dev->buffer));
    // process will wakeup when space is avail in buffer due to reading -- ret == 0
    // process will wakeup due to signal -- ret == ERESTARTSYS
    if (ret != 0)
    {
        pr_info("%s: process wakeup due to signal.\n", THIS_MODULE->name);
        return -ERESTARTSYS; // restart the syscall i.e. write()
    }
    ret = mutex_lock_interruptible(&dev->lock);
    ret = kfifo_from_user(&dev->buffer, ubuf, ubufsize, &nbytes);
    if (ret < 0)
    {
        pr_err("%s: kfifo_from_user() failed.\n", THIS_MODULE->name);
        return ret;
    }
    mutex_unlock(&dev->lock);
    return nbytes;
}

static ssize_t pchar_read(struct file *pfile, char __user *ubuf, size_t ubufsize, loff_t *poffset)
{
    pchar_device_t *dev = (pchar_device_t *)pfile->private_data;
    int nbytes, ret;
    pr_info("%s: pchar_read() called.\n", THIS_MODULE->name);
    ret = mutex_lock_interruptible(&dev->lock);
    ret = kfifo_to_user(&dev->buffer, ubuf, ubufsize, &nbytes);
    if (ret < 0)
    {
        pr_err("%s: kfifo_to_user() failed.\n", THIS_MODULE->name);
        return ret;
    }
    mutex_unlock(&dev->lock);
    // after reading a few bytes, wakeup blocked writer process (if any)
    if (nbytes > 0)
    {
        wake_up_interruptible(&dev->wr_wq);
        pr_info("%s: the blocked writer process is woken up.\n", THIS_MODULE->name);
    }
    return nbytes;
}

module_init(pchar_init);
module_exit(pchar_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Abhishek Shukla");
MODULE_DESCRIPTION("Simple Pseudo Char Device Driver Handling Multiple Device Instances");
