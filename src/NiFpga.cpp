/*
 * FPGA Interface C API entry point definitions
 *
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

#include "NiFpga.h"
#include "Common.h"
#include "DeviceInfo.h"
#include "ErrnoMap.h"
#include "Exception.h"
#include "NiFpgaPrivate.h"
#include "Session.h"
#include "Type.h"
#include <sched.h> // sched_yield
#include <cassert> // assert
#include <cstdlib> // realpath
#include <iostream> // std::cerr, std::endl
#include <map>
#include <memory> // std::unique_ptr
#include <mutex>

using namespace nirio;

#define CATCH_ALL_AND_MERGE_STATUS(status)                                  \
    catch (const ExceptionBase& e)                                          \
    {                                                                       \
        status.merge(e.getCode());                                          \
    }                                                                       \
    catch (const std::bad_alloc&)                                           \
    {                                                                       \
        status.merge(NiFpga_Status_MemoryFull);                             \
    }                                                                       \
    catch (const std::exception& error)                                     \
    {                                                                       \
        assert(false);                                                      \
        std::cerr << "libNiFpga.so: unexpected exception: " << error.what() \
                  << std::endl;                                             \
        status.merge(NiFpga_Status_SoftwareFault);                          \
    }                                                                       \
    catch (...)                                                             \
    {                                                                       \
        assert(false);                                                      \
        std::cerr << "libNiFpga.so: unexpected exception" << std::endl;     \
        status.merge(NiFpga_Status_SoftwareFault);                          \
    }

// Simple handle map. This could be improved in a number of ways, but that's
// deferred until proven necessary.
class SessionManager
{
public:
    Session& getSession(NiFpga_Session sessionHandle) const
    {
        lock_guard guard(lock);

        auto it = sessionMap.find(sessionHandle);
        if (it == sessionMap.end())
            NIRIO_THROW(InvalidSessionException());

        return *it->second.get();
    }

    NiFpga_Session registerSession(std::unique_ptr<Session>& session)
    {
        lock_guard guard(lock);

        NiFpga_Session handle;

        do {
            handle = static_cast<NiFpga_Session>(rand()) & ~0x00002000U;
        } while (sessionMap.find(handle) != sessionMap.end());

        sessionMap[handle] = std::move(session);
        return handle;
    }

    void unregisterSession(NiFpga_Session sessionHandle)
    {
        lock_guard guard(lock);

        sessionMap.erase(sessionHandle);
    }

private:
    typedef std::lock_guard<std::mutex> lock_guard;
    mutable std::mutex lock;
    std::map<NiFpga_Session, std::unique_ptr<Session>> sessionMap;
};

namespace {
SessionManager sessionManager;

Session& getSession(NiFpga_Session session)
{
    return sessionManager.getSession(session);
}
} // namespace

NiFpga_Status NiFpga_Open(const char* const bitfile,
    const char* const signature,
    const char* const resource,
    const uint32_t attribute,
    NiFpga_Session* const session)
{
    // validate parameters
    //
    // NOTE: signature can now be NULL
    if (session)
        *session = 0;
    if (!session || !bitfile || !resource)
        return NiFpga_Status_InvalidParameter;
    //  only supported attributes for now
    if (attribute != 0 && attribute != NiFpga_OpenAttribute_NoRun)
        return NiFpga_Status_InvalidParameter;

    // wrap all code that might throw in a big safety net
    Status status;
    try {
        bool alreadyDownloaded;
        // create a new session object, which opens and downloads if necessary
        std::unique_ptr<Session> newSession(
            new Session(bitfile, resource, alreadyDownloaded));
        // TODO: instead of making Session constructor do open and download, break
        //       these up into separate methods that return more information so
        //       that we don't have infer stuff from warnings, etc.

        // ensure signature matches unless they didn't pass one
        if (signature && signature != newSession->getBitfile().getSignature())
            NIRIO_THROW(SignatureMismatchException());
        // Decide whether to run the FPGA. First, if they passed NoRun, we won't.
        // But if they didn't, we have to decide whether it would've run itself.
        // If it's NOT AutoRunWhenDownloaded, then we'll have to run it for them.
        // If it IS AutoRunWhenDownloaded but it was already downloaded, we need
        // to run it again because they expected it to be run during this open.
        if (!(attribute & NiFpga_OpenAttribute_NoRun)
            && (!newSession->getBitfile().isAutoRunWhenDownloaded() || alreadyDownloaded))
            newSession->run();

        // if everything worked, pass it on
        *session = sessionManager.registerSession(newSession);
    }
    CATCH_ALL_AND_MERGE_STATUS(status)

    return status;
}

NiFpga_Status NiFpga_Close(const NiFpga_Session session, const uint32_t attribute)
{
    // validate parameters
    if (!session)
        return NiFpga_Status_InvalidParameter;
    // wrap all code that might throw in a big safety net
    Status status;
    try {
        auto& sessionObject = getSession(session);
        // close either with or without reset
        const auto resetIfLastSession =
            !(attribute & NiFpga_CloseAttribute_NoResetIfLastSession);
        sessionObject.close(resetIfLastSession);
    }
    CATCH_ALL_AND_MERGE_STATUS(status)

    sessionManager.unregisterSession(session);
    return status;
}

NiFpga_Status NiFpga_Run(const NiFpga_Session session, const uint32_t attribute)
{
    // validate parameters
    if (!session)
        return NiFpga_Status_InvalidParameter;
    // wrap all code that might throw in a big safety net
    Status status;
    try {
        const auto& sessionObject = getSession(session);
        const auto alreadyRunning = sessionObject.run();

        if (alreadyRunning)
            status.merge(-NiFpga_Status_FpgaAlreadyRunning);

        // if they want us to wait until done
        if (attribute & NiFpga_RunAttribute_WaitUntilDone) {
            // loop until it's no longer running
            while (sessionObject.isRunning()) {
                // NOTE: "In the Linux implementation, sched_yield() always succeeds":
                //    http://man7.org/linux/man-pages/man2/sched_yield.2.html
                sched_yield();
            }
        }
    }
    CATCH_ALL_AND_MERGE_STATUS(status)
    return status;
}

NiFpga_Status NiFpga_Abort(const NiFpga_Session session)
{
    // validate parameters
    if (!session)
        return NiFpga_Status_InvalidParameter;
    // wrap all code that might throw in a big safety net
    Status status;
    try {
        const auto& sessionObject = getSession(session);
        sessionObject.abort();
    }
    CATCH_ALL_AND_MERGE_STATUS(status)
    return status;
}

NiFpga_Status NiFpga_Reset(const NiFpga_Session session)
{
    // validate parameters
    if (!session)
        return NiFpga_Status_InvalidParameter;
    // wrap all code that might throw in a big safety net
    Status status;
    try {
        const auto& sessionObject = getSession(session);
        sessionObject.reset();
    }
    CATCH_ALL_AND_MERGE_STATUS(status)
    return status;
}

NiFpga_Status NiFpga_Download(const NiFpga_Session session)
{
    // wrap all code that might throw in a big safety net
    Status status;
    try {
        auto& sessionObject = getSession(session);
        try {
            sessionObject.download(true);
        } catch (const FpgaBusyFpgaInterfaceCApiException&) {
            // If a download fails, close this session.
            // TODO: Should this close for any failure, and not just busy?
            sessionObject.close();
            sessionManager.unregisterSession(session);
            throw;
        }
    }
    CATCH_ALL_AND_MERGE_STATUS(status)
    return status;
}

NiFpga_Status NiFpgaEx_FindResource(const NiFpga_Session session,
    const char* const name,
    const NiFpgaEx_ResourceType type,
    NiFpgaEx_Resource* const resource)
{
    // validate parameters
    if (resource)
        *resource = 0;
    if (!session || !name || !resource)
        return NiFpga_Status_InvalidParameter;
    // wrap all code that might throw in a big safety net
    Status status;
    try {
        const auto& sessionObject = getSession(session);
        sessionObject.findResource(name, type, *resource);
    }
    CATCH_ALL_AND_MERGE_STATUS(status)
    return status;
}

// Macro to define a typed entry point for each type.
#define NIFPGA_FOR_EACH_SCALAR(Generator)                                                \
    Generator(Bool) Generator(I8) Generator(U8) Generator(I16) Generator(U16) Generator( \
        I32) Generator(U32) Generator(I64) Generator(U64) Generator(Sgl) Generator(Dbl)

#define NIFPGA_DEFINE_READ(T)                                    \
    NiFpga_Status NiFpga_Read##T(const NiFpga_Session session,   \
        const NiFpgaEx_Register##T reg,                          \
        T::CType* const value)                                   \
    {                                                            \
        /* validate parameters */                                \
        if (value)                                               \
            *value = -1;                                         \
        if (!session || !value)                                  \
            return NiFpga_Status_InvalidParameter;               \
        /* wrap all code that might throw in a big safety net */ \
        Status status;                                           \
        try {                                                    \
            const auto& sessionObject = getSession(session);     \
            sessionObject.read<T>(reg, *value);                  \
        }                                                        \
        CATCH_ALL_AND_MERGE_STATUS(status)                       \
        return status;                                           \
    }

