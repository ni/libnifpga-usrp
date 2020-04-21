/*
 * NI FlexRIO Interface C API
 *
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

#include "niflexrio.h"
#include <string> // std::string
#include <array>  // std::array
#include <cstdio> // sscanf
#include <iostream>
#define __STDC_FORMAT_MACROS // PRIu32
#include <cinttypes> // PRIu32
#include "NiFpga.h"
#include "NiFpgaPrivate.h"
#include "Timer.h"
#include "Exception.h"
#include "SysfsFile.h"
#include "Common.h"
#include "NiFlexRioErrors.h"
#include "DeviceInfo.h"
#include "ErrnoMap.h"
#include "Exception.h"

using namespace nirio;
using namespace niflexrio;

namespace
{
const class : public ErrnoMap
{
public:
   virtual void throwErrno(const int error) const
   {
      switch (error)
      {
         case EFAULT:    throw nirio::ExceptionBase(NIFLEXRIO_Status_InvalidEEPROMAddress);
         case ENOLINK:   throw nirio::ExceptionBase(NIFLEXRIO_Status_NoI2CAck);
         case EBUSY:     throw nirio::ExceptionBase(NIFLEXRIO_Status_I2CAlreadyInUse);
         case EIO:       throw nirio::ExceptionBase(NIFLEXRIO_Status_InvalidState);
         case ENODEV:    throw nirio::ExceptionBase(NIFLEXRIO_Status_NoIOModule);
         /*
          * Using IOModuleMgrTimeout for 4 possible timeouts:
          * IOModuleMgrTimeout, I2CBusTImeout, I2CArbiterTimeout,
          * and IOModuleDetectionTimeout.
          * The error string for IOModuleDetectionTimeout seems most applicable
          * and the other errors are less likely.
          */
         case ETIMEDOUT: throw nirio::ExceptionBase(NIFLEXRIO_Status_IOModuleDetectionTimeout);
         default:        ErrnoMap::throwErrno(error);
      }
   }
}  errnoMap;

} // unnamed namespace

