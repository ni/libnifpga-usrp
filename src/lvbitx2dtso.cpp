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