// This generates the following functions:
//
//    NiFpga_ReadBool
//    NiFpga_ReadI8
//    NiFpga_ReadU8
//    NiFpga_ReadI16
//    NiFpga_ReadU16
//    NiFpga_ReadI32
//    NiFpga_ReadU32
//    NiFpga_ReadI64
//    NiFpga_ReadU64
//    NiFpga_ReadSgl
//    NiFpga_ReadDbl
NIFPGA_FOR_EACH_SCALAR(NIFPGA_DEFINE_READ)

#define NIFPGA_DEFINE_WRITE(T)                                   \
    NiFpga_Status NiFpga_Write##T(const NiFpga_Session session,  \
        const NiFpgaEx_Register##T reg,                          \
        const T::CType value)                                    \
    {                                                            \
        /* validate parameters */                                \
        if (!session)                                            \
            return NiFpga_Status_InvalidParameter;               \
        /* wrap all code that might throw in a big safety net */ \
        Status status;                                           \
        try {                                                    \
            const auto& sessionObject = getSession(session);     \
            sessionObject.write<T>(reg, value);                  \
        }                                                        \
        CATCH_ALL_AND_MERGE_STATUS(status)                       \
        return status;                                           \
    }

// This generates the following functions:
//
//    NiFpga_WriteBool
//    NiFpga_WriteI8
//    NiFpga_WriteU8
//    NiFpga_WriteI16
//    NiFpga_WriteU16
//    NiFpga_WriteI32
//    NiFpga_WriteU32
//    NiFpga_WriteI64
//    NiFpga_WriteU64
//    NiFpga_WriteSgl
//    NiFpga_WriteDbl
NIFPGA_FOR_EACH_SCALAR(NIFPGA_DEFINE_WRITE)

