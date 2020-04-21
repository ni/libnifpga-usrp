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

#include "Status.h"
#include <string> // std::string

namespace nirio {

/**
 * A cross-process, multiple-readers/single-writer shared mutex backed by a file
 * on the filesystem. It is effectively a boost::interprocess::file_lock that
 * does not have the following limitations:
 *
 *    http://www.boost.org/doc/libs/1_41_0/doc/html/interprocess/synchronization_mechanisms.html#interprocess.synchronization_mechanisms.file_lock.file_lock_not_thread_safe
 *
 * Open file descriptors will use the O_CLOEXEC flag, so that copies will not be
 * held by child processes after a call to fork() and exec(). Such descriptors
 * are opened upon construction and not closed until destruction. Locking is
 * done with flock(), and therefore have the following behavior:
 *
 *    http://linux.die.net/man/2/flock
 */
class FileLock
{
public:
    explicit FileLock(const std::string& path);

    ~FileLock();

    void lockReader();

    void lockWriter();

    bool tryLockReader();

    bool tryLockWriter();

    void unlock();

private:
    bool flock(int operation);

    int descriptor;

    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
};

} // namespace nirio
