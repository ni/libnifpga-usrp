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

#include "Session.h"
#include "ErrnoMap.h"
#include "Exception.h"
#include "NiFpga.h"
#include "SysfsFile.h"
#include <poll.h>

namespace nirio {

namespace {

const class : public ErrnoMap
{
public:
    virtual void throwErrno(const int error) const
    {
        switch (error) {
            case EALREADY:
                throw FpgaAlreadyRunningException();
            default:
                ErrnoMap::throwErrno(error);
        }
    }
} alreadyErrnoMap;

} // unnamed namespace

Session::Session(
    const std::string& bitfilePath, const std::string& device, bool& alreadyDownloaded)
    : bitfile(bitfilePath)
    , device(device)
    , fileLock(DeviceFile::getCdevPath(device))
    , boardFile(DeviceFile::getCdevPath(device), DeviceFile::WriteOnly, alreadyErrnoMap)
    , resetFile(device, "reset_vi")
    , baseAddressOnDevice(bitfile.getBaseAddressOnDevice())
{
    alreadyDownloaded = false;
    // Each session grabs a reader lock to prevent NiFpgaEx_RemoveDevice from
    // removing while any sessions are opened. If we couldn't get the reader,
    // there must be a removal in progress, in which case we error like it's
    // already removed.
    if (!fileLock.tryLockReader())
        NIRIO_THROW(InvalidResourceNameException());

// TODO auchter: Should we check targetclass against anything?
#if 0 
    // ensure the target class matches
    const auto modelName = getModelName(device);
    if (modelName != bitfile.getTargetClass())
        NIRIO_THROW(DeviceTypeMismatchException());
#endif

    alreadyDownloaded = download();

    const auto fpgaAddressSpaceSize = SysfsFile(device, "fpga_size").readU32();
    boardFile.mapMemory(fpgaAddressSpaceSize);

    // for every FIFO in this bitfile
    for (auto it = bitfile.getFifos().cbegin(), end = bitfile.getFifos().cend();
         it != end;
         ++it) {
        // Bitfile constructor guarantees these are numbered [0,n-1]
        assert(fifos.size() == it->getNumber());
        // store in member as upgraded FifoInfo
        fifos.emplace_back(new Fifo(*it, device));
    }
}

const Bitfile& Session::getBitfile() const
{
    return bitfile;
}

const std::string& Session::getDevice() const
{
    return device;
}

// NOTE: we close even on incoming bad status to keep close semantics
void Session::close(const bool resetIfLastSession)
{
    // optionally tell the kernel to reset if last session
    if (resetIfLastSession) {
        // "2" means "only if there aren't multiple sessions opened"
        try {
            // TODO auchter: see comments in kernel driver about this
            resetFile.write(2);
        } catch (const FpgaBusyFpgaInterfaceCApiException&) {
            // we ignore when reset didn't happen due to multiple sessions
        }
    }
    // board will be closed in destructor
}

bool Session::isStarted() const
{
    return SysfsFile(device, "vi_started").readBool();
}

bool Session::isFinished() const
{
    return SysfsFile(device, "vi_finished").readBool();
}

bool Session::isRunning() const
{
    return isStarted() && !isFinished();
}

void Session::checkControlRegisterStatus() const
{
    // any sysfs attribute that reads the control register will report errors
    isStarted();
}

bool Session::run() const
{
    // NOTE: we don't check need to check isRunning first because kernel will
    //       return EALREADY if it's already running

    bool alreadyRunning = false;

    try {
        SysfsFile(device, "run_vi", alreadyErrnoMap).write(true);
    } catch (const FpgaAlreadyRunningException&) {
        alreadyRunning = true;
    }

    return alreadyRunning;
}

void Session::abort() const
{
    // tell the kernel to abort
    SysfsFile(device, "abort_vi").write(true);
    // kernel will stop all FIFOs, so we need to remember that it did
    setStoppedAllFifos();
}

void Session::reset() const
{
    // tell the kernel to reset
    resetFile.write(true);
    // kernel will stop all FIFOs, so we need to remember that it did
    setStoppedAllFifos();
}

// returns whether the bitfile had already been downloaded
bool Session::download(bool force)
{
    bool alreadyRunning = false;

    setStoppedAllFifos();

    // actually do the download by writing the personality blob
    try {
        // TODO auchter: boardFile.write(personalityBlob->getBlob(),
        // personalityBlob->getSize());
    } catch (const FpgaAlreadyRunningException&) {
        alreadyRunning = true;
    }

    return alreadyRunning;
}

void Session::setStoppedAllFifos() const
{
    Status status;
    // kernel will stop all FIFOs on abort/reset, so we need to remember that it
    // did by "redundantly" calling stop on each FIFO
    //
    // NOTE: we can't stop FIFOs before abort/reset because this will block the
    //       operation if a FIFO transfer is in progress, and abort/reset must
    //       be able to work immediately
    for (auto it = fifos.cbegin(), end = fifos.cend(); it != end; ++it) {
        // TODO: Find out why we can call non-const member function (setStopped)
        //       of this member-of-member-of-member (Fifo) from inside this
        //       const member function (Session::setStoppedAllFifos). Does
        //       std::vector<std::unique_ptr<Foo>> not preserve const-correctness?
        try {
            (*it)->setStopped();
        } catch (const ExceptionBase& e) {
            status.merge(e.getCode());
        }
    }

    if (status.isError())
        throw ExceptionBase(status.getCode());
}

void Session::findResource(const char* const name,
    const NiFpgaEx_ResourceType type,
    NiFpgaEx_Resource& resource) const
{
    // validate parameters
    assert(name); // checked in NiFpga.cpp
    // handle registers
    if (isRegister(type)) {
        // keep track of what we find
        bool found              = false;
        NiFpgaEx_Register local = 0;
        // for each register
        for (auto it  = bitfile.getRegisters().cbegin(),
                  end = bitfile.getRegisters().cend();
             it != end;
             ++it) {
            const auto& reg = *it;
            // if we found a match
            if (reg.matches(name, type)) {
                // if we _already_ found a match, it's ambiguous which they want,
                // so return an error instead of giving the wrong one
                if (found)
                    NIRIO_THROW(InvalidResourceNameException());

                // remember this match, but keep looking
                found = true;
                local = baseAddressOnDevice + reg.getOffset();
                // mark "AccessMayTimeout" registers as such
                if (reg.isAccessMayTimeout())
                    setAccessMayTimeout(local);
            }
        }
        // if we found one-and-only-one, let 'em have it
        if (found) {
            resource = local;
            return;
        }
    }
    // handle FIFOs
    else if (isDmaFifo(type)) {
        // TODO: support searching for P2P FIFOs instead of just these DMA FIFOs?
        for (auto it = bitfile.getFifos().cbegin(), end = bitfile.getFifos().cend();
             it != end;
             ++it) {
            const auto& fifo = *it;
            if (fifo.matches(name, type)) {
                resource = fifo.getNumber();
                return;
            }
        }
    }
    // no such resource type
    else
        NIRIO_THROW(InvalidParameterException());

    // if we got this far, we didn't find their resource
    NIRIO_THROW(ResourceNotFoundException());
}

void Session::acknowledgeIrqs(uint32_t irqs)
{
    SysfsFile(device, "irq_status").write(irqs);
}

void Session::waitOnIrqs(
    uint32_t irqs, uint32_t timeout, uint32_t* const irqsAsserted, bool* timedOut)
{
    SysfsFile(device, "irq_mask").write(irqs);
    DeviceFile irqStatus(
        std::string("/sys/class/nirio/") + device + "/irq_status", DeviceFile::ReadOnly);

    auto asserted = readU32Hex(irqStatus);

    if (!timeout || (asserted & irqs)) {
        *timedOut     = false;
        *irqsAsserted = asserted & irqs;
        return;
    }

    struct pollfd fds;

    fds.fd     = irqStatus.getDescriptor();
    fds.events = POLLPRI;

    Timer timer(timeout);

    // Loop, since poll could return successfully signaling the assertion of an
    // IRQ that's not one we particularly care about.
    do {
        const int pollErr = ::poll(&fds, 1, timer.getRemaining());
        if (pollErr == -1)
            ErrnoMap::instance.throwErrno(errno);

        irqStatus.seek(0, SEEK_SET);
        *irqsAsserted = readU32Hex(irqStatus) & irqs;
        if (*irqsAsserted)
            return;
    } while (!timer.isTimedOut());

    *timedOut = true;
}

void Session::configureFifo(
    const NiFpgaEx_DmaFifo fifo, const size_t requestedDepth, size_t* const actualDepth)
{
    // validate parameters
    assert(requestedDepth != 0); // checked in NiFpga.cpp
    if (fifo >= fifos.size())
        NIRIO_THROW(InvalidParameterException());
    // pass it on
    fifos[fifo]->configure(requestedDepth, actualDepth);
}

void Session::startFifo(const NiFpgaEx_DmaFifo fifo)
{
    // validate parameters
    if (fifo >= fifos.size())
        NIRIO_THROW(InvalidParameterException());

    // pass it on
    fifos[fifo]->start();
}

void Session::stopFifo(const NiFpgaEx_DmaFifo fifo)
{
    // validate parameters
    if (fifo >= fifos.size())
        NIRIO_THROW(InvalidParameterException());

    // pass it on
    fifos[fifo]->stop();
}

void Session::releaseFifoElements(const NiFpgaEx_DmaFifo fifo, const size_t elements)
{
    // validate parameters
    if (fifo >= fifos.size())
        NIRIO_THROW(InvalidParameterException());

    // pass it on
    fifos[fifo]->release(elements);
}

} // namespace nirio
