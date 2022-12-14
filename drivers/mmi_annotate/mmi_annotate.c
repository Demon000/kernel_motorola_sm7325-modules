/*
 * Copyright (C) 2018 Motorola Mobility LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/ctype.h>
#include <linux/version.h>
#if KERNEL_VERSION(5, 10, 0) > LINUX_VERSION_CODE
#include <linux/dma-contiguous.h>
#endif
#include <linux/dma-mapping.h>
#include <linux/mmi_annotate.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
#if IS_ENABLED(CONFIG_QCOM_MINIDUMP)
#include <soc/qcom/minidump.h>
#endif
#endif

#define MAX_USER_STR 1024
#define DEFAULT_MEM_SIZE 4096
#define PERSIST_MAGIC_NUM 0xABCD1234

struct platform_data {
	phys_addr_t	mem_address;
	size_t		mem_size;
};

struct mem_data_t {
	unsigned int  size;
	size_t        cur_off;
	unsigned char *contents;
};

struct persist_data_t {
	unsigned int  magic_num;
	size_t        size;
	size_t        cur_off;
	unsigned char contents[];
};

static DEFINE_MUTEX(persist_lock);
static DEFINE_MUTEX(mem_lock);

static struct proc_dir_entry *procfs_file;
static struct persist_data_t *persist_data;
static struct mem_data_t mem_data;
static int persist_unsupported = 0;

static int mmi_annotate_seq_show(struct seq_file *f, void *ptr)
{
	mutex_lock(&mem_lock);
	if (mem_data.contents && mem_data.cur_off > 0) {
		seq_printf(f, "%s", mem_data.contents);
	} else {
		seq_printf(f, "No annotated data.\n");
	}
	mutex_unlock(&mem_lock);

	return 0;
}

static int mmi_annotate_open(struct inode *inode, struct file *file)
{
	return single_open(file, mmi_annotate_seq_show, inode->i_private);
}

static ssize_t mmi_annotate_write(struct file *file, const char __user *buf,
				size_t count, loff_t *offset)
{
	char buffer[MAX_USER_STR];
	const size_t maxlen = sizeof(buffer) - 1;

	memset(buffer, 0, sizeof(buffer));
	if (copy_from_user(buffer, buf, count > maxlen ? maxlen : count))
		return -EFAULT;

	if (!persist_unsupported)
		mmi_annotate_persist("%s", buffer);
	else
		mmi_annotate("%s", buffer);

	return count;
}

#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
static const struct proc_ops mmi_annotate_operations = {
	.proc_open		= mmi_annotate_open,
	.proc_read		= seq_read,
	.proc_write		= mmi_annotate_write,
	.proc_lseek		= seq_lseek,
	.proc_release	= single_release,
};
#else
static const struct file_operations mmi_annotate_operations = {
	.open		= mmi_annotate_open,
	.read		= seq_read,
	.write		= mmi_annotate_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,4,0)
static void* mmi_annotate_ram_vmap(phys_addr_t start, size_t size)
{
	struct page **pages;
	phys_addr_t page_start;
	unsigned int page_count;
	unsigned int i;
	void *vaddr;

	page_start = start - offset_in_page(start);
	page_count = DIV_ROUND_UP(size + offset_in_page(start), PAGE_SIZE);

	pages = kmalloc_array(page_count, sizeof(struct page *), GFP_KERNEL);
	if (!pages) {
		pr_err("%s: Failed to allocate array for %u pages\n",
		       __func__, page_count);
		return NULL;
	}

	for (i = 0; i < page_count; i++) {
		phys_addr_t addr = page_start + i * PAGE_SIZE;
		pages[i] = pfn_to_page(addr >> PAGE_SHIFT);
	}
	vaddr = vmap(pages, page_count, VM_MAP, PAGE_KERNEL);
	kfree(pages);

	if (!vaddr) {
		pr_err("%s: Failed to vmap the %d pages\n",
		       __func__, page_count);
		return NULL;
	}

	/*
	 * Since vmap() uses page granularity, we must add the offset
	 * into the page here, to get the byte granularity address
	 * into the mapping to represent the actual "start" location.
	 */
	return vaddr + offset_in_page(start);
}
#endif

static int mmi_annotate_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct platform_data *pdata;
	struct resource res;
	struct device_node *node;
#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
#if IS_ENABLED(CONFIG_QCOM_MINIDUMP)
	struct md_region md_entry;
#endif
#endif
	int err = 0;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		err = -ENOMEM;
		goto err;
	}

	/* Get reserved memory region from Device-tree */
	node = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!node) {
		dev_err(dev, "No %s specified\n", "memory-region");
		goto err;
	}

	err = of_address_to_resource(node, 0, &res);
	of_node_put(node);
	if (err) {
		dev_err(dev, "No memory address assigned to the region\n");
		goto err;
	}

	pdata->mem_size = resource_size(&res);
	pdata->mem_address = res.start;

	dev_info(dev, "size %zx", pdata->mem_size);
	dev_info(dev, "addr %lx", (unsigned long)pdata->mem_address);

	if (pdata->mem_size < sizeof(struct persist_data_t)) {
		dev_err(dev, "Mem size too small %zx/%zd\n",
				pdata->mem_size, sizeof(struct persist_data_t));
		err = -ENOMEM;
		goto err;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,4,0)
	persist_data = mmi_annotate_ram_vmap(pdata->mem_address, pdata->mem_size);
