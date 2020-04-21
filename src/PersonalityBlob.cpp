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

#include "PersonalityBlob.h"
#include "Exception.h"
#include "libb64/cdecode.h"
#include "nirio.h"
#include <cstring> // memcpy

namespace nirio {

PersonalityBlob::PersonalityBlob(const char* const base64Bitstream,
    const size_t base64BitstreamSize,
    const bool fifosSupportClear,
    const bool fifosSupportBridgeFlush,
    const bool resetAutoClears,
    const bool autoRunWhenDownloaded,
    const std::string& signature,
    const FifoInfoVector& fifos)
    : blob()
{
    // Create a buffer big enough to hold the personality blob. We allocate
    // enough room for the encoded bitstream plus other fields as an upper
    // bound to the eventual size, since Base64-encoded strings are bloated
    // by ~33%.
    //
    // The blob has the following format:
    // struct nirio_personality_info
    // struct nirio_fifo_info * info.num_fifos
    // uint32_t bitstreamSize; // in bytes
    // uint8_t bitstream[bitstreamSize]
    blob.resize(sizeof(uint32_t) + // bitstreamSize
                base64BitstreamSize + // ~33% bigger than bitstream!
                sizeof(struct nirio_personality_info)
                + sizeof(struct nirio_fifo_info) * fifos.size()); // fifos[fifoCount]

    struct nirio_personality_info* info =
        reinterpret_cast<struct nirio_personality_info*>(blob.data());

    info->download_flags    = 0;
    info->personality_flags = 0;
    if (fifosSupportClear)
        info->personality_flags |= NIRIO_PERSONALITY_FIFOS_SUPPORT_CLEAR;
    if (fifosSupportBridgeFlush)
        info->personality_flags |= NIRIO_PERSONALITY_FIFOS_SUPPORT_BRIDGE_FLUSH;
    if (resetAutoClears)
        info->personality_flags |= NIRIO_PERSONALITY_RESET_AUTO_CLEARS;
    if (autoRunWhenDownloaded)
        info->personality_flags |= NIRIO_PERSONALITY_RUN_WHEN_LOADED;

    // signature better be the exact right size
    if (signature.size() == 32)
        memcpy(info->signature, signature.c_str(), 32);
    else
        NIRIO_THROW(CorruptBitfileException());

    info->num_fifos = fifos.size();

    size_t i = 0;
    for (auto it = fifos.cbegin(), end = fifos.cend(); it != end; ++it, ++i) {
        const auto& fifo = *it;
        // Bitfile constructor guarantees these are numbered [0,n-1]
        assert(i == fifo.getNumber());

        // fill FIFO contents assuming array index is FIFO number
        info->fifo[i].channel     = fifo.getNumber();
        info->fifo[i].control_set = fifo.getControlSet();
        info->fifo[i].offset      = fifo.getOffset();
        info->fifo[i].direction   = fifo.isHostToTarget() ? NIRIO_HOST_TO_TARGET
                                                        : NIRIO_TARGET_TO_HOST;
        info->fifo[i].bits_per_elem = fifo.getType().getLogicalBits();
    }

    auto* next = reinterpret_cast<uint8_t*>(&info->fifo[fifos.size()]);

    // decode the Base64-encoded bitstream into the personality blob just PAST
    // where we're going to later write the size
    base64_decodestate state;
    base64_init_decodestate(&state);
    const auto bitstreamSize = base64_decode_block(base64Bitstream,
        base64BitstreamSize,
        reinterpret_cast<char*>(next + sizeof(uint32_t)),
        &state);

    // now we can fill with the decoded bitstream size
    *reinterpret_cast<uint32_t*>(next) = bitstreamSize;
    next += sizeof(uint32_t) + bitstreamSize;
    // the total size is the difference between the end and the beginning
    blob.resize(next - blob.data());
}

PersonalityBlob::~PersonalityBlob() {}

const uint8_t* PersonalityBlob::getBlob() const
{
    return blob.data();
}

size_t PersonalityBlob::getSize() const
{
    return blob.size();
}

void PersonalityBlob::setForceDownload()
{
    struct nirio_personality_info* info =
        reinterpret_cast<struct nirio_personality_info*>(blob.data());
    info->download_flags |= NIRIO_DOWNLOAD_FORCE;
}

} // namespace nirio
