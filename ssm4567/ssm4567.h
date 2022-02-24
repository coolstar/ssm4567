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

#include <acpiioct.h>
#include <ntstrsafe.h>

#include "spb.h"

//
// String definitions
//

#define DRIVERNAME                 "ssm4567.sys: "

#define SSM4567_POOL_TAG            (ULONG) 'B343'

typedef struct _SSM4567_CONTEXT
{

	WDFDEVICE FxDevice;

	WDFQUEUE ReportQueue;

	SPB_CONTEXT I2CContext;

	INT32 UID;

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

#if 1
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