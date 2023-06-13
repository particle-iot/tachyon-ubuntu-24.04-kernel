/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _UAPI_LINUX_GUNYAH_H
#define _UAPI_LINUX_GUNYAH_H

/*
 * Userspace interface for /dev/gunyah - gunyah based virtual machine
 */

#include <linux/types.h>
#include <linux/ioctl.h>

#define GH_IOCTL_TYPE			'G'

/*
 * ioctls for /dev/gunyah fds:
 */
#define GH_CREATE_VM			_IO(GH_IOCTL_TYPE, 0x0) /* Returns a Gunyah VM fd */

/*
 * ioctls for VM fds
 */

/**
 * enum gh_mem_flags - Possible flags on &struct gh_userspace_memory_region
 * @GH_MEM_ALLOW_READ: Allow guest to read the memory
 * @GH_MEM_ALLOW_WRITE: Allow guest to write to the memory
 * @GH_MEM_ALLOW_EXEC: Allow guest to execute instructions in the memory
 */
enum gh_mem_flags {
	GH_MEM_ALLOW_READ	= 1UL << 0,
	GH_MEM_ALLOW_WRITE	= 1UL << 1,
	GH_MEM_ALLOW_EXEC	= 1UL << 2,
};

/**
 * struct gh_userspace_memory_region - Userspace memory descripion for GH_VM_SET_USER_MEM_REGION
 * @label: Identifer to the region which is unique to the VM.
 * @flags: Flags for memory parcel behavior. See &enum gh_mem_flags.
 * @guest_phys_addr: Location of the memory region in guest's memory space (page-aligned)
 * @memory_size: Size of the region (page-aligned)
 * @userspace_addr: Location of the memory region in caller (userspace)'s memory
 *
 * See Documentation/virt/gunyah/vm-manager.rst for further details.
 */
struct gh_userspace_memory_region {
	__u32 label;
	__u32 flags;
	__u64 guest_phys_addr;
	__u64 memory_size;
	__u64 userspace_addr;
};

#define GH_VM_SET_USER_MEM_REGION	_IOW(GH_IOCTL_TYPE, 0x1, \
						struct gh_userspace_memory_region)

/**
 * struct gh_vm_dtb_config - Set the location of the VM's devicetree blob
 * @guest_phys_addr: Address of the VM's devicetree in guest memory.
 * @size: Maximum size of the devicetree including space for overlays.
 *        Resource manager applies an overlay to the DTB and dtb_size should
 *        include room for the overlay. A page of memory is typicaly plenty.
 */
struct gh_vm_dtb_config {
	__u64 guest_phys_addr;
	__u64 size;
};
#define GH_VM_SET_DTB_CONFIG	_IOW(GH_IOCTL_TYPE, 0x2, struct gh_vm_dtb_config)

#define GH_VM_START		_IO(GH_IOCTL_TYPE, 0x3)

#define GH_FN_MAX_ARG_SIZE		256

/**
 * struct gh_fn_desc - Arguments to create a VM function
 * @type: Type of the function. See &enum gh_fn_type.
 * @arg_size: Size of argument to pass to the function. arg_size <= GH_FN_MAX_ARG_SIZE
 * @arg: Pointer to argument given to the function. See &enum gh_fn_type for expected
 *       arguments for a function type.
 */
struct gh_fn_desc {
	__u32 type;
	__u32 arg_size;
	__u64 arg;
};

#define GH_VM_ADD_FUNCTION	_IOW(GH_IOCTL_TYPE, 0x4, struct gh_fn_desc)
#define GH_VM_REMOVE_FUNCTION	_IOW(GH_IOCTL_TYPE, 0x7, struct gh_fn_desc)

#endif
