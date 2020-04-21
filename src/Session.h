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

#pragma once

#include "Bitfile.h"
#include "DeviceFile.h"
#include "Exception.h"
#include "Fifo.h"
#include "FileLock.h"
#include "Type.h"
#include "nirio.h"
#include <type_traits>
#include <cassert> // assert
#include <cstring> // memcpy
#include <memory> // std::unique_ptr
#include <vector> // std::vector

namespace nirio {

/**
 * A session to a NI-RIO device. Holds information about the current session
 * and how to communicate with the filesystem interface.
 */
class Session
{
public:
    Session(const std::string& bitfilePath,
        const std::string& device,
        bool& alreadyDownloaded);

    const Bitfile& getBitfile() const;

    const std::string& getDevice() const;

    void close(bool resetIfLastSession = false);

    bool isStarted() const;

    bool isFinished() const;

    bool isRunning() const;

    void checkControlRegisterStatus() const;

    // Returns true if the FPGA was already running when called
    bool run() const;

    void abort() const;

    void reset() const;

    bool download(bool force = false);

    void findResource(
        const char* name, NiFpgaEx_ResourceType type, NiFpgaEx_Resource& resource) const;

    template <typename T>
    void read(NiFpgaEx_Register reg, typename T::CType& value) const;

    template <typename T>
    void write(NiFpgaEx_Register reg, typename T::CType value) const;

    template <typename T>
    void readArray(
        NiFpgaEx_RegisterArray reg, typename T::CType* values, size_t count) const;

    template <typename T>
    void writeArray(
        NiFpgaEx_RegisterArray reg, const typename T::CType* values, size_t count) const;

    void acknowledgeIrqs(uint32_t irqs);

    void waitOnIrqs(
        uint32_t irqs, uint32_t timeout, uint32_t* const irqsAsserted, bool* timedOut);

    void configureFifo(NiFpgaEx_DmaFifo fifo, size_t requestedDepth, size_t* actualDepth);

    void configureFifoGpu(NiFpgaEx_DmaFifo fifo, size_t depth, void* buffer);

    void startFifo(NiFpgaEx_DmaFifo fifo);

    void stopFifo(NiFpgaEx_DmaFifo fifo);

    template <typename T, bool IsWrite>
    void acquireFifoElements(NiFpgaEx_DmaFifo fifo,
        typename T::CType*& elements,
        size_t elementsRequested,
        uint32_t timeout,
        size_t& elementsAcquired,
        size_t* elementsRemaining);

    void releaseFifoElements(NiFpgaEx_DmaFifo fifo, size_t elements);

    template <typename T>
    void readFifo(NiFpgaEx_TargetToHostFifo fifo,
        typename T::CType* data,
        size_t count,
        uint32_t timeout,
        size_t* elementsRemaining);

    template <typename T>
    void writeFifo(NiFpgaEx_HostToTargetFifo fifo,
        const typename T::CType* data,
        size_t count,
        uint32_t timeout,
        size_t* elementsRemaining);

private:
    /**
     * Bit that when asserted means a given control or indicator's access may
     * timeout (yielding NiFpga_Status_CommunicationTimeout) due to being in
     * an external clock domain. We had to steal a bit from from the top of
     * register offset itself because we couldn't easily change the signature
     * of the read and write functions but needed to pass this additional
     * information.
     */
    static const NiFpgaEx_Register accessMayTimeoutBit = 1 << 31;

    /**
     * Sets that a given control or indicator's access may timeout (yielding
     * NiFpga_Status_CommunicationTimeout) due to being in an external clock
     * domain.
     *
     * @param reg control or indicator to set
     * @return modified register
     */
    static void setAccessMayTimeout(NiFpgaEx_Register& reg)
    {
        reg |= accessMayTimeoutBit;
    }

