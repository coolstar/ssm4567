#if !defined(_SSM4567_H_)
#define _SSM4567_H_

#pragma warning(disable:4200)  // suppress nameless struct/union warning
#pragma warning(disable:4201)  // suppress nameless struct/union warning
#pragma warning(disable:4214)  // suppress bit field types other than int warning
#include <initguid.h>
#include <wdm.h>

#pragma warning(default:4200)
#pragma warning(default:4201)
#pragma warning(default:4214)
#include <wdf.h>

#include <portcls.h>

#include <acpiioct.h>
#include <ntstrsafe.h>

#include <stdint.h>

#include "spb.h"

#define JACKDESC_RGB(r, g, b) \
    ((COLORREF)((r << 16) | (g << 8) | (b)))

//
// String definitions
//

#define DRIVERNAME                 "ssm4567.sys: "

#define SSM4567_POOL_TAG            (ULONG) 'B343'

#define true 1
#define false 0

#pragma pack(push,1)
typedef struct _IntcSSTArg
{
	int32_t chipModel;
	int32_t sstQuery;
	int32_t caller;
	int32_t querySize;

#ifdef __GNUC__
	char EndOfHeader[0];
#endif

	uint8_t deviceInD0;
#ifdef __GNUC__
	char EndOfPowerCfg[0];
#endif

	int32_t dword11;
	GUID guid;

#ifdef __GNUC__
	char EndOfGUID[0];
#endif
	uint8_t byte25;
	int32_t dword26;
	int32_t dword2A;
	int32_t dword2E;
	int32_t dword32;
	int32_t dword36;
	int32_t dword3A;
	int32_t dword3E;
	uint8_t byte42;
	uint8_t byte43;
	char padding[90]; //idk what this is for
}  IntcSSTArg, * PIntcSSTArg;
#pragma pack(pop)

typedef struct _SSM4567_CONTEXT
{

	WDFDEVICE FxDevice;

	WDFQUEUE ReportQueue;

	SPB_CONTEXT I2CContext;

	BOOLEAN SetUID;
	INT32 UID;

	BOOLEAN DevicePoweredOn;
	INT8 IntcSSTStatus;

	WDFWORKITEM IntcSSTWorkItem;
	PCALLBACK_OBJECT IntcSSTHwMultiCodecCallback;
	PVOID IntcSSTCallbackObj;

	IntcSSTArg sstArgTemp;

} SSM4567_CONTEXT, *PSSM4567_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SSM4567_CONTEXT, GetDeviceContext)

//
// Function definitions
//

DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_UNLOAD Ssm4567DriverUnload;

EVT_WDF_DRIVER_DEVICE_ADD Ssm4567EvtDeviceAdd;

EVT_WDFDEVICE_WDM_IRP_PREPROCESS Ssm4567EvtWdmPreprocessMnQueryId;

EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL Ssm4567EvtInternalDeviceControl;

//
// Helper macros
//

#define DEBUG_LEVEL_ERROR   1
#define DEBUG_LEVEL_INFO    2
#define DEBUG_LEVEL_VERBOSE 3

#define DBG_INIT  1
#define DBG_PNP   2
#define DBG_IOCTL 4

#if 0
#define Ssm4567Print(dbglevel, dbgcatagory, fmt, ...) {          \
    if (Ssm4567DebugLevel >= dbglevel &&                         \
        (Ssm4567DebugCatagories && dbgcatagory))                 \
	    {                                                           \
        DbgPrint(DRIVERNAME);                                   \
        DbgPrint(fmt, __VA_ARGS__);                             \
	    }                                                           \
}
#else
#define Ssm4567Print(dbglevel, fmt, ...) {                       \
}
#endif

#endif