#define NIFPGA_DEFINE_READ_ARRAY(T)                                 \
    NiFpga_Status NiFpga_ReadArray##T(const NiFpga_Session session, \
        const NiFpgaEx_RegisterArray##T reg,                        \
        T::CType* const values,                                     \
        const size_t size)                                          \
    {                                                               \
        /* validate parameters */                                   \
        if (!session || !values)                                    \
            return NiFpga_Status_InvalidParameter;                  \
        /* wrap all code that might throw in a big safety net */    \
        Status status;                                              \
        try {                                                       \
            const auto& sessionObject = getSession(session);        \
            sessionObject.readArray<T>(reg, values, size);          \
        }                                                           \
        CATCH_ALL_AND_MERGE_STATUS(status)                          \
        return status;                                              \
    }

// This generates the following functions:
//
//    NiFpga_ReadArrayBool
//    NiFpga_ReadArrayI8
//    NiFpga_ReadArrayU8
//    NiFpga_ReadArrayI16
//    NiFpga_ReadArrayU16
//    NiFpga_ReadArrayI32
//    NiFpga_ReadArrayU32
//    NiFpga_ReadArrayI64
//    NiFpga_ReadArrayU64
//    NiFpga_ReadArraySgl
//    NiFpga_ReadArrayDbl
NIFPGA_FOR_EACH_SCALAR(NIFPGA_DEFINE_READ_ARRAY)

#define NIFPGA_DEFINE_WRITE_ARRAY(T)                                 \
    NiFpga_Status NiFpga_WriteArray##T(const NiFpga_Session session, \
        const NiFpgaEx_RegisterArray##T reg,                         \
        const T::CType* const values,                                \
        const size_t size)                                           \
    {                                                                \
        /* validate parameters */                                    \
        if (!session || !values)                                     \
            return NiFpga_Status_InvalidParameter;                   \
        /* wrap all code that might throw in a big safety net */     \
        Status status;                                               \
        try {                                                        \
            const auto& sessionObject = getSession(session);         \
            sessionObject.writeArray<T>(reg, values, size);          \
        }                                                            \
        CATCH_ALL_AND_MERGE_STATUS(status)                           \
        return status;                                               \
    }

