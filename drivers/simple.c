#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#define DEVICE_NAME "my_name"
#define CLASS_NAME "my_class"
#define BUF_SIZE 4096

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Justin");
MODULE_DESCRIPTION("Simple character device driver");

struct simple_dev {
    dev_t dev_number;
    struct cdev c_dev;
    char buf[BUF_SIZE];
    struct class *class;
    size_t len;
    struct mutex lock;
};

static struct simple_dev *dev;

/* Forward declarations */
static int my_simple_open(struct inode *inode, struct file *filp);
static int simple_release(struct inode *inode, struct file *filp);
static ssize_t simple_read(struct file *filp, char __user *buff, size_t count, loff_t *offp);
static ssize_t simple_write(struct file *filp, const char __user *buff, size_t count, loff_t *offp);

static const struct file_operations simple_fops = {
    .owner = THIS_MODULE,
    .read = simple_read,
    .write = simple_write,
    .open = my_simple_open,
    .release = simple_release,
};

static int my_simple_open(struct inode* inode, struct file* filp) {
    struct simple_dev* d;
    d = container_of(inode->i_cdev, struct simple_dev, c_dev);
    filp->private_data = d;
    return 0;
}

static int simple_release(struct inode* inode, struct file* filp) {
    return 0;
}

static ssize_t simple_read(struct file* filp, char __user *buff, size_t count, loff_t* offp) {
    struct simple_dev* d = filp->private_data;
    size_t available, to_copy;

    mutex_lock(&d->lock);
    available = (d->len > *offp) ? (d->len - *offp) : 0;
    to_copy = (count < available) ? count : available;

    if (to_copy == 0) {
        mutex_unlock(&d->lock);
        return 0;
    }

    if (copy_to_user(buff, d->buf + *offp, to_copy)) {
        mutex_unlock(&d->lock);
        return -EFAULT;
    }

    *offp += to_copy;
    mutex_unlock(&d->lock);

    printk(KERN_INFO "simple: read %zu bytes (offp=%lld)\n", to_copy, (long long)*offp);
    return to_copy;
}

static ssize_t simple_write(struct file* filp, const char __user *buff, size_t count, loff_t* offp) {
    struct simple_dev* d = filp->private_data;
    size_t to_copy;

    mutex_lock(&d->lock);
    to_copy = (count < BUF_SIZE - 1) ? count : BUF_SIZE - 1;

    if (copy_from_user(d->buf, buff, to_copy)) {
        mutex_unlock(&d->lock);
        return -EFAULT;
    }

    d->buf[to_copy] = '\0';
    d->len = to_copy;
    *offp = 0;

    mutex_unlock(&d->lock);
    printk(KERN_INFO "simple: wrote %zu bytes\n", to_copy);
    return to_copy;
}

static int __init simple_init(void) {
    int ret;

    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;
    mutex_init(&dev->lock);

    ret = alloc_chrdev_region(&dev->dev_number, 0, 1, DEVICE_NAME);
    if (ret) {
        printk(KERN_ERR "simple: failed to alloc chardev region\n");
        kfree(dev);
        return ret;
    }

    cdev_init(&dev->c_dev, &simple_fops);
    dev->c_dev.owner = THIS_MODULE;
    dev->c_dev.ops = &simple_fops;
    ret = cdev_add(&dev->c_dev, dev->dev_number, 1);
    if (ret) {
        unregister_chrdev_region(dev->dev_number, 1);
        kfree(dev);
        printk(KERN_ERR "simple: failed to set up cdev\n");
        return ret;
    }

    dev->class = class_create(CLASS_NAME);
    if (IS_ERR(dev->class)) {
        cdev_del(&dev->c_dev);
        unregister_chrdev_region(dev->dev_number, 1);
        kfree(dev);
        return PTR_ERR(dev->class);
    }

    if (device_create(dev->class, NULL, dev->dev_number, NULL, DEVICE_NAME) == NULL) {
        class_destroy(dev->class);
        cdev_del(&dev->c_dev);
        unregister_chrdev_region(dev->dev_number, 1);
        kfree(dev);
        return -1;
    }

    printk(KERN_INFO "simple: registered. Major: %d, Minor: %d\n",
           MAJOR(dev->dev_number), MINOR(dev->dev_number));

    return 0;
}

static void __exit simple_exit(void) {
    device_destroy(dev->class, dev->dev_number);
    class_destroy(dev->class);
    cdev_del(&dev->c_dev);
    unregister_chrdev_region(dev->dev_number, 1);
    kfree(dev);

    printk(KERN_INFO "simple: unloaded\n");
}

module_init(simple_init);
module_exit(simple_exit);

