/*
 * Copyright (c) 2013 National Instruments
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
#include "FifoInfo.h"
#include <string> // std::string
#include <vector> // std::vector

namespace nirio
{

/**
 * A binary "blob" containing all information required to describe an FPGA
 * personality to the kernel driver.
 */
class PersonalityBlob
{
   public:
      PersonalityBlob(const char*           base64Bitstream,
                      size_t                base64BitstreamSize,
                      bool                  fifosSupportClear,
                      bool                  fifosSupportBridgeFlush,
                      bool                  resetAutoClears,
                      bool                  autoRunWhenDownloaded,
                      const std::string&    signature,
                      const FifoInfoVector& fifos);

      ~PersonalityBlob();

      const uint8_t* getBlob() const;

      size_t getSize() const;

      // Modify the blob to enable force download
      void setForceDownload();

   private:
      std::vector<uint8_t> blob;

      PersonalityBlob(const PersonalityBlob&) = delete;
      PersonalityBlob& operator =(const PersonalityBlob&) = delete;
};

} // namespace nirio
