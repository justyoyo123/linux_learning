#include <linux/module.h>
#include <linux/init.h>
#include <linxu/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#define DEVICE_NAME "mychardev"
#define CLASS_NAME "mychardev_class"
#define BUFFER_SIZE 4096


struct mychar_dev {     
    static dev_t dev_number;
    static struct cdev my_cdev;
    static struct class *my_class;
    static char* kernel_buffer;
    static size_t data_size;
    struct mutex lock;
};


static int mychardev_open(struct inode *inode, struct file *filp) {
    pr_info("%s: open\n", DEVICE_NAME);
    
    struct mychar_dev *dev;
    dev = container_of(inode->i_cdev, struct mychar_dev, cdev);
    filp->private_data = dev;

    return 0;
}

static int mychardev_release() {

}

static const struct file_operations mychardev_fops {
    .owner = THIS_MODULE,
    .llseek = mychardev_llseek,
    .read = mychardev_read,
    .write = mychardev_write,
    .ioctl = mychardev_ioctl,
    .open = mychardev_open,
    .release = mychardev_release,
};

static int __init chardev_init(void) {
    int ret;

    ret = alloc_chrdev_region(&dev_nuymber, 0, 1, DEVICE_NAME);
    if(ret) {
        pr_err("%s: alloc_chrdev_region failed: %d\n", DEVICE_NAME, ret);
    }
    cdev_init(&my_cdev, &mychardev_fops);
    my_cdev.owner = THIS_MODULE;
    my_cdev.ops = &mychardev_fops;
    
    ret = cdev_add(&my_cdev, dev_number, 1);
    if(ret) {
        unregister_chrdev_region(dev_number, 1);
        pr_err("%s: cdev_add failed: %d\n", DEVICE_NAME, ret);
        return ret;
    }

}