// This generates the following functions:
//
//    NiFpga_WriteArrayBool
//    NiFpga_WriteArrayI8
//    NiFpga_WriteArrayU8
//    NiFpga_WriteArrayI16
//    NiFpga_WriteArrayU16
//    NiFpga_WriteArrayI32
//    NiFpga_WriteArrayU32
//    NiFpga_WriteArrayI64
//    NiFpga_WriteArrayU64
//    NiFpga_WriteArraySgl
//    NiFpga_WriteArrayDbl
NIFPGA_FOR_EACH_SCALAR(NIFPGA_DEFINE_WRITE_ARRAY)

NiFpga_Status NiFpga_ReserveIrqContext(
    const NiFpga_Session session, NiFpga_IrqContext* const context)
{
    UNUSED(session);
    UNUSED(context);
    return NiFpga_Status_Success;
}

NiFpga_Status NiFpga_UnreserveIrqContext(
    const NiFpga_Session session, const NiFpga_IrqContext context)
{
    UNUSED(session);
    UNUSED(context);
    return NiFpga_Status_Success;
}

NiFpga_Status NiFpga_WaitOnIrqs(const NiFpga_Session session,
    const NiFpga_IrqContext context,
    const uint32_t irqs,
    const uint32_t timeout,
    uint32_t* const irqsAsserted,
    NiFpga_Bool* const timedOut)
{
    UNUSED(context);
    Status status;
    try {
        auto& sessionObject = getSession(session);
        bool timedOut_;
        sessionObject.waitOnIrqs(irqs, timeout, irqsAsserted, &timedOut_);
        *timedOut = timedOut_;
    }
    CATCH_ALL_AND_MERGE_STATUS(status)
    return status;
}

NiFpga_Status NiFpga_AcknowledgeIrqs(const NiFpga_Session session, const uint32_t irqs)
{
    Status status;
    try {
        auto& sessionObject = getSession(session);
        sessionObject.acknowledgeIrqs(irqs);
    }
    CATCH_ALL_AND_MERGE_STATUS(status)
    return status;
}

NiFpga_Status NiFpga_ConfigureFifo(
    const NiFpga_Session session, const NiFpgaEx_DmaFifo fifo, const size_t depth)
{
    // call newer version
    return NiFpga_ConfigureFifo2(session, fifo, depth, NULL);
}

NiFpga_Status NiFpga_ConfigureFifo2(const NiFpga_Session session,
    const NiFpgaEx_DmaFifo fifo,
    const size_t requestedDepth,
    size_t* const actualDepth)
{
    // validate parameters
    if (actualDepth)
        *actualDepth = 0;
    /* actualDepth is optional */
    if (!session)
        return NiFpga_Status_InvalidParameter;
    if (requestedDepth == 0)
        return NiFpga_Status_BadDepth;
    // wrap all code that might throw in a big safety net
    Status status;
    try {
        auto& sessionObject = getSession(session);
        sessionObject.configureFifo(fifo, requestedDepth, actualDepth);
    }
    CATCH_ALL_AND_MERGE_STATUS(status)
    return status;
}

NiFpga_Status NiFpgaEx_ConfigureFifoGpu(const NiFpga_Session session,
    const NiFpgaEx_DmaFifo fifo,
    const size_t depth,
    void* const buffer)
{
    // validate parameters
    if (!session || !buffer)
        return NiFpga_Status_InvalidParameter;
    if (depth == 0)
        return NiFpga_Status_BadDepth;
    // wrap all code that might throw in a big safety net
    Status status;
    try {
        auto& sessionObject = getSession(session);
        sessionObject.configureFifoGpu(fifo, depth, buffer);
    }
    CATCH_ALL_AND_MERGE_STATUS(status)
    return status;
}

NiFpga_Status NiFpga_ConfigureFifoGpu(const NiFpga_Session session,
    const NiFpgaEx_DmaFifo fifo,
    const size_t depth,
    void* const buffer)
{
    return NiFpgaEx_ConfigureFifoGpu(session, fifo, depth, buffer);
}

NiFpga_Status NiFpga_StartFifo(const NiFpga_Session session, const NiFpgaEx_DmaFifo fifo)
{
    // validate parameters
    if (!session)
        return NiFpga_Status_InvalidParameter;
    // wrap all code that might throw in a big safety net
    Status status;
    try {
        auto& sessionObject = getSession(session);
        sessionObject.startFifo(fifo);
    }
    CATCH_ALL_AND_MERGE_STATUS(status)
    return status;
}