#else
	persist_data = dma_remap(dev, NULL, pdata->mem_address,
					pdata->mem_size, 0);
#endif
	if (!persist_data) {
		dev_err(dev, "Cannot remap buffer %pa size %zx\n",
				&pdata->mem_address, pdata->mem_size);
		err = -ENOMEM;
		goto err;
	}

	/* Allocate local memory region for non-persistent data */
	if(of_property_read_u32(dev->of_node, "mem-size", &mem_data.size)) {
		mem_data.size = DEFAULT_MEM_SIZE;
		dev_info(dev, "mem-size defaulting to %d", DEFAULT_MEM_SIZE);
	}
	else
		dev_info(dev, "mem-size %x", mem_data.size);

	persist_unsupported = of_property_read_bool(dev->of_node, "persist_unsupported");
	if (persist_unsupported )
		dev_info(dev, "persist unsupported : 1");

	mem_data.contents = kzalloc(mem_data.size, GFP_KERNEL);
	if(!mem_data.contents) {
		dev_err(dev, "Cannot allocate buffer of size %x\n",
				mem_data.size);
		err = -ENOMEM;
		goto err;
	}

	/* If persist data is valid, copy it to the local mem area and reset it */
	persist_data->size = pdata->mem_size - sizeof(struct persist_data_t);
	if(persist_data->magic_num == PERSIST_MAGIC_NUM &&
		persist_data->cur_off <= persist_data->size) {
		memcpy(mem_data.contents,
				persist_data->contents,
				persist_data->cur_off);
		mem_data.cur_off += persist_data->cur_off;
	}
	else
		persist_data->magic_num = PERSIST_MAGIC_NUM;
	persist_data->cur_off = 0;

	/* Create the procfs file at /proc/driver/mmi_annotate */
	procfs_file = proc_create("driver/mmi_annotate",
		0444, NULL, &mmi_annotate_operations);

#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
#if IS_ENABLED(CONFIG_QCOM_MINIDUMP)
	/*Register annotate to minidump */
	strlcpy(md_entry.name, "ANNOTATE", sizeof(md_entry.name));
	if (!persist_unsupported) {
		md_entry.virt_addr = (uintptr_t)phys_to_virt(pdata->mem_address);
		md_entry.phys_addr = pdata->mem_address;
		md_entry.size = pdata->mem_size;
	} else {
		md_entry.virt_addr = (uintptr_t)mem_data.contents;
		md_entry.phys_addr = virt_to_phys((void*)mem_data.contents);
		md_entry.size = mem_data.size;
	}
	if (msm_minidump_add_region(&md_entry) < 0)
		pr_err("Failed to add annotate in Minidump\n");
#endif
#endif
err:
	return err;
}

int mmi_annotate(const char *fmt, ...)
{
	va_list args;
	int len = 0;
	char line_buf[512];

	va_start(args, fmt);
	len += vsnprintf(line_buf, sizeof(line_buf), fmt, args);
	va_end(args);

	mutex_lock(&mem_lock);
	if(mem_data.contents && mem_data.cur_off + len < mem_data.size) {
		memcpy(mem_data.contents + mem_data.cur_off, line_buf, len);
		mem_data.cur_off += len;
	}
	mutex_unlock(&mem_lock);

	return 0;
}
EXPORT_SYMBOL(mmi_annotate);

int mmi_annotate_persist(const char *fmt, ...)
{
	va_list args;
	int len = 0;
	char line_buf[512];

	va_start(args, fmt);
	len += vsnprintf(line_buf, sizeof(line_buf), fmt, args);
	va_end(args);

	if (!persist_unsupported) {
		mutex_lock(&persist_lock);
		if(persist_data &&
			persist_data->cur_off + len < persist_data->size) {
			memcpy(persist_data->contents + persist_data->cur_off,
				line_buf, len);
			persist_data->cur_off += len;
		}
		mutex_unlock(&persist_lock);
	} else {
		mmi_annotate("%s", line_buf);
	}

	return 0;
}
EXPORT_SYMBOL(mmi_annotate_persist);

static int mmi_annotate_remove(struct platform_device *pdev)
{
	if (procfs_file)
		remove_proc_entry("driver/mmi_annotate", NULL);
	if(mem_data.contents)
		kfree(mem_data.contents);
	return 0;
}

static const struct of_device_id mmi_annotate_match[] = {
	{ .compatible = "mmi,annotate" },
	{}
};

static struct platform_driver mmi_annotate_driver = {
	.probe		= mmi_annotate_probe,
	.remove		= mmi_annotate_remove,
	.driver		= {
		.name = "mmi_annotate",
		.of_match_table = mmi_annotate_match,
	},
};

static int mmi_annotate_init(void)
{
	return platform_driver_register(&mmi_annotate_driver);
}

static void mmi_annotate_exit(void)
{
	platform_driver_unregister(&mmi_annotate_driver);
}

module_init(mmi_annotate_init);
module_exit(mmi_annotate_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MMI annotate");