//! Query an attribute for a NI FlexRIO device.
//! \param [in] deviceHandle The device handle for a NI FlexRIO device returned from NiFpga_Open().
//! \param [in] attribute The attribute to query.  Please refer to, and use, the enum NIFLEXRIO_Attr.
//! \param [in] valueType The type of attribute to query.  Please refer to, and use, the enum NIFLEXRIO_ValueType.
//! \param [out] value The result will be placed in this variable.  The client is responsible for proper memory allocation/deallocation.
//! \return status code
int32_t NiFlexRio_GetAttribute(const NiFpga_Session deviceHandle,
                               int32_t const        attribute,
                               int32_t const        valueType,
                               void * const         value)
{
   //------------------------------------------------------------------------
   //  Validate parameters
   //------------------------------------------------------------------------
   if (value     == NULL)                                     return NiFpga_Status_InvalidParameter;
   if (attribute >= static_cast<int32_t>(NIFLEXRIO_Attr_END)) return NiFpga_Status_InvalidParameter;
   if (valueType != NIFLEXRIO_ValueType_U32)                  return NiFpga_Status_InvalidParameter;

   std::array<char, 10> device;
   Status status(NiFpgaPrivate_GetDeviceName(deviceHandle, device.data(), device.size()));
   if (status.getCode() > 0)
      return status.getCode();

   try
   {
      std::string sysfsAttribute;
      //------------------------------------------------------------------------
      //  Depending on the attribute being queried, a FAM may or may not be required.
      //  This switch statement checks.
      //------------------------------------------------------------------------
      switch (attribute)
      {
         // These attributes require a FAM be present and should error otherwise
         // Since all attributes that require a FAM also use a FAM, just fall through, too
         //
         case NIFLEXRIO_Attr_FamPowerGood:
            sysfsAttribute = "nirio_fam_power_good";
            break;
         case NIFLEXRIO_Attr_FamPresent:
            sysfsAttribute = "nirio_fam_present";
            break;
         case NIFLEXRIO_Attr_InsertedFamID:
            sysfsAttribute = "nirio_fam_id";
            break;
         case NIFLEXRIO_Attr_SerialNum:
            // NOTE: the CHInCh serial number is actually a 64-bit field, but this
            //       function only support 32-bits at this time
            {
               const auto serial = getSerialNumber(device.data());
               uint32_t result;
               if (sscanf(serial.c_str(), "%" PRIx32, &result) != 1)
                  NIRIO_THROW(SoftwareFaultException());
               *static_cast<uint32_t*>(value) = result;
               return NiFpga_Status_Success;
            }
            break;
         case NIFLEXRIO_Attr_Revision:
            sysfsAttribute = "nirio_fixed_logic_revision";
            break;
         case NIFLEXRIO_Attr_FamSerialNum:
            sysfsAttribute = "nirio_fam_serial_number";
            break;
         case NIFLEXRIO_Attr_VccoProgrammedSuccessfully:
         case NIFLEXRIO_Attr_PXIClk10Present:
         case NIFLEXRIO_Attr_FamIOEnabled:
         case NIFLEXRIO_Attr_FamPowerEnabled:
         case NIFLEXRIO_Attr_EEPROMPowerEnabled:
         case NIFLEXRIO_Attr_FamPowerGoodTimeout:
         case NIFLEXRIO_Attr_FamIDMismatch:
         case NIFLEXRIO_Attr_ExpectedFamID:
         case NIFLEXRIO_Attr_CurrentTemperature:
         case NIFLEXRIO_Attr_VccoARaw:
         case NIFLEXRIO_Attr_VccoBRaw:
         case NIFLEXRIO_Attr_Signature:
         case NIFLEXRIO_Attr_OldestCompatibleRevision:
         case NIFLEXRIO_Attr_FamState:
         case NIFLEXRIO_Attr_I2CMux:
         case NIFLEXRIO_Attr_InsertedFamHasEEPROM:
         case NIFLEXRIO_Attr_ExpectedFamHasEEPROM:
         case NIFLEXRIO_Attr_FamIDReadTimeout:
         case NIFLEXRIO_Attr_PXIeClk100Locked:
            return NiFpga_Status_FeatureNotSupported;
         default:
            return NiFpga_Status_InvalidParameter;
      }

      const SysfsFile sysfile(device.data(), "flexrio", sysfsAttribute, errnoMap);
      *static_cast<uint32_t*>(value) = sysfile.readU32Hex();
   }
   catch (const nirio::ExceptionBase& e)
   {
      status.merge(e.getCode());
   }
   catch (const std::bad_alloc&)
   {
      status.merge(NiFpga_Status_MemoryFull);
   }
   catch (const std::exception& error)
   {
      assert(false);
      std::cerr << "libniflexrio.so: unexpected exception: " << error.what()
                << std::endl;
      status.merge(NiFpga_Status_SoftwareFault);
   }
   catch (...)
   {
      assert(false);
      std::cerr << "libniflexrio.so: unexpected exception" << std::endl;
      status.merge(NiFpga_Status_SoftwareFault);
   }

   return status.getCode();
}

//! Query an array of attributes for a NI FlexRIO device.  For multiple attributes, querying as an array will be significantly faster than individually.  The attributes will be queried in the order they appear in the array.
//! \param [in] deviceHandle The device handle for a NI FlexRIO device returned from NiFpga_Open().
//! \param [in] attributeArray The attributes to query.  Please refer to, and use, the enum NIFLEXRIO_Attr.
//! \param [in] valueTypeArray The types of attributes to query.  Please refer to, and use, the enum NIFLEXRIO_ValueType.
//! \param [in] arraySize The number of attributes to be queried.
//! \param [out] valueArray The results will be placed in this variable.  The client is responsible for proper memory allocation/deallocation.
//! \return status code
int32_t NiFlexRio_GetAttributesArray(const NiFpga_Session deviceHandle,
                                     int32_t const *      const attributeArray,
                                     int32_t const *      const valueTypeArray,
                                     uint32_t const       arraySize,
                                     void * const         valueArray)
{
   UNUSED(deviceHandle);
   UNUSED(attributeArray);
   UNUSED(valueTypeArray);
   UNUSED(arraySize);
   UNUSED(valueArray);
   return NiFpga_Status_FeatureNotSupported;
}