NiFpga_Status NiFpga_StopFifo(const NiFpga_Session session, const NiFpgaEx_DmaFifo fifo)
{
    // validate parameters
    if (!session)
        return NiFpga_Status_InvalidParameter;
    // wrap all code that might throw in a big safety net
    Status status;
    try {
        auto& sessionObject = getSession(session);
        sessionObject.stopFifo(fifo);
    }
    CATCH_ALL_AND_MERGE_STATUS(status)
    return status;
}

#define NIFPGA_DEFINE_READ_FIFO(T)                                         \
    NiFpga_Status NiFpga_ReadFifo##T(const NiFpga_Session session,         \
        const NiFpgaEx_TargetToHostFifo##T fifo,                           \
        T::CType* const data,                                              \
        const size_t numberOfElements,                                     \
        const uint32_t timeout,                                            \
        size_t* const elementsRemaining)                                   \
    {                                                                      \
        /* validate parameters (elementsRemaining is optional) */          \
        if (elementsRemaining)                                             \
            *elementsRemaining = 0;                                        \
        if (!session || !data)                                             \
            return NiFpga_Status_InvalidParameter;                         \
        /* wrap all code that might throw in a big safety net */           \
        Status status;                                                     \
        try {                                                              \
            auto& sessionObject = getSession(session);                     \
            sessionObject.readFifo<T>(                                     \
                fifo, data, numberOfElements, timeout, elementsRemaining); \
        }                                                                  \
        CATCH_ALL_AND_MERGE_STATUS(status)                                 \
        return status;                                                     \
    }

// This generates the following functions:
//
//    NiFpga_ReadFifoBool
//    NiFpga_ReadFifoI8
//    NiFpga_ReadFifoU8
//    NiFpga_ReadFifoI16
//    NiFpga_ReadFifoU16
//    NiFpga_ReadFifoI32
//    NiFpga_ReadFifoU32
//    NiFpga_ReadFifoI64
//    NiFpga_ReadFifoU64
//    NiFpga_ReadFifoSgl
//    NiFpga_ReadFifoDbl
NIFPGA_FOR_EACH_SCALAR(NIFPGA_DEFINE_READ_FIFO)

#define NIFPGA_DEFINE_WRITE_FIFO(T)                                        \
    NiFpga_Status NiFpga_WriteFifo##T(const NiFpga_Session session,        \
        const NiFpgaEx_HostToTargetFifo##T fifo,                           \
        const T::CType* const data,                                        \
        const size_t numberOfElements,                                     \
        const uint32_t timeout,                                            \
        size_t* const elementsRemaining)                                   \
    {                                                                      \
        /* validate parameters (elementsRemaining is optional) */          \
        if (elementsRemaining)                                             \
            *elementsRemaining = 0;                                        \
        if (!session || !data)                                             \
            return NiFpga_Status_InvalidParameter;                         \
        /* wrap all code that might throw in a big safety net */           \
        Status status;                                                     \
        try {                                                              \
            auto& sessionObject = getSession(session);                     \
            sessionObject.writeFifo<T>(                                    \
                fifo, data, numberOfElements, timeout, elementsRemaining); \
        }                                                                  \
        CATCH_ALL_AND_MERGE_STATUS(status)                                 \
        return status;                                                     \
    }

// This generates the following functions:
//
//    NiFpga_WriteFifoBool
//    NiFpga_WriteFifoI8
//    NiFpga_WriteFifoU8
//    NiFpga_WriteFifoI16
//    NiFpga_WriteFifoU16
//    NiFpga_WriteFifoI32
//    NiFpga_WriteFifoU32
//    NiFpga_WriteFifoI64
//    NiFpga_WriteFifoU64
//    NiFpga_WriteFifoSgl
//    NiFpga_WriteFifoDbl
NIFPGA_FOR_EACH_SCALAR(NIFPGA_DEFINE_WRITE_FIFO)

#define NIFPGA_DEFINE_ACQUIRE_FIFO_ELEMENTS(T, ReadOrWrite, TargetHost, IsWrite) \
    NiFpga_Status NiFpga_AcquireFifo##ReadOrWrite##Elements##T(                  \
        const NiFpga_Session session,                                            \
        const NiFpgaEx_##TargetHost##Fifo##T fifo,                               \
        T::CType** const elements,                                               \
        const size_t elementsRequested,                                          \
        const uint32_t timeout,                                                  \
        size_t* const elementsAcquired,                                          \
        size_t* const elementsRemaining)                                         \
    {                                                                            \
        /* validate parameters (elementsRemaining is optional) */                \
        if (elements)                                                            \
            *elements = NULL;                                                    \
        if (elementsAcquired)                                                    \
            *elementsAcquired = 0;                                               \
        if (elementsRemaining)                                                   \
            *elementsRemaining = 0;                                              \
        if (!session || !elements || !elementsAcquired)                          \
            return NiFpga_Status_InvalidParameter;                               \
        /* wrap all code that might throw in a big safety net */                 \
        Status status;                                                           \
        try {                                                                    \
            auto& sessionObject = getSession(session);                           \
            sessionObject.acquireFifoElements<T, IsWrite>(fifo,                  \
                *elements,                                                       \
                elementsRequested,                                               \
                timeout,                                                         \
                *elementsAcquired,                                               \
                elementsRemaining);                                              \
        }                                                                        \
        CATCH_ALL_AND_MERGE_STATUS(status)                                       \
        return status;                                                           \
    }

