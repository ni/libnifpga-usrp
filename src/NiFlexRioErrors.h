/*
 * NI FlexRIO Interface C API Errors
 *
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

namespace niflexrio
{

//! NI FlexRIO error codes start at this value and decrease.
//!
#define NIFLEXRIO_ERROR_BASE -304000

//! An X-Macro table for the NI FlexRIO error codes.
//!
#define NIFLEXRIO_STATUS_TABLE \
    NIFLEXRIO_STATUS( Success                         ,                          0, "Success" ) \
    \
   /* niflexrio shared library error codes */ \
   NIFLEXRIO_STATUS( NoIOModule                      , NIFLEXRIO_ERROR_BASE -   0, "No IO module" ) \
   NIFLEXRIO_STATUS( InvalidState                    , NIFLEXRIO_ERROR_BASE -   1, "Invalid state" ) \
   NIFLEXRIO_STATUS( NoIOModulePower                 , NIFLEXRIO_ERROR_BASE -   2, "No IO module power" ) \
   NIFLEXRIO_STATUS( I2CBusTimeout                   , NIFLEXRIO_ERROR_BASE -   3, "I2C bus timeout" ) \
   NIFLEXRIO_STATUS( NoI2CAck                        , NIFLEXRIO_ERROR_BASE -   4, "No I2C ACK" ) \
   NIFLEXRIO_STATUS( I2CArbiterTimeout               , NIFLEXRIO_ERROR_BASE -   5, "I2C arbiter timeout" ) \
   NIFLEXRIO_STATUS( IOModuleMgrTimeout              , NIFLEXRIO_ERROR_BASE -   6, "IO Module manager timeout" ) \
   NIFLEXRIO_STATUS( NoEEPROMPower                   , NIFLEXRIO_ERROR_BASE -   7, "No EEPROM power" ) \
   NIFLEXRIO_STATUS( EEPROMWriteTimeout              , NIFLEXRIO_ERROR_BASE -   8, "EEPROM write timeout" ) \
   NIFLEXRIO_STATUS( DefaultBitstreamNotFound        , NIFLEXRIO_ERROR_BASE -   9, "Default bitstream not found" ) \
   NIFLEXRIO_STATUS( IncompatibleDevice              , NIFLEXRIO_ERROR_BASE -  10, "Incompatible device" ) \
   NIFLEXRIO_STATUS( NoI2CAccess                     , NIFLEXRIO_ERROR_BASE -  11, "No I2C access" ) \
   NIFLEXRIO_STATUS( InvalidEEPROMAddress            , NIFLEXRIO_ERROR_BASE -  12, "Invalid EEPROM address" ) \
   NIFLEXRIO_STATUS( IOModuleDetectionTimeout        , NIFLEXRIO_ERROR_BASE -  13, "IO module detection timeout" ) \
   NIFLEXRIO_STATUS( Flash_InvalidParameter          , NIFLEXRIO_ERROR_BASE -  14, "Flash invalid parameter" ) \
   NIFLEXRIO_STATUS( Flash_NotSupported              , NIFLEXRIO_ERROR_BASE -  15, "Flash not supported" ) \
   NIFLEXRIO_STATUS( Flash_HardwareFault             , NIFLEXRIO_ERROR_BASE -  16, "Flash hardware fault" ) \
   NIFLEXRIO_STATUS( MemoryFull                      , NIFLEXRIO_ERROR_BASE -  17, "Memory full" ) \
   NIFLEXRIO_STATUS( Flash_InvalidOffset             , NIFLEXRIO_ERROR_BASE -  18, "Invalid offset" ) \
   NIFLEXRIO_STATUS( Flash_WriteFail                 , NIFLEXRIO_ERROR_BASE -  19, "Write fail" ) \
   NIFLEXRIO_STATUS( InvalidFunction                 , NIFLEXRIO_ERROR_BASE -  20, "Invalid function" ) \
   NIFLEXRIO_STATUS( NotAllowedToTalkToHardware      , NIFLEXRIO_ERROR_BASE -  21, "Not allowed to talk to hardware" ) \
   NIFLEXRIO_STATUS( I2CAlreadyInUse                 , NIFLEXRIO_ERROR_BASE -  22, "The I2C bus is already in use.  Please try again after the current I2C bus owner releases access." ) \
   NIFLEXRIO_STATUS( Unused23                        , NIFLEXRIO_ERROR_BASE -  23, "Unexpected software error.  This error code is currently unused.  If this error continues to occur, please contact National Instruments." ) \
   NIFLEXRIO_STATUS( Unused24                        , NIFLEXRIO_ERROR_BASE -  24, "Unexpected software error.  This error code is currently unused.  If this error continues to occur, please contact National Instruments." ) \
   NIFLEXRIO_STATUS( Unused25                        , NIFLEXRIO_ERROR_BASE -  25, "Unexpected software error.  This error code is currently unused.  If this error continues to occur, please contact National Instruments." ) \
   NIFLEXRIO_STATUS( Unused26                        , NIFLEXRIO_ERROR_BASE -  26, "Unexpected software error.  This error code is currently unused.  If this error continues to occur, please contact National Instruments." ) \
   NIFLEXRIO_STATUS( FpgaDownloadDisallowed          , NIFLEXRIO_ERROR_BASE -  27, "FPGA download is disallowed" ) \
   NIFLEXRIO_STATUS( InitializationFailed            , NIFLEXRIO_ERROR_BASE -  28, "Initialization failed" ) \
   NIFLEXRIO_STATUS( InitializationNotPerformed      , NIFLEXRIO_ERROR_BASE -  29, "Initialization must be performed before continuing" ) \
   NIFLEXRIO_STATUS( FirmwareUpdateRequired          , NIFLEXRIO_ERROR_BASE -  30, "A firmware update is required"  ) \
   NIFLEXRIO_STATUS( SerialNumberIsCorrupt           , NIFLEXRIO_ERROR_BASE -  31, "The serial number is either missing or formatted incorrectly." ) \
   NIFLEXRIO_STATUS( InvalidInterfaceNumber          , NIFLEXRIO_ERROR_BASE -  32, "The RIO interface number is invalid.  Has RIO provided an interface number for this device yet?" ) \
   NIFLEXRIO_STATUS( UnknownSoftwareError            , NIFLEXRIO_ERROR_BASE -  33, "[Internal] There has been an unexpected software error.  If this error continues to occur, please contact National Instruments." ) \
   NIFLEXRIO_STATUS( Tbc_ReadFileFailed              , NIFLEXRIO_ERROR_BASE -  34, "Failed to read the TBC/FAM file.  This could be due to a bad TBC/FAM file path.  Double check the TBC/FAM file path and try again." ) \
   NIFLEXRIO_STATUS( Tbc_ReadPropertyFailed          , NIFLEXRIO_ERROR_BASE -  35, "Failed to read a property from the TBC/FAM file.  This could be due to a malformed TBC/FAM file.  Double check the TBC/FAM file and try again." ) \
   NIFLEXRIO_STATUS( FailedToRegisterRTSetCleanupProc, NIFLEXRIO_ERROR_BASE -  36, "[Internal] Registering with RTSetCleanupProc failed." ) \
   NIFLEXRIO_STATUS( InvalidSubsystem                , NIFLEXRIO_ERROR_BASE -  37, "The requested subsytem is invalid." ) \
   NIFLEXRIO_STATUS( BitStreamTooLarge               , NIFLEXRIO_ERROR_BASE -  38, "The size of the bitfile is not compatible with this FPGA model. A corrupt bitfile may be causing this error. Please recompile the bitfile." ) \
   \
   /* flexrioshared error codes */ \
   NIFLEXRIO_STATUS( FailedToGetDeviceInterfacePath  , NIFLEXRIO_ERROR_BASE -  50, "The device interface path for a device with this RIO interface number was not found" ) \
   NIFLEXRIO_STATUS( FailedToGetDeviceInfoFromHandle , NIFLEXRIO_ERROR_BASE -  51, "[Internal] The tDeviceInfo for this handle was not found" ) \
   NIFLEXRIO_STATUS( InsufficientPrivileges          , NIFLEXRIO_ERROR_BASE -  52, "The requested operation failed due to insufficient privileges." ) \
   NIFLEXRIO_STATUS( InvalidAccessModeRequested      , NIFLEXRIO_ERROR_BASE -  53, "The requested access mode is invalid." ) \
   NIFLEXRIO_STATUS( InvalidMemoryMappingRequestType , NIFLEXRIO_ERROR_BASE -  54, "The requested memory mapping type is invalid." ) \
   NIFLEXRIO_STATUS( RequestedAccessOutOfBounds      , NIFLEXRIO_ERROR_BASE -  55, "The requested memory being accessed is out of bounds." ) \
   \
   /* usb driver error codes */ \
   NIFLEXRIO_STATUS( UsbDeviceStateNotOpen           , NIFLEXRIO_ERROR_BASE - 101, "The device's state is not open" ) \
   NIFLEXRIO_STATUS( UsbDeviceStateInvalid           , NIFLEXRIO_ERROR_BASE - 102, "The device's state is invalid" ) \
   NIFLEXRIO_STATUS( UsbDeviceStateUnknown           , NIFLEXRIO_ERROR_BASE - 103, "The device's state is unknown" ) \
   NIFLEXRIO_STATUS( UsbInvalidDeviceInfo            , NIFLEXRIO_ERROR_BASE - 110, "Failed to get device info from handle" ) \
   NIFLEXRIO_STATUS( UsbDevicesCountMismatch         , NIFLEXRIO_ERROR_BASE - 111, "The number of devices identified is different than the number of devices given" ) \
   \
   /* chinch driver error codes */ \
   NIFLEXRIO_STATUS( Series7CommitRegisterFailure    , NIFLEXRIO_ERROR_BASE - 125, "The commit register failed to return the expected constant. Dynamic PCIe Reconfiguration failed." ) \
   NIFLEXRIO_STATUS( Series7FlashUserImageIsCorrupt  , NIFLEXRIO_ERROR_BASE - 126, "The user image in flash is corrupt. You cannot set the FPGA to load from the user image. Use the RIO Device setup utility to download a new image to the flash." ) \
   NIFLEXRIO_STATUS( RioFpgaDeviceSupportNULL        , NIFLEXRIO_ERROR_BASE - 150, "An internal pointer was NULL." ) \
   NIFLEXRIO_STATUS( InvalidMemoryAccessSize         , NIFLEXRIO_ERROR_BASE - 151, "The requested memory access size is invalid." ) \
   NIFLEXRIO_STATUS( CpldVersionIncompatibility      , NIFLEXRIO_ERROR_BASE - 152, "Failed to download the FPGA bitfile. The current FPGA bitfile is not fully compatible with the HW." ) \
   NIFLEXRIO_STATUS( FailedReconfigLoadedFlashImage  , NIFLEXRIO_ERROR_BASE - 153, "Reconfiguration of FPGA failed. HW reverted to flash image." ) \
   NIFLEXRIO_STATUS( FailedReadFixedLogicSignature   , NIFLEXRIO_ERROR_BASE - 154, "Reading the Fixed Logic Signature failed. The signature does not match what is expected. The Chinch is most likely dead." ) \
   NIFLEXRIO_STATUS( FPGADownloadNotSupported        , NIFLEXRIO_ERROR_BASE - 155, "Downloading to the FPGA is not supported on this OS. Please use the RIO Device Setup Utility from a Windows machine to download your bitfile to the flash through RPC, and then power-cycle your machine in order to load the bitfile onto the FPGA." ) \
   NIFLEXRIO_STATUS( FixedLogicVersionMismatch       , NIFLEXRIO_ERROR_BASE - 156, "The FPGA program loaded on the device was built with an incompatible NI FlexRIO Support.  You must rebuild this FPGA VI with the currently installed NI FlexRIO Support.") \
   NIFLEXRIO_STATUS( HardwareVersionMismatch         , NIFLEXRIO_ERROR_BASE - 157, "The FPGA Module is too new to work with this version of NI FlexRIO Support.  Upgrade your NI FlexRIO Support.") \
   \
   /* mite driver error codes */ \
   NIFLEXRIO_STATUS( RioDeviceNULL                   , NIFLEXRIO_ERROR_BASE - 170, "An internal pointer was NULL." ) \



//! X-Macro to create an enum of status names and codes
//! NOTE: If the compiler complains about the (extra) comma, a dummy status may be added at the end.  This is currently the case.
//!
#define NIFLEXRIO_STATUS(status_name, status_code, status_description) NIFLEXRIO_Status_ ## status_name = (status_code),
typedef enum
{
    NIFLEXRIO_STATUS_TABLE
    NIFLEXRIO_STATUS_MemGuard = 0xFFFFFFFF //!< A final value used as a memory guard.
} NIFLEXRIO_Status;
#undef NIFLEXRIO_STATUS

// generate
// static const char *const NIFLEXRIO_Status_NoIoModule_String = "NIFLEXRIO_Status_NoIoModule: \"No IO module\"";
// etc. for all codes
#define NIFLEXRIO_STATUS(status_name, status_code, status_description) \
   static const char *const NIFLEXRIO_Status_ ## status_name ## _String = "NIFLEXRIO_Status_" #status_name ": " #status_description;
NIFLEXRIO_STATUS_TABLE
#undef NIFLEXRIO_STATUS

} // namespace niflexrio
