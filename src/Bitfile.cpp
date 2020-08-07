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

#include "Bitfile.h"
#include "Exception.h"
#include "NiFpga.h"
#include "Type.h"
#include "libb64/cdecode.h"
#include "rapidxml/rapidxml_utils.hpp"
#include <cstring>
#include <iostream>
#include <limits> // std::numeric_limits

namespace nirio {

namespace {

const auto invalid                    = std::numeric_limits<NiFpgaEx_Register>::max();
const uint32_t maxBitfileVersionMajor = 4;

Type parseType(const std::string& text)
{
    if (text == "Boolean")
        return Bool();
    else if (text == "I8")
        return I8();
    else if (text == "U8" || text == "EnumU8")
        return U8();
    else if (text == "I16")
        return I16();
    else if (text == "U16" || text == "EnumU16")
        return U16();
    else if (text == "I32")
        return I32();
    else if (text == "U32" || text == "EnumU32")
        return U32();
    else if (text == "I64")
        return I64();
    // NOTE: it appears that EnumU64 isn't possible in LabVIEW FPGA, but there
    //       exists old code that acts like it is, so if we happen to find it
    //       we'll treat it as one would expect it should be treated
    else if (text == "U64" || text == "EnumU64")
        return U64();
    else if (!strcasecmp(text.c_str(), "Sgl"))
        return Sgl();
    else if (!strcasecmp(text.c_str(), "Dbl"))
        return Dbl();
    else {
        // FXPs and Clusters are supported by LabVIEW FPGA but not the C API,
        // so we mark them as unsupported and ignore them for now. Anything
        // else we didn't expect is an error.
        if (text != "FXP" && text != "Cluster")
            NIRIO_THROW(CorruptBitfileException());
        return UnsupportedType();
    }
}

unsigned int parseUnsignedInteger(rapidxml::xml_node<>& element)
{
    unsigned int value = 0;
    // NOTE: must try to match hex before decimal so leading zero doesn't match
    if (sscanf(element.value(), "0x%x", &value) != 1
        && sscanf(element.value(), "%u", &value) != 1)
        NIRIO_THROW(CorruptBitfileException());
    return value;
}

bool parseBoolean(rapidxml::xml_node<>& element)
{
    auto value = false;
    if (!strcasecmp(element.value(), "true"))
        value = true;
    else if (strcasecmp(element.value(), "false"))
        NIRIO_THROW(CorruptBitfileException());
    return value;
}

void parseVersionString(
    rapidxml::xml_node<>& element, unsigned int& major, unsigned int& minor)
{
    major = 0;
    minor = 0;
    if (sscanf(element.value(), "%u.%u", &major, &minor) != 2)
        NIRIO_THROW(CorruptBitfileException());
}

rapidxml::xml_node<>& findFirstChild(
    rapidxml::xml_node<>& parent, const char* const name = NULL)
{
    if (auto* const child = parent.first_node(name))
        return *child;
    else
        throw rapidxml::parse_error("XML parse error: child element not found", NULL);
}

rapidxml::xml_attribute<>& findFirstAttribute(
    rapidxml::xml_node<>& parent, const char* const name)
{
    if (auto* const attribute = parent.first_attribute(name))
        return *attribute;
    else
        throw rapidxml::parse_error("XML parse error: attribute not found", NULL);
}

rapidxml::xml_node<>& operator/(rapidxml::xml_node<>& parent, const char* const name)
{
    return findFirstChild(parent, name);
}

} // unnamed namespace

Bitfile::Bitfile(const std::string& path)
    : path(path)
    , baseAddressOnDevice(invalid)
    , signatureRegister(invalid)
    , controlRegister(invalid)
    , resetRegister(invalid)
    , fifosSupportClear(false)
    , fifosSupportBridgeFlush(false)
    , resetAutoClears(false)
    , autoRunWhenDownloaded(false)
    , bitstreamVersion(invalid)
{
    try {
        // open the bitfile
        rapidxml::file<> bitfile(path.c_str());
        rapidxml::xml_document<> document;
        document.parse<0>(bitfile.data());
        // validate bitfile versions aren't too new
        // TODO: validate NiFpga's Version, if present, isn't greater than latest?
        // TODO: validate NiRio's Version, if present, isn't greater than latest?
        auto& xmlBitfile = document / "Bitfile";
        uint32_t major, minor;
        parseVersionString(xmlBitfile / "BitfileVersion", major, minor);
        if (major > maxBitfileVersionMajor)
            NIRIO_THROW(IncompatibleBitfileException());
        // get the bitfile signature (not the signature register offset)
        signature = (xmlBitfile / "SignatureRegister").value();
        // search through the list of registers
        //
        // TODO: create a std::iterator for doing these loops
        for (auto* xmlRegister =
                 (xmlBitfile / "VI" / "RegisterList").first_node("Register");
             xmlRegister;
             xmlRegister = xmlRegister->next_sibling("Register")) {
            // determine common properties
            const std::string name((*xmlRegister / "Name").value());
            const auto offset   = parseUnsignedInteger(*xmlRegister / "Offset");
            const auto internal = parseBoolean(*xmlRegister / "Internal");
            // remember certain internal registers separately
            if (internal) {
                if (name == "ViSignature")
                    signatureRegister = offset;
                else if (name == "ViControl")
                    controlRegister = offset;
                else if (name == "DiagramReset")
                    resetRegister = offset;
                else if (name == "InterruptEnable")
                    irqEnable = offset;
                else if (name == "InterruptMask")
                    irqMask = offset;
                else if (name == "InterruptStatus")
                    irqStatus = offset;
                // ignore the rest
            }
            // remember all supported non-internal registers for later
            else {
                // determine type, etc.
                const auto indicator = parseBoolean(*xmlRegister / "Indicator");
                const auto accessMayTimeout =
                    parseBoolean(*xmlRegister / "AccessMayTimeout");
                auto& xmlDatatypeChild = findFirstChild(*xmlRegister / "Datatype");
                const std::string datatypeChild(xmlDatatypeChild.name());
                std::string typeString;
                bool array = false;
                // skip unsupported types
                if (datatypeChild == "Cluster" || datatypeChild == "String") {
                    // though FPGA VIs shouldn't contain strings anyway
                    assert(datatypeChild != "String");
                    continue;
                }
                // have to dig deeper to determine array types
                else if (datatypeChild == "Array") {
                    array      = true;
                    typeString = findFirstChild(xmlDatatypeChild / "Type").name();
                }
                // use simple types as-is
                else {
                    typeString = datatypeChild;
                }
                assert(!typeString.empty());
                // remember the register for later
                registers.emplace_back(name,
                    parseType(typeString),
                    offset,
                    indicator,
                    array,
                    accessMayTimeout);
            }
        }
        // ensure we found everything we expected
        if (signatureRegister == invalid || controlRegister == invalid
            || resetRegister == invalid)
            NIRIO_THROW(CorruptBitfileException());
        // get the target class
        auto& xmlProject = xmlBitfile / "Project";
        targetClass      = (xmlProject / "TargetClass").value();
        // determine whether it's auto-loaded
        autoRunWhenDownloaded = parseBoolean(xmlProject / "AutoRunWhenDownloaded");
        auto& compilationResults =
            xmlProject / "CompilationResultsTree" / "CompilationResults";
        try {
            // overlay is, for some reason, stored as a string of hex characters...
            char cbuf[3];
            cbuf[2]                = 0;
            auto& overlay          = compilationResults / "deviceTreeOverlay";
            auto overlayLen        = overlay.value_size();
            const char* overlayStr = overlay.value();
            assert(overlayLen % 2 == 0);
            for (size_t i = 0; i < overlayLen; i += 2) {
                cbuf[0]  = overlayStr[i];
                cbuf[1]  = overlayStr[i + 1];
                auto val = strtoul(cbuf, NULL, 16);
                dtOverlay.push_back(static_cast<char>(val));
            }
        } catch (const rapidxml::parse_error&) {
            // may be absent
        }
        // find the base address
        auto& xmlNiFpga     = compilationResults / "NiFpga";
        baseAddressOnDevice = parseUnsignedInteger(xmlNiFpga / "BaseAddressOnDevice");
        // get the bitstream version
        bitstreamVersion = parseUnsignedInteger(xmlBitfile / "BitstreamVersion");
        // different behaviors depend upon bitstream version
        fifosSupportClear       = bitstreamVersion >= 1;
        fifosSupportBridgeFlush = bitstreamVersion >= 2;
        resetAutoClears         = bitstreamVersion >= 2;
        // find all DMA FIFOs
        size_t i = 0;
        for (auto* xmlChannel =
                 (xmlNiFpga / "DmaChannelAllocationList").first_node("Channel");
             xmlChannel;
             xmlChannel = xmlChannel->next_sibling("Channel"), ++i) {
            // determine identifying information
            const std::string name = findFirstAttribute(*xmlChannel, "name").value();
            const auto number      = parseUnsignedInteger(*xmlChannel / "Number");
            const auto controlSet  = parseUnsignedInteger(*xmlChannel / "ControlSet");
            // determine direction, and skip any non-DMA FIFOs
            const std::string direction((*xmlChannel / "Direction").value());
            bool write;
            if (direction == "TargetToHost")
                write = false;
            else if (direction == "HostToTarget")
                write = true;
            else
                continue; // skip non-DMA FIFOs
            // determine type
            const auto type = parseType((*xmlChannel / "DataType" / "SubType").value());
            // NOTE: we expect FIFOs to be numbered [0,n-1] and in the bitfile
            //       in EXACTLY that order!
            if (number != i)
                NIRIO_THROW(CorruptBitfileException());
            // otherwise, add this FIFO in its numbered position
            else
                fifos.emplace_back(name,
                    type,
                    number,
                    controlSet,
                    write,
                    (*xmlChannel / "BaseAddressTag").value());
        }
        // if the map is filled sparsely, something's wrong
        i = 0; // reused
        for (auto it = fifos.cbegin(), end = fifos.cend(); it != end; ++it, ++i)
            if (it->getNumber() != i)
                NIRIO_THROW(CorruptBitfileException());
        // find FIFO offsets in a separate _optional_ section
        rapidxml::xml_node<>* xmlRegisterBlockList = NULL;
        try {
            xmlRegisterBlockList = &(xmlNiFpga / "RegisterBlockList");
        } catch (const rapidxml::parse_error&) {
            // no register block list is present if there are no FIFOs
        }
        // find all FIFO offsets, if present
        if (xmlRegisterBlockList)
            for (auto* xmlRegisterBlock =
                     xmlRegisterBlockList->first_node("RegisterBlock");
                 xmlRegisterBlock;
                 xmlRegisterBlock = xmlRegisterBlock->next_sibling("RegisterBlock")) {
                // find the offset
                const auto offset = parseUnsignedInteger(*xmlRegisterBlock / "Offset");
                // find the name
                auto& xmlName          = findFirstAttribute(*xmlRegisterBlock, "name");
                const std::string name = xmlName.value();
                // we should be able to find a FIFO with the same name
                auto found = false;
                for (auto it = fifos.begin(), end = fifos.end(); it != end; ++it) {
                    auto& fifo = *it;
                    // if the name matches the tag, set the offset
                    if (fifo.getBaseAddressTag() == name) {
                        fifo.setOffset(offset);
                        found = true;
                        break;
                    }
                }
                // if we didn't find a match, something's wrong
                if (!found)
                    NIRIO_THROW(CorruptBitfileException());
            }
        // ensure we found the offset of all FIFOs
        for (auto it = fifos.cbegin(), end = fifos.cend(); it != end; ++it)
            if (!it->isOffsetSet())
                NIRIO_THROW(CorruptBitfileException());
    } catch (const std::runtime_error&) {
        // rapidxml::file will throw this if it fails to open the file
        NIRIO_THROW(BitfileReadErrorException());
    } catch (const rapidxml::parse_error&) {
        // something went wrong parsing the contents
        NIRIO_THROW(CorruptBitfileException());
    }
}

const std::string& Bitfile::getPath() const
{
    return path;
}

const std::string& Bitfile::getSignature() const
{
    return signature;
}

const std::string& Bitfile::getTargetClass() const
{
    return targetClass;
}

const std::string& Bitfile::getOverlay() const
{
    return dtOverlay;
}

std::vector<char> Bitfile::getBitstream() const
{
    rapidxml::file<> bitfile(path.c_str());
    rapidxml::xml_document<> document;
    document.parse<0>(bitfile.data());

    auto& xmlBitfile = document / "Bitfile";

    // check bitstream encoding
    try {
        // if it was specified, ensure it's Base64
        if (strcasecmp((xmlBitfile / "BitstreamEncoding").value(), "base64")) {
            // this is considered corrupt instead of incompatible because we
            // already validated it was not a future BitfileVersion
            NIRIO_THROW(CorruptBitfileException());
        }
    } catch (const rapidxml::parse_error&) {
        // no bitstream encoding specified means it's de facto Base64
    }
    // find the bitstream
    auto& xmlBitstream = xmlBitfile / "Bitstream";

    std::vector<char> bitstream;
    bitstream.resize(xmlBitstream.value_size());

    base64_decodestate state;
    base64_init_decodestate(&state);
    auto decodedSize = base64_decode_block(
        xmlBitstream.value(), xmlBitstream.value_size(), &(bitstream[0]), &state);
    bitstream.resize(decodedSize);

    return bitstream;
}

NiFpgaEx_Register Bitfile::getBaseAddressOnDevice() const
{
    return baseAddressOnDevice;
}

NiFpgaEx_Register Bitfile::getSignatureRegister() const
{
    return signatureRegister;
}

NiFpgaEx_Register Bitfile::getControlRegister() const
{
    return controlRegister;
}

NiFpgaEx_Register Bitfile::getResetRegister() const
{
    return resetRegister;
}

NiFpgaEx_Register Bitfile::getIrqEnableRegister() const
{
    return irqEnable;
}

NiFpgaEx_Register Bitfile::getIrqMaskRegister() const
{
    return irqMask;
}

NiFpgaEx_Register Bitfile::getIrqStatusRegister() const
{
    return irqStatus;
}

bool Bitfile::isFifosSupportClear() const
{
    return fifosSupportClear;
}

bool Bitfile::isFifosSupportBridgeFlush() const
{
    return fifosSupportBridgeFlush;
}

bool Bitfile::isResetAutoClears() const
{
    return resetAutoClears;
}

bool Bitfile::isAutoRunWhenDownloaded() const
{
    return autoRunWhenDownloaded;
}

const RegisterInfoVector& Bitfile::getRegisters() const
{
    return registers;
}

const FifoInfoVector& Bitfile::getFifos() const
{
    return fifos;
}

uint32_t Bitfile::getBitstreamVersion() const
{
    return bitstreamVersion;
}

} // namespace nirio