#define NIFPGA_DEFINE_ACQUIRE_FIFO_READ_ELEMENTS(T) \
    NIFPGA_DEFINE_ACQUIRE_FIFO_ELEMENTS(T, Read, TargetToHost, false)

// This generates the following functions:
//
//    NiFpga_AcquireFifoReadElementsBool
//    NiFpga_AcquireFifoReadElementsI8
//    NiFpga_AcquireFifoReadElementsU8
//    NiFpga_AcquireFifoReadElementsI16
//    NiFpga_AcquireFifoReadElementsU16
//    NiFpga_AcquireFifoReadElementsI32
//    NiFpga_AcquireFifoReadElementsU32
//    NiFpga_AcquireFifoReadElementsI64
//    NiFpga_AcquireFifoReadElementsU64
//    NiFpga_AcquireFifoReadElementsSgl
//    NiFpga_AcquireFifoReadElementsDbl
NIFPGA_FOR_EACH_SCALAR(NIFPGA_DEFINE_ACQUIRE_FIFO_READ_ELEMENTS)

#define NIFPGA_DEFINE_ACQUIRE_FIFO_WRITE_ELEMENTS(T) \
    NIFPGA_DEFINE_ACQUIRE_FIFO_ELEMENTS(T, Write, HostToTarget, true)

// This generates the following functions:
//
//    NiFpga_AcquireFifoWriteElementsBool
//    NiFpga_AcquireFifoWriteElementsI8
//    NiFpga_AcquireFifoWriteElementsU8
//    NiFpga_AcquireFifoWriteElementsI16
//    NiFpga_AcquireFifoWriteElementsU16
//    NiFpga_AcquireFifoWriteElementsI32
//    NiFpga_AcquireFifoWriteElementsU32
//    NiFpga_AcquireFifoWriteElementsI64
//    NiFpga_AcquireFifoWriteElementsU64
//    NiFpga_AcquireFifoWriteElementsSgl
//    NiFpga_AcquireFifoWriteElementsDbl
NIFPGA_FOR_EACH_SCALAR(NIFPGA_DEFINE_ACQUIRE_FIFO_WRITE_ELEMENTS)

NiFpga_Status NiFpga_ReleaseFifoElements(
    const NiFpga_Session session, const NiFpgaEx_DmaFifo fifo, const size_t elements)
{
    // validate parameters
    if (!session)
        return NiFpga_Status_InvalidParameter;
    // wrap all code that might throw in a big safety net
    Status status;
    try {
        auto& sessionObject = getSession(session);
        sessionObject.releaseFifoElements(fifo, elements);
    }
    CATCH_ALL_AND_MERGE_STATUS(status)
    return status;
}

NiFpga_Status NiFpga_GetPeerToPeerFifoEndpoint(const NiFpga_Session session,
    const NiFpgaEx_PeerToPeerFifo fifo,
    uint32_t* const endpoint)
{
    UNUSED(session);
    UNUSED(fifo);
    UNUSED(endpoint);
    return NiFpga_Status_FeatureNotSupported;
}

NiFpga_Status NiFpgaEx_GetAttributeString(const char* const resource,
    const NiFpgaEx_AttributeString attribute,
    char* const buffer,
    const size_t size)
{
    // validate parameters
    if (!size)
        return NiFpga_Status_InvalidParameter;
    if (buffer)
        buffer[0] = '\0';
    if (!resource || !buffer)
        return NiFpga_Status_InvalidParameter;
    // wrap all code that might throw in a big safety net
    Status status;
    try {
        std::string result;
        switch (attribute) {
            case NiFpgaEx_AttributeString_ModelName:
                result = getModelName(resource);
                break;
            case NiFpgaEx_AttributeString_SerialNumber:
                result = getSerialNumber(resource);
                break;
            default:
                return status.merge(NiFpga_Status_InvalidParameter);
        }
        // ensure there's enough room for it
        if (result.length() >= size)
            status.merge(NiFpga_Status_BufferInvalidSize);
        // copy entire contents and then add a null character
        if (status.isNotError())
            buffer[result.copy(buffer, size)] = '\0';
    }
    CATCH_ALL_AND_MERGE_STATUS(status)
    return status;
}