    /**
     * Checks whether a given control or indicator's access may timeout
     * (yielding NiFpga_Status_CommunicationTimeout) due to being in an
     * external clock domain.
     *
     * @param reg control or indicator to test
     * @return whether this register's access may timeout
     */
    static bool isAccessMayTimeout(const NiFpgaEx_Register reg)
    {
        return reg & accessMayTimeoutBit;
    }

    /**
     * Gets the register offset of a given control or indicator by masking
     * out any extra bits.
     *
     * @param reg control or indicator
     * @return register offset of this control or indicator
     */
    static NiFpgaEx_Register getOffset(const NiFpgaEx_Register reg)
    {
        return reg & ~accessMayTimeoutBit;
    }

    void setStoppedAllFifos() const;

    template <typename T, bool IsSingle, bool IsRead>
    void readOrWrite(
        NiFpgaEx_Register reg, typename T::CType* values, size_t count) const;

    Bitfile bitfile;
    const std::string device;
    FileLock fileLock;
    DeviceFile boardFile;
    std::unique_ptr<DeviceFile> personalityFile;
    const PersonalitySysfsFile resetFile;
    const uint32_t baseAddressOnDevice;

    typedef std::vector<std::unique_ptr<Fifo>> FifoVector;
    FifoVector fifos;

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
};

/**
 * Reads or writes from a control or indicator.
 *
 * @tparam T type of this register
 * @tparam IsSingle whether this represents a single-point access instead of an
 *                  array access. This is an optimization over just using count
 *                  so that single-point code is smaller and has less runtime
 *                  checks.
 * @tparam IsWrite whether this is a write operation instead of a read
 * @param offset register offset from FPGA address space
 * @param values pointer to read/write element(s). This is not const to support
 *               both the read and write cases.
 * @param count number of elements to read or write. This is assumed to be 1 if
 *              Single is true.
 */
template <typename T, bool IsSingle, bool IsWrite>
void Session::readOrWrite(
    NiFpgaEx_Register reg, typename T::CType* const values, const size_t count) const
{
    // strip any extra bits
    auto offset = getOffset(reg);
    // All accesses must be 32-bit aligned to prevent a failed bus transaction.
    // 16-bit and smaller registers have 2 added to the 32-bit aligned offset
    // that we must mask out. We always mask out the bottom two bits for these
    // sub-32-bit accesses and just in case a bad offset is passed.
    offset &= ~3;
    // The FPGA Interface C API Generator does not just copy control/indicator
    // offsets found in the bitfile's XML, which describes the offset into the
    // device's FPGA address space. Instead, capigen adds <BaseAddressOnDevice>
    // to each control/indicator's offset. We need to subtract
    // <BaseAddressOnDevice> to get back to matching the bitfile XML. This is
    // correct because the mapping the kernel provides will begin at the start
    // of FPGA address space.
    offset -= baseAddressOnDevice;
    // 32-bit and smaller accesses are done by a single 32-bit access to the
    // mapped registers file
    if ((IsSingle || count == 1) && T::elementBytes <= 4 && personalityFile->isMapped()) {
        // Helper type to select which form of mapped access should be used; for
        // all integral types, use a 32-bit unsigned access; use float for Sgl
        typedef
            typename std::conditional<std::is_same<T, Sgl>::value, float, uint32_t>::type
                MappedType;
        // either read or write
        //
        // NOTE: we don't bother setting -1 on error for reads because we do that
        //       in NiFpga.cpp
        if (IsWrite)
            personalityFile->mappedWrite<MappedType>(offset, *values);
        else
            *values = personalityFile->mappedRead<MappedType>(offset);
    }
    // 64-bit and higher accesses are done through an ioctl for safety and
    // performance reasons. The "array engine" on the FPGA expects that one
    // accesses a wide register with _exactly_ the right number of 32-bit
    // accesses in a row. If less occur before another wide register access, the
    // array engine gets out of sync and gives bad data. In order to prevent
    // multiple sessions from interleaving wide register accesses, or a crashed
    // process from doing a partial read, we do them atomically in the kernel
    // with one ioctl. Other alternatives like global locking would incur more
    // user/kernel transitions that would negatively affect performance.
    else {
        const auto payloadSize = T::elementBytes * count;
        // make enough room for the input struct as well as the output data
        //
        // NOTE: we only dynamically allocate if we need more than 8 bytes so
        //       that 64-bit accesses are faster and more deterministic
        uint8_t buffer64[sizeof(nirio_array) + 8];
        std::unique_ptr<uint8_t[]> bufferDynamic;
        uint8_t* buffer; // assigned below
        if (payloadSize > 8) {
            bufferDynamic.reset(new uint8_t[sizeof(nirio_array) + payloadSize]);
            buffer = bufferDynamic.get();
        } else
            buffer = buffer64;
        // treat the first part as a struct and fill it in
        auto& array         = *reinterpret_cast<nirio_array*>(buffer);
        array.offset        = offset;
        array.bits_per_elem = T::logicalBits; // 1 for Bool, 8 for U8
        array.num_elem      = count;
        // either read or write
        if (IsWrite) {
            // copy the entire payload all at once since ioctl packs as T::CType[]
            memcpy(buffer + sizeof(nirio_array), values, payloadSize);
            // send the ioctl
            personalityFile->ioctl(NIRIO_IOC_ARRAY_WRITE, buffer);
        } else {
            // send the ioctl
            personalityFile->ioctl(NIRIO_IOC_ARRAY_READ, buffer);
            // copy the entire payload all at once since ioctl packs as T::CType[]
            memcpy(values, buffer + sizeof(nirio_array), payloadSize);
        }
    }
    // if access may timeout, check for errors
    if (isAccessMayTimeout(reg))
        checkControlRegisterStatus();
}

template <typename T>
void Session::read(const NiFpgaEx_Register reg, typename T::CType& value) const
{
    readOrWrite<T, true, false>(reg, &value, 1);
}

template <typename T>
void Session::write(const NiFpgaEx_Register reg, typename T::CType value) const
{
    readOrWrite<T, true, true>(reg, const_cast<typename T::CType*>(&value), 1);
}

template <typename T>
void Session::readArray(const NiFpgaEx_RegisterArray reg,
    typename T::CType* const values,
    const size_t count) const
{
    readOrWrite<T, false, false>(reg, values, count);
}

template <typename T>
void Session::writeArray(const NiFpgaEx_RegisterArray reg,
    const typename T::CType* const values,
    const size_t count) const
{
    readOrWrite<T, false, true>(reg, const_cast<typename T::CType*>(values), count);
}

template <typename T, bool IsWrite>
void Session::acquireFifoElements(const NiFpgaEx_DmaFifo fifo,
    typename T::CType*& elements,
    const size_t elementsRequested,
    const uint32_t timeout,
    size_t& elementsAcquired,
    size_t* const elementsRemaining)
{
    // validate parameters
    if (fifo >= fifos.size())
        NIRIO_THROW(InvalidParameterException());

    // pass it on
    fifos[fifo]->acquire<T, IsWrite>(
        elements, elementsRequested, timeout, elementsAcquired, elementsRemaining);
}

template <typename T>
void Session::readFifo(const NiFpgaEx_TargetToHostFifo fifo,
    typename T::CType* const data,
    const size_t count,
    const uint32_t timeout,
    size_t* const elementsRemaining)
{
    // validate parameters
    if (fifo >= fifos.size())
        NIRIO_THROW(InvalidParameterException());

    // pass it on
    fifos[fifo]->read<T>(data, count, timeout, elementsRemaining);
}

template <typename T>
void Session::writeFifo(const NiFpgaEx_HostToTargetFifo fifo,
    const typename T::CType* const data,
    const size_t count,
    const uint32_t timeout,
    size_t* const elementsRemaining)
{
    // validate parameters
    if (fifo >= fifos.size())
        NIRIO_THROW(InvalidParameterException());

    // pass it on
    fifos[fifo]->write<T>(data, count, timeout, elementsRemaining);
}

} // namespace nirio
