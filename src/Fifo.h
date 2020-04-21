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

#include "FifoInfo.h"
#include "DeviceFile.h"
#include "SysfsFile.h"
#include "Timer.h"
#include "Common.h"
#include "Exception.h"
#include <cassert> // assert
#include <memory> // std::unique_ptr
#include <mutex> // std::recursive_mutex
#include <algorithm> // std::min
#include <cstring>
#include "nirio.h"
#include "valgrind.h"

namespace nirio
{

class FifoBufferInterface
{
public:
   virtual ~FifoBufferInterface() {};

   nirio_fifo_set_buffer_info getInfo() const
   {
      nirio_fifo_set_buffer_info info;

      info.bytes = getSize();
      info.buff_ptr = reinterpret_cast<unsigned long>(getBuffer());
      info.memory_type = getType();

      return info;
   }

   virtual size_t getSize() const = 0;
   virtual void* getBuffer() const = 0;
   virtual uint32_t getType() const = 0;
};

/**
 * Represents an actual DMA FIFO of a currently-downloaded personality.
 */
class Fifo : public FifoInfo
{
   public:
      Fifo(const FifoInfo&    fifo,
           const std::string& device);

      virtual ~Fifo() NIRIO_NOEXCEPT_IF(true);

      void configure(size_t  requestedDepth,
                     size_t* actualDepth);

      void configureGpu(size_t  depth,
                        void*   buffer);

      void start();

      void stop();

      void setStopped();

      template <typename T, bool IsWrite>
      void acquire(typename T::CType*& elements,
                   size_t              elementsRequested,
                   uint32_t            timeout,
                   size_t&             elementsAcquired,
                   size_t*             elementsRemaining);

      void release(size_t elements);

      template <typename T>
      void read(typename T::CType* data,
                size_t             elementsRequested,
                uint32_t           timeout,
                size_t*            elementsRemaining);

      template <typename T>
      void write(const typename T::CType* data,
                 size_t                   elementsRequested,
                 uint32_t                 timeout,
                 size_t*                  elementsRemaining);

   private:
      template <typename T, bool IsWrite>
      void readOrWrite(typename T::CType* data,
                       size_t             elementsRequested,
                       uint32_t           timeout,
                       size_t*            elementsRemaining);

      /// Do bookkeeping and set elements pointer after acquiring elements.
      /// Only handles contiguous acquires, i.e. does not handle wraparound
      /// case.
      template <typename T>
      void doContiguousAcquireBookkeeping(typename T::CType*& elements,
                                          size_t              elementsAcquired);

      void calculateDimensions(size_t  requestedDepth,
                               size_t& actualDepth,
                               size_t& actualSize,
                               bool    gpu) const;

      void setBuffer(const FifoBufferInterface&);

      void ensureConfigured();

      void ensureConfiguredAndStarted();

      void configureCpuOrGpu(size_t  requestedDepth,
                             size_t* actualDepth,
                             void*   gpuBuffer);

      /// Polls (from usermode) until all elements are available.
      size_t pollUntilAvailable(size_t       elementsRequested,
                                size_t*      elementsAvailable,
                                const Timer& timer);

      /// Get elements available.
      /// Handles aborted transfers by restarting FIFO.
      void getElementsAvailable(size_t& elementsAvailable);

      /// Acquires elements with a timeout.
      /// Does not update acquire bookkeeping, caller must do this.
      /// Uses kernel ioctl, so driver can trigger an interrupt instead of
      /// polling.
      void acquireWithWait(size_t   elementsRequested,
                           uint32_t timeoutMs,
                           size_t*  elementsRemaining);

      mutable std::recursive_mutex lock; ///< Lock to serialize access.
      std::unique_ptr<FifoBufferInterface> fifoBuffer;
      const std::string            device; ///< Device, such as "RIO0".
      /// Whether to use acquire and release instead of read and write. If
      /// false, next and acquired members are unused.
      const bool                   acquireRelease;
      bool                         started; ///< Whether currently started.
      /// Number of bytes per element transferred between hardware and driver.
      const size_t                 hardwareElementBytes;
      const size_t                 paddingBytes; ///< Buffer padding in bytes.
      size_t                       depth; ///< Total depth in elements.
      size_t                       size; ///< Total size in bytes.
      void*                        buffer; ///< Host or GPU memory buffer.
      bool                         gpu; ///< Whether buffer is a GPU buffer.
      size_t                       acquired; ///< Current number acquired.
      size_t                       next; ///< Next element to be acquired.
      std::unique_ptr<DeviceFile>  file; ///< FIFO character device file.
      const FifoSysfsFile          startedFile;
      const FifoSysfsFile          availableFile;
      const FifoSysfsFile          acquireFile;
      const FifoSysfsFile          releaseFile;