// Macro to early return if there are any open sessions, and prevent any other
// sessions from opening while still in enclosing code block. Each session
// grabs a reader lock to prevent shenanigans while any sessions are opened, so
// if we couldn't get the writer, there must still be sessions opened.
//
// NOTE: Not in a macro do-while so that file lock is held the whole time.
#define NO_OPEN_SESSIONS_GUARD                                                         \
    FileLock fileLock(DeviceFile::getCdevPath(resource, "board"));                     \
    if (!fileLock.tryLockWriter())                                                     \
        NIRIO_THROW(FpgaBusyFpgaInterfaceCApiException());                             \
    /* sanity check to ensure there realy are no opened sessions */                    \
    const auto sessions = SysfsFile(resource, "nirio_personality_refcount").readU32(); \
    if (sessions != 0) {                                                               \
        assert(false);                                                                 \
        NIRIO_THROW(SoftwareFaultException());                                         \
    }

NiFpga_Status NiFpgaEx_ClearFpga(const char* const resource)
{
    // validate parameters
    if (!resource)
        return NiFpga_Status_InvalidParameter;
    // wrap all code that might throw in a big safety net
    Status status;
    try {
        // early return if any sessions are opened, and prevent any from opening
        NO_OPEN_SESSIONS_GUARD
        // tell kernel to clear the FPGA
        SysfsFile(resource, "nirio_clear").write(true);
    }
    CATCH_ALL_AND_MERGE_STATUS(status)
    return status;
}

namespace {

const class : public ErrnoMap
{
public:
    virtual void throwErrno(const int error) const
    {
        switch (error) {
            // "Permission denied"
            case EACCES:
                throw AccessDeniedException();
            // pass on the rest
            default:
                ErrnoMap::throwErrno(error);
        }
    }
} hotplugErrnoMap;

} // unnamed namespace

NiFpga_Status NiFpgaEx_RemoveDevice(
    const char* const resource, NiFpgaEx_RemovedDevice* const device)
{
    // validate parameters
    if (device)
        *device = 0;
    if (!resource || !device)
        return NiFpga_Status_InvalidParameter;
    std::unique_ptr<char, void (*)(void*)> canonical(NULL, &free);
    // wrap all code that might throw in a big safety net
    Status status;
    try {
        // early return if any sessions are opened, and prevent any from opening
        NO_OPEN_SESSIONS_GUARD
        // ensure it's a supported removable NI 915x device
        const auto modelName = getModelName(resource);
        if (modelName.find("NI 915") != 0)
            return status.merge(NiFpga_Status_FeatureNotSupported);
        // get the absolute, canonical (no symlinks) path to this device
        canonical.reset(realpath(SysfsFile::getDevicePath(resource).c_str(), NULL));
        if (!canonical)
            return status.merge(NiFpga_Status_ResourceNotFound);

        // PCI "hotplug" removal of the upstream device. We take the third parent
        // to go from (1) the bus interface chip (DustMITE-NT) via PCI to a
        // bridge, then (2) over PCIe to a switch, and then finally (3) over MXIe
        // to whatever switch it's connected to.
        const auto remove = joinPath(canonical.get(), "..", "..", "..", "remove");
        SysfsFile(remove, hotplugErrnoMap).write(true);
        // Wait a while for the device to go away. This is necessary because older
        // kernels had asynchronous removal. If we didn't wait, someone could
        // remove (scheduled for later), rescan (no-op because it hasn't been
        // removed yet), and then the remove finally occurs. Subsequent calls to
        // NiFpga_Open would error even though it wouldn't look like they should.
        // Note that this isn't hypothetical; it actually happened in testing.
        // See here for more information: http://lwn.net/Articles/580141/
        if (!SysfsFile(canonical.get()).waitUntilDoesNotExist(5000))
            NIRIO_THROW(SoftwareFaultException());
    }
    CATCH_ALL_AND_MERGE_STATUS(status)
    // if everything worked, relinquish control of malloc'ed device path
    // to output parameter for use in NiFpgaEx_RemoveDevice
    if (status.isNotError())
        *device = reinterpret_cast<NiFpgaEx_RemovedDevice>(canonical.release());
    return status;
}

