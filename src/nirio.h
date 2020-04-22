/*
 * Header for NI-RIO device driver
 *
 * Copyright (c) 2016 National Instruments
 */

#ifndef INCLUDE_NIRIO_H
#define INCLUDE_NIRIO_H

#include <linux/types.h> /* __u32 */
#ifndef __KERNEL__
#    include <sys/ioctl.h> /* _IOWR, _IOW */
#endif

/**
 * struct nirio_array - struct for RIO array IOCTLs
 * @offset: register offset from the lvbitx
 * @bits_per_elem: bits per element of the array (booleans are 1)
 * @num_elem: number of elements in the array
 *
 * To use the array IOCTL, pass a struct nirio_array. After the struct,
 * pass the array data to write (if NIRIO_IOC_ARRAY_WRITE), or enough
 * space to hold the array data to be received (if NIRIO_IOC_ARRAY_READ).
 *
 * Example:
 *	To write [1, 2, 3] to an array control of 3 U8's with
 *	lvbitx offset 160, write this data:
 *	[u32 160] [u32 8] [u32 3] [u8 1] [u8 2] [u8 3]
 */
struct nirio_array
{
    __u32 offset;
    __u32 bits_per_elem;
    __u32 num_elem;
    /*
     * NOTE: We use the __may_alias__ attribute so we can cast an ioctl buffer to
     *       an nirio_array struct without getting the following warning:
     *       "dereferencing type-punned pointer will break strict-aliasing rules"
     */
} __attribute__((__may_alias__));

/**
 * struct nirio_fifo_set_buffer_info - info to set a FIFO DMA buffer
 * @bytes: size in bytes of buffer
 * @buff_ptr: address of DMA buffer
 * memory_type: an enum memory_type value
 *
 * Both values should be page-rounded.
 */
struct nirio_fifo_set_buffer_info
{
    __aligned_u64 bytes;
    __aligned_u64 buff_ptr;
    __u32 memory_type;
};

#define NIRIO_INFINITE_TIMEOUT (0xffffffff)

/**
 * struct nirio_fifo__wait - info to wait for FIFO elements
 * @wait_num_elem: input (to kernel), number of elements to wait for
 * @num_elem_avail: output (from kernel), elements available after wait
 * @timeout_ms: input (to kernel), how long to wait in milliseconds,
 *		or NIRIO_INFINITE_TIMEOUT
 * @timed_out: output (from kernel), if timed out
 */
struct nirio_fifo_wait
{
    __aligned_u64 wait_num_elem;
    __aligned_u64 num_elem_avail;
    __u32 timeout_ms;
    __u32 timed_out;
};

#define NIRIO_IOC_MAGIC (93) /* need a random 8-bit number */

#define NIRIO_IOC_ARRAY_READ _IOWR(NIRIO_IOC_MAGIC, 0, struct nirio_array)
#define NIRIO_IOC_ARRAY_WRITE _IOW(NIRIO_IOC_MAGIC, 1, struct nirio_array)

#define NIRIO_IOC_FIFO_SET_BUFFER \
    _IOW(NIRIO_IOC_MAGIC, 2, struct nirio_fifo_set_buffer_info)

/**
 * On success, num_elem_avail will be the number of elements
 * available after acquiring wait_num_elem.
 * If timed out, no error will be returned, but timed_out will be set to true
 * and no elements will have been acquired at all. num_elem_avail will still
 * be the number of elements available (which must be less than wait_num_elem).
 */
#define NIRIO_IOC_FIFO_ACQUIRE_WAIT _IOWR(NIRIO_IOC_MAGIC, 3, struct nirio_fifo_wait)

#endif /* INCLUDE_NIRIO_H */
