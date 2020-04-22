/*
 * Copyright (c) 2014 National Instruments
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#include "Fifo.h"
#include "ErrnoMap.h"
#include "Exception.h"
#include <unistd.h> // sysconf
#include <cassert> // assert
#include <cstdlib> // valloc

namespace nirio {

namespace {

const auto pageSize = sysconf(_SC_PAGESIZE);

const auto pageSizeGpu = 65536; // 64 KiB

const auto minimumDepth = 1 << 14; // 16384 elements

size_t pageAlign(const size_t value, const size_t size)
{
    return value & ~(size - 1);
}

size_t pageRound(const size_t value, const bool gpu)
{
    const auto size = gpu ? pageSizeGpu : pageSize;
    return pageAlign(value + size - 1, size);
}

const class : public ErrnoMap
{
public:
    virtual void throwErrno(const int error) const
    {
        switch (error) {
            // if you, for example, try to start a started FIFO
            case EALREADY:
                return;
            // override because FIFO is busy, not the FPGA itself
            case EBUSY:
                throw FifoReservedException();
            // someone reset or otherwise stopped the FIFO behind our back
            case EPERM:
                throw TransferAbortedException();
            // if we acquire or release a bad amount
            //
            // NOTE: won't happen unless something happens behind our back
            case ENODATA:
                throw ElementsNotPermissibleToBeAcquiredException();
            // a GPU buffer was cudaFree'd out from under us
            case ENOTCONN:
                throw ResourceNotFoundException();
            // pass on the rest
            default:
                ErrnoMap::throwErrno(error);
        }
    }
} errnoMap;

} // unnamed namespace

class FifoBufferNull : public FifoBufferInterface
{
public:
    uint32_t getType() const
    {
        return MEMORY_TYPE_USER;
    }
    size_t getSize() const
    {
        return 0;
    }
    void* getBuffer() const
    {
        return NULL;
    }
};

class FifoBufferHost : public FifoBufferInterface
{
public:
    // page-align both because CHInCh-based devices perform better when using
    // whole pages, but more importantly because when using bounce-buffering,
    // whole pages will be buffered and we don't want to clobber other memory
    //
    // NOTE: can't use aligned_alloc until C11:
    //    http://man7.org/linux/man-pages/man3/posix_memalign.3.html
    explicit FifoBufferHost(size_t size) : buffer(valloc(size), free), size(size)
    {
        if (!buffer)
            NIRIO_THROW(MemoryFullException());
    }

    uint32_t getType() const
    {
        return MEMORY_TYPE_USER;
    }
    size_t getSize() const
    {
        return size;
    }
    void* getBuffer() const
    {
        return buffer.get();
    }

private:
    std::unique_ptr<void, void (*)(void*)> buffer;
    size_t size;
};

class FifoBufferGpu : public FifoBufferInterface
{
public:
    FifoBufferGpu(void* buffer, size_t size) : buffer(buffer), size(size) {}

    uint32_t getType() const
    {
        return MEMORY_TYPE_NVIDIA;
    }
    size_t getSize() const
    {
        return size;
    }
    void* getBuffer() const
    {
        return buffer;
    }

private:
    void* buffer;
    size_t size;
};


Fifo::Fifo(const FifoInfo& fifo, const std::string& device)
    : FifoInfo(fifo)
    , device(device)
    , started(false)
    , hardwareElementBytes(
          FifoSysfsFile(device, number, "element_bytes", errnoMap).readU32())
    ,
    // depth assigned below
    // size assigned below
    buffer(NULL)
    , gpu(false)
    , acquired(0)
    , next(0)
    , startedFile(device, number, "nirio_started", errnoMap)
    , availableFile(device, number, "nirio_elements_available", errnoMap)
    , acquireFile(device, number, "nirio_acquire_elements", errnoMap)
    , releaseFile(device, number, "nirio_release_elements", errnoMap)
{
    // calculate depth and size
    calculateDimensions(minimumDepth, depth, size, false /* gpu */);
}

Fifo::~Fifo() noexcept(true) {}

