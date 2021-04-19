/*
 * Copyright (c) 2020 National Instruments
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

#include "Bitfile.h"
#include "DeviceTree.h"
#include <stdio.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <memory>

int main(int argc, char** argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <bitfile.lvbitx>\n", argv[0]);
        return 1;
    }

    nirio::Bitfile bitfile(argv[1]);

    std::cout << nirio::generateDeviceTree(bitfile) << std::endl;

    auto&& bitstream = bitfile.getBitstream();

    const auto bitstreamName = bitfile.getSignature() + ".bin";
    std::ofstream bitstream_file(bitstreamName);
    bitstream_file.write(bitstream.data(), bitstream.size());

    return 0;
}
