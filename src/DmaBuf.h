#pragma once

#include "Common.h"
#include "DeviceFile.h"
#include "linux/dma-heap.h"
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <sys/stat.h>
#include <sys/types.h>

namespace nirio {

class DmaBuf
{
public:
    static DmaBuf* allocate(size_t size, bool hostToTarget, const char* heap = "system")
    {
        DeviceFile heapFile(joinPath("/dev/dma_heap", heap), DeviceFile::ReadWrite);
        struct dma_heap_allocation_data arg;

        arg.len        = size;
        arg.fd         = 0;
        arg.fd_flags   = O_RDWR | O_CLOEXEC; // TODO: validate
        arg.heap_flags = 0;

        heapFile.ioctl(DMA_HEAP_IOCTL_ALLOC, &arg);

        return new DmaBuf(arg.fd, size, hostToTarget);
    }

    volatile void* getPointer()
    {
        if (bufFile.isMapped())
            return buffer;

        buffer = bufFile.mapMemory(size);
        return buffer;
    }

    size_t getSize() const
    {
        return size;
    }

    void beginCpuAccess()
    {
        syncBuf(true);
    }

    void endCpuAccess()
    {
        syncBuf(false);
    }

    int getDescriptor() const
    {
        return bufFile.getDescriptor();
    }

private:
    explicit DmaBuf(int descriptor, size_t size, bool hostToTarget)
        : bufFile(descriptor, DeviceFile::ReadWrite)
        , size(size)
        , hostToTarget(hostToTarget)
        , buffer(NULL)
    {
    }

    void syncBuf(bool start)
    {
        struct dma_buf_sync arg;
        arg.flags = hostToTarget ? DMA_BUF_SYNC_WRITE : DMA_BUF_SYNC_READ;
        arg.flags |= start ? DMA_BUF_SYNC_START : DMA_BUF_SYNC_END;
        bufFile.ioctl(DMA_BUF_IOCTL_SYNC, &arg);
    }

    DeviceFile bufFile;
    const size_t size;
    const bool hostToTarget;
    volatile void* buffer;
};

} // namespace nirio
