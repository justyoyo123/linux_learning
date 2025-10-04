#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/wait.h>

#define DEVICE_NAME "simple_name"
#define CLASS_NAME "simple_class"
#define BUF_SIZE 4096

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Justin");
MODULE_DESCRIPTION("Circular Buffer + Blocking I/O");

struct circ_buf {
    dev_t dev_number;
    struct cdev c_dev;
    struct class *class;
    char buffer[BUF_SIZE];
    size_t head;
    size_t tail;
    struct mutex lock;
    wait_queue_head_t read_queue;
    wait_queue_head_t write_queue;
};

static struct circ_buf *buf_dev;

/* Forward declarations */
static int buf_open(struct inode *inode, struct file *filp);
static int buf_release(struct inode *inode, struct file *filp);
static ssize_t buf_read(struct file *filp, char __user *buff, size_t count, loff_t *offp);
static ssize_t buf_write(struct file *filp, const char __user *buff, size_t count, loff_t *offp);

static const struct file_operations buf_fops = {
    .owner = THIS_MODULE,
    .read = buf_read,
    .write = buf_write,
    .open = buf_open,
    .release = buf_release,
};

/* ------------------- File Operations ------------------- */

static int buf_open(struct inode *inode, struct file *filp)
{
    struct circ_buf *dev;
    dev = container_of(inode->i_cdev, struct circ_buf, c_dev);
    filp->private_data = dev;
    return 0;
}

static int buf_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static ssize_t buf_read(struct file *filp, char __user *buff, size_t count, loff_t *offp)
{
    struct circ_buf *dev = filp->private_data;
    size_t bytes_read = 0;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    while (dev->head == dev->tail) {
        mutex_unlock(&dev->lock);
        if (wait_event_interruptible(dev->read_queue, dev->head != dev->tail))
            return -ERESTARTSYS;
        if (mutex_lock_interruptible(&dev->lock))
            return -ERESTARTSYS;
    }

    while (bytes_read < count && dev->head != dev->tail) {
        if (put_user(dev->buffer[dev->tail], buff + bytes_read))
            break;

        dev->tail = (dev->tail + 1) % BUF_SIZE;
        bytes_read++;
    }

    mutex_unlock(&dev->lock);
    wake_up_interruptible(&dev->write_queue);
    return bytes_read;
}

static ssize_t buf_write(struct file *filp, const char __user *buff, size_t count, loff_t *offp)
{
    struct circ_buf *dev = filp->private_data;
    size_t bytes_written = 0;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    while (bytes_written < count) {
        size_t next = (dev->head + 1) % BUF_SIZE;

        /* Buffer full */
        if (next == dev->tail) {
            mutex_unlock(&dev->lock);
            if (wait_event_interruptible(dev->write_queue, (dev->head + 1) % BUF_SIZE != dev->tail))
                return bytes_written ? bytes_written : -ERESTARTSYS;
            if (mutex_lock_interruptible(&dev->lock))
                return -ERESTARTSYS;
            continue;
        }

        char c;
        if (get_user(c, buff + bytes_written))
            break;

        dev->buffer[dev->head] = c;
        dev->head = next;
        bytes_written++;
    }

    mutex_unlock(&dev->lock);
    wake_up_interruptible(&dev->read_queue);
    return bytes_written;
}

/* ------------------- Module Init/Exit ------------------- */

static int __init buf_init(void)
{
    int ret;

    buf_dev = kzalloc(sizeof(*buf_dev), GFP_KERNEL);
    if (!buf_dev)
        return -ENOMEM;

    mutex_init(&buf_dev->lock);
    init_waitqueue_head(&buf_dev->read_queue);
    init_waitqueue_head(&buf_dev->write_queue);

    ret = alloc_chrdev_region(&buf_dev->dev_number, 0, 1, DEVICE_NAME);
    if (ret) {
        pr_err("circ_buf: failed to alloc chardev region\n");
        kfree(buf_dev);
        return ret;
    }

    cdev_init(&buf_dev->c_dev, &buf_fops);
    buf_dev->c_dev.owner = THIS_MODULE;

    ret = cdev_add(&buf_dev->c_dev, buf_dev->dev_number, 1);
    if (ret) {
        unregister_chrdev_region(buf_dev->dev_number, 1);
        kfree(buf_dev);
        pr_err("circ_buf: failed to set up cdev\n");
        return ret;
    }

    buf_dev->class = class_create(CLASS_NAME);
    if (IS_ERR(buf_dev->class)) {
        cdev_del(&buf_dev->c_dev);
        unregister_chrdev_region(buf_dev->dev_number, 1);
        kfree(buf_dev);
        return PTR_ERR(buf_dev->class);
    }

    if (!device_create(buf_dev->class, NULL, buf_dev->dev_number, NULL, DEVICE_NAME)) {
        class_destroy(buf_dev->class);
        cdev_del(&buf_dev->c_dev);
        unregister_chrdev_region(buf_dev->dev_number, 1);
        kfree(buf_dev);
        return -1;
    }

    pr_info("circ_buf: registered. Major: %d, minor: %d\n",
            MAJOR(buf_dev->dev_number), MINOR(buf_dev->dev_number));
    return 0;
}

static void __exit buf_exit(void)
{
    device_destroy(buf_dev->class, buf_dev->dev_number);
    class_destroy(buf_dev->class);
    cdev_del(&buf_dev->c_dev);
    unregister_chrdev_region(buf_dev->dev_number, 1);
    kfree(buf_dev);
    pr_info("circ_buf: unloaded\n");
}

module_init(buf_init);
module_exit(buf_exit);