//! Control and NI FlexRIO device's adapter module's power.
//! \param [in] deviceHandle The device handle for a NI FlexRIO device returned from NiFpga_Open().
//! \param [in] enable Control the FAM power by enabling (1) or disabling (0).
//! \return status code
int32_t NiFlexRio_FamControlPower(const NiFpga_Session deviceHandle,
                                  uint32_t const       enable)
{
   UNUSED(deviceHandle);
   UNUSED(enable);
   return NiFpga_Status_FeatureNotSupported;
}

//! Read from a NI FlexRIO's adapter module's EEPROM.
//! \param [in] deviceHandle The device handle for a NI FlexRIO device returned from NiFpga_Open().
//! \param [in] address The address in the EEPROM to begin reading from.
//! \param [in] numBytes The number of bytes to read.
//! \param [out] data The results will be placed in this variable.  The client is responsible for proper memory allocation/deallocation.
//! \return status code
int32_t NiFlexRio_FamReadEeprom(const NiFpga_Session deviceHandle,
                                uint8_t const        address,
                                uint16_t const       numBytes,
                                uint8_t * const      data)
{
   UNUSED(deviceHandle);
   UNUSED(address);
   UNUSED(numBytes);
   UNUSED(data);
   return NiFpga_Status_FeatureNotSupported;
}

//! Write to a NI FlexRIO's adapter module's EEPROM.
//! \param [in] deviceHandle The device handle for a NI FlexRIO device returned from NiFpga_Open().
//! \param [in] address The address in the EEPROM to begin writing to.
//! \param [in] numBytes The number of bytes to write.
//! \param [in] data The data to write into the EEPROM will be taken from this variable.
//! \return status code
int32_t NiFlexRio_FamWriteEeprom(const NiFpga_Session  deviceHandle,
                                 uint8_t const         address,
                                 uint16_t const        numBytes,
                                 uint8_t const * const data)
{
   UNUSED(deviceHandle);
   UNUSED(address);
   UNUSED(numBytes);
   UNUSED(data);
   return NiFpga_Status_FeatureNotSupported;
}

//! Control software's access to the I2C bus for the adapter module.  Access must be acquired prior to calling NiFlexRio_FamIssueI2CBusCycle(), and must be released afterwards.
//! \param [in] deviceHandle The device handle for a FlexRIO device returned from NiFpga_Open().
//! \param [in] i2cAccessMethod The type of access to request.  Please refer to, and use, the enum NIFLEXRIO_I2CAccess.
//! \param [in] timeout The amount of time in milliseconds to wait before the command times out.  Use -1 to wait indefinitely.
//! \return status code
int32_t NiFlexRio_FamI2CAccessControl(const NiFpga_Session deviceHandle,
                                      int32_t const        i2cAccessMethod,
                                      int32_t const        timeout)
{
   UNUSED(deviceHandle);
   UNUSED(i2cAccessMethod);
   UNUSED(timeout);
   return NiFpga_Status_FeatureNotSupported;
}

//! Issue an I2C command to the NI FlexRIO adapter module.  IMPORTANT: Software access to the I2C bus *must* be acquired *prior* to issuing this command.  Use NiFlexRioLV_FamI2CAccessControl() above.
//! \param [in] deviceHandle The device handle for a NI FlexRIO device returned from NiFpga_Open().
//! \param [in] i2cCommand The I2C command for the device.  Please refer to, and use, the enum NIFLEXRIO_I2CCmd.
//! \param [in] startBit Issue a start bit?  1 for yes, 0 for no.
//! \param [in] stopBit Issue a stop bit?  1 for yes, 0 for no.
//! \param [in] expectAck Will the other device acknowledge us and ACK back?  1 for yes, 0 for no.
//! \param [in] writeData The data to write to the bus (not used for reads).
//! \param [out] readData The data read from the bus (not used for writes).
//! \return status code
int32_t NiFlexRio_FamIssueI2CBusCycle(const NiFpga_Session deviceHandle,
                                      int32_t const        i2cCommand,
                                      uint32_t const       startBit,
                                      uint32_t const       stopBit,
                                      uint32_t const       expectAck,
                                      uint8_t const        writeData,
                                      uint8_t * const      readData)
{
   UNUSED(deviceHandle);
   UNUSED(i2cCommand);
   UNUSED(startBit);
   UNUSED(stopBit);
   UNUSED(expectAck);
   UNUSED(writeData);
   UNUSED(readData);
   return NiFpga_Status_FeatureNotSupported;
}