// precondition: in constructor, or lock is locked
void Fifo::calculateDimensions(const size_t requestedDepth,
    size_t& actualDepth,
    size_t& actualSize,
    const bool gpu) const
{
    // page-round both because CHInCh-based devices perform better when using
    // whole pages, but more importantly because when using bounce-buffering,
    // whole pages will be buffered and we don't want to clobber other memory
    //
    // NOTE: this differs from the closed-source driver in that MITE-based
    //       devices with coerce to larger depths, but this is fine since it
    //       won't perform any worse, and won't affect applications regarding
    //       buffer wrap-around since they don't support acquire/release anyway
    actualSize  = pageRound(requestedDepth * hardwareElementBytes, gpu);
    actualDepth = actualSize / hardwareElementBytes;
}

// precondition: lock is locked
void Fifo::ensureConfigured()
{
    if (!file) {
        // no file in GPU mode means the GPU buffer cudaFree'd out from under us
        if (gpu)
            NIRIO_THROW(TransferAbortedException());
        // in CPU mode, just pretend they called configure with the old depth
        else
            configure(depth, NULL);
    }
}

// precondition: lock is locked
void Fifo::ensureConfiguredAndStarted()
{
    // conditionally configure
    ensureConfigured();
    // don't bother starting if we think it already is
    //
    // NOTE: even though it's possible someone reset or otherwise stopped the
    // FIFO behind our back, we don't have an extra start call for every
    // acquire/read/write
    if (!started)
        start();
}

// precondition: lock is locked
void Fifo::setBuffer(const FifoBufferInterface& fifoBuf)
{
    // if there's not an error, we should have a file
    assert(file);
    // write the size and buffer to configure the FIFO
    nirio_fifo_set_buffer_info info = fifoBuf.getInfo();
    file->ioctl(NIRIO_IOC_FIFO_SET_BUFFER, &info);

    // if we have a new buffer, we start from the beginning of it
    buffer   = fifoBuf.getBuffer();
    next     = 0;
    acquired = 0;
}

void Fifo::configure(const size_t requestedDepth, size_t* const actualDepth)
{
    configureCpuOrGpu(requestedDepth, actualDepth, NULL);
}

void Fifo::configureGpu(const size_t depth, void* const buffer)

{
    configureCpuOrGpu(depth, NULL, buffer);
}

void Fifo::configureCpuOrGpu(
    const size_t requestedDepth, size_t* const actualDepth, void* const gpuBuffer)

{
    // we're working with a GPU buffer if they passed one
    const bool configuringGpu = gpuBuffer != NULL;
    // validate parameters
    assert(requestedDepth != 0); // checked in NiFpga.cpp
    // grab the lock
    const std::lock_guard<std::recursive_mutex> guard(lock);
    // cannot do this while elements are acquired
    if (acquired)
        NIRIO_THROW(FifoElementsCurrentlyAcquiredException());

    // calculate the actual dimensions
    size_t localActualDepth, actualSize;
    calculateDimensions(requestedDepth, localActualDepth, actualSize, configuringGpu);
    // GPU buffers must already be sized correctly
    if (configuringGpu && requestedDepth != localActualDepth)
        NIRIO_THROW(BadDepthException());

    // We can reuse the buffer if reconfiguring a CPU buffer of the same size.
    // If GPU, we can't reuse because they may've cudaFree'd out from under us.
    if (file && !configuringGpu && !gpu && actualSize == size) {
        // if the sizes are the same, the depths should be
        assert(localActualDepth == depth);
        // if a file is open, we should have a buffer
        assert(buffer);
    }
    // otherwise, we've got work to do
    else {
        // mark it as stopped
        started = false;
        // if we have a file, unset the old buffer we configured
        //
        // NOTE: we reuse the opened file so no one can steal it out from under us
        if (file) {
            setBuffer(FifoBufferNull());
        }
        // otherwise, open the cdev file for the first time
        else
            file.reset(new DeviceFile(DeviceFile::getFifoCdevPath(device, number),
                hostToTarget ? DeviceFile::WriteOnly : DeviceFile::ReadOnly,
                errnoMap));
        // we're about to change the buffer member, so update what type it is
        gpu = configuringGpu;
        // if GPU, just use the buffer they gave us
        if (gpu)
            fifoBuffer.reset(new FifoBufferGpu(gpuBuffer, actualSize));
        else
            fifoBuffer.reset(new FifoBufferHost(actualSize));

        // set the buffer in the kernel
        setBuffer(*fifoBuffer);
        // if everything's okay, remember the new sizes
        depth = localActualDepth;
        size  = fifoBuffer->getSize();
    }

    if (actualDepth)
        *actualDepth = depth;
}

