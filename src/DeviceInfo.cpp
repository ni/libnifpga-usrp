/*
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

#include "DeviceInfo.h"
#include "Exception.h"
#include "SysfsFile.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

namespace nirio {

#ifdef __arm__
namespace {
std::string getFwVar(const std::string& var)
{
    char buffer[256]   = {};
    const auto command = std::string("/sbin/fw_printenv -n ") + var;
    std::unique_ptr<FILE, int (*)(FILE*)> p(::popen(command.c_str(), "r"), ::pclose);

    if (!p)
        NIRIO_THROW(SoftwareFaultException());

    ::fgets(buffer, sizeof buffer, p.get());
    // strip final newline
    buffer[strlen(buffer) - 1] = '\0';

    return buffer;
}
} // namespace
#endif

#ifdef __arm__
static const struct
{
    const char* const deviceCode;
    const char* modelName;
} armModelMap[] = {
    {"0x77B1", "NI-7931R"},
    {"0x77B2", "NI-7932R"},
    {"0x77AC", "NI-7935R"},
};
#else
static const struct
{
    const uint32_t device;
    const uint32_t subsystemDevice;
    const char* const modelName;
} pciModelMap[] = {
    {0x7626, 0x7626, "NI 9154"},
    {0x7627, 0x7627, "NI 9155"},
    {0x7539, 0x7539, "NI 9157"},
    {0x753A, 0x753A, "NI 9159"},
    {0x7391, 0x7391, "PXI-7842R"},
    {0x73E1, 0x73E1, "PXI-7854R"},
    {0xC4C4, 0x74D0, "PXIe-7961R"},
    {0xC4C4, 0x74E2, "PXIe-7962R"},
    {0xC4C4, 0x74E3, "PXIe-7965R"},
    {0xC4C4, 0x75CE, "PXIe-7966R"},
    {0xC4C4, 0x74F3, "PCIe-5140R"},
    {0xC4C4, 0x7553, "PCIe-1473R"},
    {0xC4C4, 0x76FB, "PCIe-1473R-LX110"},
    {0xC4C4, 0x7570, "PCIe-1474R"},
    {0xC4C4, 0x7571, "PCIe-1475R"},
    {0xC4C4, 0x7572, "PCIe-1476R"},
    {0xC4C4, 0x76B5, "PXIe-7971R"},
    {0xC4C4, 0x76B6, "PXIe-7972R"},
    {0xC4C4, 0x76B7, "PXIe-7975R"},
    {0xC4C4, 0x7777, "PXIe-7976R"},
    {0xC4C4, 0x7790, "PXIe-5170R (4CH)"},
    {0xC4C4, 0x7791, "PXIe-5170R (8CH)"},
    {0xC4C4, 0x7793, "PXIe-5171R (8CH)"},
    {0xC4C4, 0x7820, "PXIe-5164"},
    /* TODO: JCG- remove `R` from device ID string to correlate with newer LV
     * builds that remove it (marketing...) */
    {0xC4C4, 0x78F8, "PXIe-7981R"},
    {0xC4C4, 0x78F9, "PXIe-7982R"},
    {0xC4C4, 0x78FA, "PXIe-7985R"},
    {0xC4C4, 0x798C, "PXIe-7986R"},
    {0xC4C4, 0x79D3, "PCIe-7981R"},
    {0xC4C4, 0x79D4, "PCIe-7982R"},
    {0xC4C4, 0x79D5, "PCIe-7985R"},
};
#endif

std::string getModelName(const std::string& resource)
{
#ifndef __arm__
    const auto devicePath = SysfsFile::getDevicePath(resource);
    const auto device     = SysfsFile(joinPath(devicePath, "device")).readU32Hex();
    const auto subsystem =
        SysfsFile(joinPath(devicePath, "subsystem_device")).readU32Hex();
    for (size_t i = 0; i < sizeof(pciModelMap) / sizeof(*pciModelMap); i++)
        if (pciModelMap[i].device == device
            && pciModelMap[i].subsystemDevice == subsystem)
            return pciModelMap[i].modelName;
#else
    // Open & read a known present file to validate resource
    const auto dev        = SysfsFile(resource, "dev").readLineNoErrno();
    const auto deviceCode = getFwVar("DeviceCode");
    for (size_t i = 0; i < sizeof(armModelMap) / sizeof(*armModelMap); i++)
        if (deviceCode == armModelMap[i].deviceCode)
            return armModelMap[i].modelName;
#endif
    NIRIO_THROW(SoftwareFaultException());
}

std::string getSerialNumber(const std::string& resource)
{
#ifndef __arm__
    return SysfsFile(resource, "nirio_serial_number").readLineNoErrno();
#else
    // Open & read a known present file to validate resource
    const auto dev = SysfsFile(resource, "dev").readLineNoErrno();
    return getFwVar("serial#");
#endif
}

} // namespace nirio