//! Set the product ID for the device's adapter module.
//! \param [in] deviceHandle The device handle for a NI FlexRIO device returned from NiFpga_Open().
//! \param [in] productId Set the product ID for this adapter module.
//! \return status code
int32_t NiFlexRio_FamSetProductId(const NiFpga_Session deviceHandle,
                                  uint32_t const       productId)
{
   UNUSED(deviceHandle);
   UNUSED(productId);
   return NiFpga_Status_FeatureNotSupported;
}

//! Get the product ID for the device's adapter module.
//! \param [in] deviceHandle The device handle for a NI FlexRIO device returned from NiFpga_Open().
//! \param [out] productId The product ID read from the device will be placed in this variable.  The client is responsible for proper memory allocation/deallocation.
//! \return status code
int32_t NiFlexRio_FamGetProductId(const NiFpga_Session deviceHandle,
                                  uint32_t * const     productId)
{
   UNUSED(deviceHandle);
   UNUSED(productId);
   return NiFpga_Status_FeatureNotSupported;
}

//! Redetect the device's adapter module.
//! \param [in] deviceHandle The device handle for a NI FlexRIO device returned from NiFpga_Open().
//! \return status code
int32_t NiFlexRio_FamRedetect(const NiFpga_Session deviceHandle)
{
   UNUSED(deviceHandle);
   return NiFpga_Status_FeatureNotSupported;
}

//! Read a user attribute from the device.  The attributes are non-volatile, for client use, and may be used however a client pleases.
//! \param [in] deviceHandle The device handle for a NI FlexRIO device returned from NiFpga_Open().
//! \param [in] attribute The attribute to read.
//! \param [out] value The attribute will be placed into this variable after it is read.  The client is responsible for proper memory allocation/deallocation.
//! \return status code
int32_t NiFlexRio_ReadUserAttribute(const NiFpga_Session deviceHandle,
                                    uint32_t const       attribute,
                                    uint32_t * const     value)
{
   UNUSED(deviceHandle);
   UNUSED(attribute);
   UNUSED(value);
   return NiFpga_Status_FeatureNotSupported;
}

//! Write a user attribute to the device.  The attributes are non-volatile, for client use, and may be used however a client pleases.
//! \param [in] deviceHandle The device handle for a NI FlexRIO device returned from NiFpga_Open().
//! \param [in] attribute The attribute to write.
//! \param [in] value The attribute will be set to the value of this variable.
//! \return status code
int32_t NiFlexRio_WriteUserAttribute(const NiFpga_Session deviceHandle,
                                     uint32_t const       attribute,
                                     uint32_t const       value)
{
   UNUSED(deviceHandle);
   UNUSED(attribute);
   UNUSED(value);
   return NiFpga_Status_FeatureNotSupported;
}

#pragma GCC visibility push(default)

#if ( defined(__cplusplus) )
extern "C" {
#endif

//! Returns a string with a description of the input status
//! \param [in] status The status code.
//! \return a string containing the description of the status
const char * NIFLEXRIO_EXPORT NIFLEXRIO_CDECL NiFlexRio_StatusToString(const int32_t status);
#if ( defined(__cplusplus) )
}
#endif

#pragma GCC visibility pop

const char * NIFLEXRIO_EXPORT NIFLEXRIO_CDECL NiFlexRio_StatusToString(const int32_t status)
{
  switch (status)
  {
    // generate
    // case NIFLEXRIO_Status_NoIOModule: return NIFLEXRIO_Status_NoIOModule_String;
    // etc. for all codes
    #define NIFLEXRIO_STATUS(status_name, status_code, description) \
      case NIFLEXRIO_Status_ ## status_name: return NIFLEXRIO_Status_ ## status_name ## _String;
    NIFLEXRIO_STATUS_TABLE
    #undef NIFLEXRIO_STATUS

    default:
      return "Not an NI FlexRIO status code";
  }
}