void Fifo::start()
{
    // grab the lock
    const std::lock_guard<std::recursive_mutex> guard(lock);
    // conditionally configure
    ensureConfigured();
    // start regardless of whether we thought it was started in case someone
    // reset or otherwise stopped the FIFO behind our back
    //
    // NOTE: EALREADY should be mapped to Success
    startedFile.write(true);
    started = true;
}

/**
 * Stops a started FIFO.
 *
 */
void Fifo::stop()
{
    // grab the lock
    const std::lock_guard<std::recursive_mutex> guard(lock);
    // cannot do this while elements are acquired, unless it's forced
    if (acquired)
        NIRIO_THROW(FifoElementsCurrentlyAcquiredException());
    // actually mark it as stopped
    //
    // NOTE: we don't bother writing 0 to nirio_started because setStopped will
    //       close the file which will make kernel stop the FIFO, _and_ we don't
    //       want stop to error if it's already stopped (because, for example,
    //       Session::reset will call Session::stop _after_ kernel mode already
    //       stopped all FIFOs)
    setStopped();
}

/**
 * Marks the FIFO as stopped and does any necessary cleanup if we know if
 * someone reset or otherwise stopped the FIFO behind our back.
 *
 */
void Fifo::setStopped()
{
    // grab the lock
    const std::lock_guard<std::recursive_mutex> guard(lock);
    // if anything is configured, tear it down
    //
    // NOTE: even if the FIFO isn't started, we need to relinquish control of the
    //       FIFO by closing the file
    if (file) {
        file.reset();
        fifoBuffer.reset();
        buffer = NULL;
        // mark it as stopped
        started = false;
        // now that it's stopped, forget our previous progress
        next     = 0;
        acquired = 0;
        // remember depth and size in case they start again without a configure
    }
}

void Fifo::release(const size_t elements)
{
    // release of 0 elements should always succeed
    if (!elements)
        return;
    // grab the lock
    const std::lock_guard<std::recursive_mutex> guard(lock);
    // they shouldn't release more than they have
    if (elements > acquired)
        NIRIO_THROW(BadReadWriteCountException());
    // just pass it on, assuming kernel will error if wrong
    //
    // TODO: refactor SysfsFile so each read/write doesn't open, which allocates
    try {
        releaseFile.write(elements);
    } catch (const TransferAbortedException&) {
        // if someone reset or otherwise stopped the FIFO behind our back, take note
        setStopped();
    } catch (const ResourceNotFoundException&) {
        assert(gpu);
        setStopped();
        // round to public error code
        NIRIO_THROW(TransferAbortedException());
    }

    if (!gpu) {
        const char* buf               = static_cast<const char*>(buffer);
        const size_t bufSize          = depth * type.getElementBytes();
        const size_t acquiredInBytes  = acquired * type.getElementBytes();
        const size_t releasingInBytes = elements * type.getElementBytes();
        const size_t nextInBytes      = next * type.getElementBytes();

        if (nextInBytes >= acquiredInBytes) {
            VALGRIND_MAKE_MEM_NOACCESS(
                buf + (nextInBytes - acquiredInBytes), releasingInBytes);
        } else {
            // we've got a wrap to deal with:
            const size_t start = nextInBytes + bufSize - acquiredInBytes;
            const size_t len1  = std::min(bufSize - start, releasingInBytes);
            const size_t len2  = releasingInBytes - len1;
            VALGRIND_MAKE_MEM_NOACCESS(buf + start, len1);
            VALGRIND_MAKE_MEM_NOACCESS(buf, len2);
        }
    }

    // if they successfully released, remember it
    acquired -= elements;
}

