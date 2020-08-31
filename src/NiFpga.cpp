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
#include "DeviceTree.h"
#include "ErrnoMap.h"
#include "Exception.h"
#include "Session.h"
#include "Type.h"
#include <sched.h> // sched_yield
#include <cassert> // assert
#include <cstdlib> // realpath
#include <fstream>
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
        } while (!handle || sessionMap.find(handle) != sessionMap.end());

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

void download(const nirio::Bitfile& bitfile)
{
    const auto bitfileSignature = bitfile.getSignature();
    const std::string fwPath    = "/lib/firmware";
    const auto fpgaPath         = joinPath(fwPath, bitfileSignature + ".bin");
    const auto dtsPath          = joinPath(fwPath, bitfileSignature + ".dts");

    // write .dts and .bin to some location
    if (!exists(fpgaPath)) {
        auto&& bitstream = bitfile.getBitstream();
        std::ofstream fpgaFile(fpgaPath);
        fpgaFile.write(bitstream.data(), bitstream.size());
    }

    if (!exists(dtsPath)) {
        auto dts = nirio::generateDeviceTree(bitfile);
        std::ofstream dtsFile(dtsPath);
        dtsFile.write(dts.c_str(), dts.size());
    }

    // invoke uhd_image_loader to load image
    std::string cmd = "uhd_image_loader --args \"type=x4xx\" --fpga-path ";
    cmd += fpgaPath;
    if (system(cmd.c_str())) {
        std::cerr << "call to load fpga failed: " << errno << std::endl;
        NIRIO_THROW(SoftwareFaultException());
    }
}

} // namespace

NiFpga_Status NiFpga_Open(const char* const bitfilePath,
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
    if (!session || !bitfilePath || !resource)
        return NiFpga_Status_InvalidParameter;
    //  only supported attributes for now
    if (attribute & ~(NiFpga_OpenAttribute_NoRun | NiFpga_OpenAttribute_NoSignatureCheck))
        return NiFpga_Status_InvalidParameter;

    // wrap all code that might throw in a big safety net
    Status status;
    try {
        auto bitfile          = std::make_unique<Bitfile>(bitfilePath);
        auto bitfileSignature = bitfile->getSignature();
        SysfsFile signatureFile(resource, "signature");

        bool alreadyDownloaded = false;

        if (signatureFile.exists()) {
            auto runningSignature = signatureFile.readLineNoErrno();
            if (!strcasecmp(runningSignature.c_str(), bitfileSignature.c_str()))
                alreadyDownloaded = true;
        }

        if (!alreadyDownloaded)
            download(*bitfile);

        // create a new session object, which opens and downloads if necessary
        std::unique_ptr<Session> newSession(new Session(std::move(bitfile), resource));

        // ensure signature matches unless they didn't pass one
        if (!(attribute & NiFpga_OpenAttribute_NoSignatureCheck)
            && (signature && signature != newSession->getBitfile().getSignature()))
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
            sessionObject.preDownload();
            download(sessionObject.getBitfile());
            sessionObject.postDownload();
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
    Status status;
    try {
        auto& sessionObject = getSession(session);
        sessionObject.reserveIrqContext(context);
    }
    CATCH_ALL_AND_MERGE_STATUS(status)
    return status;
}

NiFpga_Status NiFpga_UnreserveIrqContext(
    const NiFpga_Session session, const NiFpga_IrqContext context)
{
    Status status;
    try {
        auto& sessionObject = getSession(session);
        sessionObject.unreserveIrqContext(context);
    }
    CATCH_ALL_AND_MERGE_STATUS(status)
    return status;
}

NiFpga_Status NiFpga_WaitOnIrqs(const NiFpga_Session session,
    const NiFpga_IrqContext context,
    const uint32_t irqs,
    const uint32_t timeout,
    uint32_t* const irqsAsserted,
    NiFpga_Bool* const timedOut)
{
    Status status;
    try {
        auto& sessionObject = getSession(session);
        bool timedOut_;
        sessionObject.waitOnIrqs(context, irqs, timeout, irqsAsserted, &timedOut_);
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