NiFpga_Status NiFpgaEx_RescanDevice(NiFpgaEx_RemovedDevice* const device)
{
    // validate parameters
    if (!device || !*device)
        return NiFpga_Status_InvalidParameter;
    // get the original device path
    const auto canonical = reinterpret_cast<char*>(*device);
    // wrap all code that might throw in a big safety net
    Status status;
    try {
        // PCI "hotplug" rescan the _parent_ of the upstream device we previously
        // removed in NiFpgaEx_RemoveDevice
        const auto upstreamParent = joinPath(canonical, "..", "..", "..", "..");
        SysfsFile(joinPath(upstreamParent, "rescan"), hotplugErrnoMap).write(true);
        // Try to wait a while for the device to come back, but don't error if we
        // don't see that happen. This is because it's possible another thread
        // removed the device after we rescanned but before we were able to
        // notice. The only way to prevent this would be to make remove and rescan
        // mutually exclusive (by grabbing the same file lock), but that would
        // require knowing the resource string ("RIO0"), which would complicate
        // the implementation of NiFpgaEx_RemovedDevice. Instead, we just wait for
        // a while here and hope that it's enough time to make subsequent open
        // calls successful. If not, NiFpga_Open will give an appropriate error.
        SysfsFile(canonical).waitUntilExists(5000);
    }
    CATCH_ALL_AND_MERGE_STATUS(status)
    // if everything worked, free the device path
    if (status.isNotError()) {
        free(canonical);
        *device = 0;
    }
    return status;
}

NiFpga_Status NiFpga_FindRegisterPrivate(const NiFpga_Session session,
    const char* const registerName,
    uint32_t expectedResourceType,
    uint32_t* resourceOffset)
{
    if (expectedResourceType != NiFpgaEx_ResourceType_Any)
        return NiFpga_Status_InvalidParameter;

    return NiFpgaEx_FindResource(session,
        registerName,
        static_cast<NiFpgaEx_ResourceType>(expectedResourceType),
        resourceOffset);
}

NiFpga_Status NiFpga_FindFifoPrivate(const NiFpga_Session session,
    const char* const fifoName,
    uint32_t expectedResourceType,
    uint32_t* resourceOffset)
{
    if (expectedResourceType != NiFpgaEx_ResourceType_Any)
        return NiFpga_Status_InvalidParameter;

    return NiFpgaEx_FindResource(session,
        fifoName,
        static_cast<NiFpgaEx_ResourceType>(expectedResourceType),
        resourceOffset);
}

NiFpga_Status NiFpga_GetBitfileSignature(
    const NiFpga_Session session, uint32_t* signature, size_t* signatureSize)
{
    if (!session || !signature || !signatureSize)
        return NiFpga_Status_InvalidParameter;

    // wrap all code that might throw in a big safety net
    Status status;
    try {
        const auto& sessionObject    = getSession(session);
        const auto& bitfileSignature = sessionObject.getBitfile().getSignature();

        if (*signatureSize < 4) {
            *signatureSize = 4;
            return NiFpga_Status_InvalidParameter;
        }

        sscanf(bitfileSignature.c_str(),
            "%08x%08x%08x%08x",
            signature,
            signature + 1,
            signature + 2,
            signature + 3);

        *signatureSize = 4;
    }
    CATCH_ALL_AND_MERGE_STATUS(status)
    return status;
}

/**
 * Gets the device name from a session.
 *
 * This function is not externally supported and therefore not in NiFpga.h.
 *
 * @param session handle to a currently open session
 * @param buffer output buffer to fill
 * @param size size in bytes of buffer to fill
 * @return result of the call
 */
NiFpga_Status NiFpgaPrivate_GetDeviceName(
    const NiFpga_Session session, char* const buffer, const size_t size)
{
    // validate parameters
    if (!session || !buffer)
        return NiFpga_Status_InvalidParameter;
    // wrap all code that might throw in a big safety net
    Status status;
    try {
        auto& sessionObject  = getSession(session);
        const auto& resource = sessionObject.getDevice();
        // ensure there's enough room for it
        if (resource.length() >= size)
            return status.merge(NiFpga_Status_BufferInvalidSize);
        // copy entire contents and then add a null character
        buffer[resource.copy(buffer, size)] = '\0';
    }
    CATCH_ALL_AND_MERGE_STATUS(status)
    return status;
}