// precondition: lock is locked
// precondition: FIFO is configured and started, or there's an error
void Fifo::acquireWithWait(const size_t elementsRequested,
    const uint32_t timeoutMs,
    size_t* const elementsRemaining)
{
    struct nirio_fifo_wait fifo_wait;
    fifo_wait.wait_num_elem = elementsRequested, fifo_wait.timeout_ms = timeoutMs;
    try {
        file->ioctl(NIRIO_IOC_FIFO_ACQUIRE_WAIT, &fifo_wait);
    } catch (const TransferAbortedException&) {
        // FIFO was stopped out from under us
        // clean up our members, restart, and try one more time to acquire
        setStopped();
        start();
        file->ioctl(NIRIO_IOC_FIFO_ACQUIRE_WAIT, &fifo_wait);
    } catch (const ResourceNotFoundException&) {
        // GPU buffer was cudaFree'd out from under us
        assert(gpu);
        // clean up our members but don't bother trying to restart
        setStopped();
        // round to public error code
        NIRIO_THROW(TransferAbortedException());
    }

    if (elementsRemaining)
        *elementsRemaining = fifo_wait.num_elem_avail;
    if (fifo_wait.timed_out)
        NIRIO_THROW(FifoTimeoutException());
}

void Fifo::getElementsAvailable(size_t& elementsAvailable)
{
    size_t available;

    try {
        available = availableFile.readU32();
    } catch (const TransferAbortedException&) {
        // FIFO was stopped out from under us
        // clean up members, restart, and try one more time
        setStopped();
        start();
        available = availableFile.readU32();
    } catch (const ResourceNotFoundException&) {
        // GPU buffer was cudaFree'd out from under us
        assert(gpu);
        // clean up our members but don't bother trying to restart
        setStopped();
        // round to public error code
        NIRIO_THROW(TransferAbortedException());
    }

    elementsAvailable = available;
}

// precondition: lock is locked
// precondition: FIFO is configured and started, or there's an error
// postcondition: elementsAvailable was set if non-NULL and no error
// postcondition: returns >= elementsRequested, or there's an error
size_t Fifo::pollUntilAvailable(
    const size_t elementsRequested, size_t* const elementsAvailable, const Timer& timer)
{
    assert(file && started);
    // loop waiting for enough elements
    size_t available = 0;
    do {
        // check whether we've timed out before checking available so that we
        // ensure there was "one last chance" to acquire in the case where there
        // was a significant thread switch
        const auto timedOut = timer.isTimedOut();
        // remember elements available
        //
        // TODO: We could consider remembering available in a member so that we
        //       don't don't have to ask kernel to ask the hardware when we know
        //       there should still be enough remaining. However, if they passed a
        //       non-NULL elementsAvailable, they likely want the most up-to-date
        //       value, so we'd have to do it anyway. In the case where they don't
        //       care about elementsAvailable we could skip asking the kernel, but
        //       then we'd have to copy the TransferAborted restart code below
        //       at the actual acquire call, since this available check would not
        //       have clued us in that someone reset or otherwise stopped the FIFO
        //       behind our back. So for now, we're ignoring this potential
        //       optimization unless we deem it necessary in the future.
        //
        // TODO: refactor SysfsFile so each read/write doesn't open, which
        //       allocates
        getElementsAvailable(available);
        // only update them if we got a good value
        if (elementsAvailable)
            *elementsAvailable = available;
        // early break if enough were available, so FifoTimeout isn't set
        if (available >= elementsRequested)
            break;
        // check if we've need to set the timeout error
        if (timedOut)
            NIRIO_THROW(FifoTimeoutException());
    } while (true);
    // return whatever the last value was, even on error
    return available;
}

} // namespace nirio
