/*
 * Private definitions
 *
 * Copyright (c) 2016 National Instruments
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

#include "NiFpga.h"

#pragma GCC visibility push(default)

extern "C"
NiFpga_Status NiFpgaPrivate_GetDeviceName(const NiFpga_Session session,
                                          char* const          buffer,
                                          const size_t         size);

#pragma GCC visibility pop
