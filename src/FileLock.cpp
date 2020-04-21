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

#include "FileLock.h"
#include "Exception.h"
#include "ErrnoMap.h"
#include <fcntl.h> // open
#include <unistd.h> // close
#include <sys/stat.h> // fstat
#include <sys/file.h> // flock

namespace nirio
{

namespace
{

const int invalidDescriptor = -1;

const class : public ErrnoMap
{
   public:
      virtual void throwErrno(const int error) const
      {
         switch (error)
         {
            // "The file is locked and the LOCK_NB flag was selected."
            case EWOULDBLOCK: return;
            // "The kernel ran out of memory for allocating lock records."
            case ENOLCK:      throw MemoryFullException();
            // "fd is not an open file descriptor."
            case EBADF:
               // file should already be open or status already error
               assert(false);
               throw SoftwareFaultException();
            // pass on the rest
            default:          ErrnoMap::throwErrno(error);
         }
      }
} flockErrnoMap;

} // unnamed namespace

FileLock::FileLock(const std::string& path) :
   descriptor(invalidDescriptor)
{
   // O_RDONLY because flock doesn't care the mode in which it was opened
   // O_CLOEXEC to ensure child processes don't inherit open handles
   descriptor = open(path.c_str(), O_RDONLY | O_CLOEXEC);
   // err out if open failed
   if (descriptor == invalidDescriptor)
      ErrnoMap::instance.throwErrno(errno);
}

FileLock::~FileLock()
{
   // TODO: Should we call unlock? Depends what we think of this:
   //
   //          Locks created by flock() are associated with an open file table
   //          entry. This means that duplicate file descriptors (created by,
   //          for example, fork(2) or dup(2)) refer to the same lock, and this
   //          lock may be modified or released using any of these descriptors.
   //          Furthermore, the lock is released either by an explicit LOCK_UN
   //          operation on any of these duplicate descriptors, or when all such
   //          descriptors have been closed.
   if (descriptor != invalidDescriptor)
      close(descriptor);
}

bool FileLock::flock(const int operation)
{
   // if it worked, return true
   if (::flock(descriptor, operation) == 0)
      return true;

   flockErrnoMap.throwErrno(errno);
   return false;
}

void FileLock::lockReader()
{
   flock(LOCK_SH);
}

void FileLock::lockWriter()
{
   flock(LOCK_EX);
}

bool FileLock::tryLockReader()
{
   return flock(LOCK_SH | LOCK_NB);
}

bool FileLock::tryLockWriter()
{
   return flock(LOCK_EX | LOCK_NB);
}

void FileLock::unlock()
{
   flock(LOCK_UN);
}

} // namespace nirio