      Fifo(const Fifo&) = delete;
      Fifo& operator =(const Fifo&) = delete;
};

// precondition: lock is locked
// precondition: FIFO is configured and started
template <typename T>
void Fifo::doContiguousAcquireBookkeeping(typename T::CType*& elements,
                                          const size_t        elementsAcquired)
{
   acquired += elementsAcquired;
   elements = &static_cast<typename T::CType*>(buffer)[next];
   next += elementsAcquired;
   if (next == depth)
      next = 0;

   if (!gpu)
   {
      if (hostToTarget)
         VALGRIND_MAKE_MEM_UNDEFINED(elements, elementsAcquired * type.getElementBytes());
      else
         VALGRIND_MAKE_MEM_DEFINED(elements, elementsAcquired * type.getElementBytes());
   }
}

template <typename T, bool IsWrite>
void Fifo::acquire(typename T::CType*& elements,
                   size_t              elementsRequested,
                   const uint32_t      timeout,
                   size_t&             elementsAcquired,
                   size_t* const       elementsRemaining)
{
   // not all FIFOs support the acquire/release interface
   if (!acquireRelease)
      NIRIO_THROW(FeatureNotSupportedException());
   // ensure the type and direction are right
   if (T()     != type
   ||  IsWrite != hostToTarget)
      NIRIO_THROW(InvalidParameterException());
   // grab the lock
   const std::lock_guard<std::recursive_mutex> guard(lock);
   // you can't ask for more than is possible
   if (elementsRequested > depth)
      NIRIO_THROW(BadReadWriteCountException());
   // ensure they don't try to overrun the buffer
   elementsRequested = std::min(elementsRequested, depth - next);
   // you can't ask for more than are allowed due to not releasing enough
   if (elementsRequested + acquired > depth)
      NIRIO_THROW(ElementsNotPermissibleToBeAcquiredException());
   // configure and start are optional calls, so do them if necessary
   ensureConfiguredAndStarted();
   // Not trying to acquire anything at all, just get elements remaining
   if (elementsRequested == 0)
   {
      if (elementsRemaining)
         getElementsAvailable(*elementsRemaining);
      return;
   }

   acquireWithWait(elementsRequested, timeout, elementsRemaining);
   elementsAcquired = elementsRequested;
   doContiguousAcquireBookkeeping<T>(elements, elementsAcquired);
}

template <typename T, bool IsWrite>
void Fifo::readOrWrite(typename T::CType* data,
                       size_t             elementsRequested,
                       const uint32_t     timeout,
                       size_t* const      elementsRemaining)
{
   // ensure the type and direction are right
   if (T()     != type
   ||  IsWrite != hostToTarget)
      NIRIO_THROW(InvalidParameterException());
   // grab the lock
   const std::lock_guard<std::recursive_mutex> guard(lock);
   // cannot do this while elements are acquired
   if (acquired)
      NIRIO_THROW(FifoElementsCurrentlyAcquiredException());
   // you can't ask for more than is possible
   if (elementsRequested > depth)
      NIRIO_THROW(BadReadWriteCountException());
   // configure and start are optional calls, so do them if necessary
   ensureConfiguredAndStarted();
   // Not trying to read/write anything at all, just get elements remaining
   if (elementsRequested == 0)
   {
      if (elementsRemaining)
         getElementsAvailable(*elementsRemaining);
      return;
   }

   // We implement read/write in user-mode through acquire/release if possible
   // for easier debugging. But if acquire/release isn't supported, as with
   // MITE-based devices, we have to just read/write from the kernel.
   if (acquireRelease)
   {
      acquireWithWait(elementsRequested, timeout, elementsRemaining);

      // loop until we've copied the entire amount we just acquired
      size_t iterations = 0;
      do
      {
         // bookkeep the acquire (possibly a subset of total amount)
         typename T::CType* elements;
         const size_t elementsAcquired = std::min(elementsRequested, depth - next);
         doContiguousAcquireBookkeeping<T>(elements, elementsAcquired);
         // copy between the acquired region and the user's buffer
         const auto bytes = elementsAcquired * T::elementBytes;
         if (IsWrite)
            memcpy(elements, data, bytes);
         else
            memcpy(data, elements, bytes);
         // release the region
         //
         // NOTE: If release somehow failed, we'll be left in a weird state
         //       where elements are acquired but cannot be released, and
         //       potentially some elements were copied but not all. However,
         //       there's not much else we can do other than err out.
         release(elementsAcquired);
         // account for how many we got
         data += elementsAcquired;
         elementsRequested -= elementsAcquired;
         iterations++;
      } while (elementsRequested);
      // since it's a contiguous buffer and you can't ask for larger than
      // depth, there can only be one wrap-around case, thus only 2 iterations
      assert(iterations <= 2);
   }
   else
   {
      const Timer timer(timeout);
      // wait for enough elements (this sets elementsRemaining if non-NULL)
      const auto available = pollUntilAvailable(elementsRequested,
                                                elementsRemaining,
                                                timer);
      // we either got what we wanted, or erred
      UNUSED(available);
      assert(available >= elementsRequested);
      // early return if they were just querying
      if (!elementsRequested)
         return;

      const auto bytes = elementsRequested * T::elementBytes;
      // read/write depending upon direction
      if (IsWrite)
         file->write(data, bytes);
      else
         file->read(data, bytes);

      // there are now less remaining than pollUntilAvailable found
      if (elementsRemaining)
         *elementsRemaining -= elementsRequested;
   }
}

template <typename T>
void Fifo::read(typename T::CType* const data,
                const size_t             elementsRequested,
                const uint32_t           timeout,
                size_t* const            elementsRemaining)
{
   readOrWrite<T, false>(data,
                         elementsRequested,
                         timeout,
                         elementsRemaining);
}

template <typename T>
void Fifo::write(const typename T::CType* const data,
                 const size_t                   elementsRequested,
                 const uint32_t                 timeout,
                 size_t* const                  elementsRemaining)
{
   readOrWrite<T, true>(const_cast<typename T::CType*>(data),
                        elementsRequested,
                        timeout,
                        elementsRemaining);
}

} // namespace nirio