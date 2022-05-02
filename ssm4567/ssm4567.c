#include "ssm4567.h"
#include "registers.h"

#define bool int

static ULONG Ssm4567DebugLevel = 100;
static ULONG Ssm4567DebugCatagories = DBG_INIT || DBG_PNP || DBG_IOCTL;

NTSTATUS
DriverEntry(
	__in PDRIVER_OBJECT  DriverObject,
	__in PUNICODE_STRING RegistryPath
)
{
	NTSTATUS               status = STATUS_SUCCESS;
	WDF_DRIVER_CONFIG      config;
	WDF_OBJECT_ATTRIBUTES  attributes;

	Ssm4567Print(DEBUG_LEVEL_INFO, DBG_INIT,
		"Driver Entry\n");

	WDF_DRIVER_CONFIG_INIT(&config, Ssm4567EvtDeviceAdd);

	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

	//
	// Create a framework driver object to represent our driver.
	//

	status = WdfDriverCreate(DriverObject,
		RegistryPath,
		&attributes,
		&config,
		WDF_NO_HANDLE
	);

	if (!NT_SUCCESS(status))
	{
		Ssm4567Print(DEBUG_LEVEL_ERROR, DBG_INIT,
			"WdfDriverCreate failed with status 0x%x\n", status);
	}

	return status;
}

NTSTATUS ssm4567_reg_read(
	_In_ PSSM4567_CONTEXT pDevice,
	uint8_t reg,
	uint8_t* data
) {
	uint8_t raw_data = 0;
	NTSTATUS status = SpbXferDataSynchronously(&pDevice->I2CContext, &reg, sizeof(uint8_t), &raw_data, sizeof(uint8_t));
	*data = raw_data;
	return status;
}

NTSTATUS ssm4567_reg_write(
	_In_ PSSM4567_CONTEXT pDevice,
	uint8_t reg,
	uint8_t data
) {
	uint8_t buf[2];
	buf[0] = reg;
	buf[1] = data;
	return SpbWriteDataSynchronously(&pDevice->I2CContext, buf, sizeof(buf));
}

NTSTATUS ssm4567_reg_update(
	_In_ PSSM4567_CONTEXT pDevice,
	uint8_t reg,
	uint8_t mask,
	uint8_t val
) {
	uint8_t tmp = 0, orig = 0;

	NTSTATUS status = ssm4567_reg_read(pDevice, reg, &orig);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	tmp = orig & ~mask;
	tmp |= val & mask;

	if (tmp != orig) {
		status = ssm4567_reg_write(pDevice, reg, tmp);
	}
	return status;
}

NTSTATUS ssm4567_set_power(
	_In_ PSSM4567_CONTEXT pDevice,
	_In_ BOOLEAN enable
) {
	NTSTATUS status;
	if (enable) {
		status = ssm4567_reg_write(pDevice, SSM4567_REG_SOFT_RESET, 0x00);
		if (!NT_SUCCESS(status)) {
			return status;
		}
	}

	status = ssm4567_reg_update(pDevice, SSM4567_REG_POWER_CTRL,
		SSM4567_POWER_SPWDN, enable ? 0 : SSM4567_POWER_SPWDN);
	return status;
}

NTSTATUS
GetDeviceUID(
	_In_ WDFDEVICE FxDevice,
	_In_ PINT32 PUID
)
{
	NTSTATUS status = STATUS_ACPI_NOT_INITIALIZED;
	ACPI_EVAL_INPUT_BUFFER_EX inputBuffer;
	RtlZeroMemory(&inputBuffer, sizeof(inputBuffer));

	inputBuffer.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE_EX;
	status = RtlStringCchPrintfA(
		inputBuffer.MethodName,
		sizeof(inputBuffer.MethodName),
		"_UID"
	);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	WDFMEMORY outputMemory;
	PACPI_EVAL_OUTPUT_BUFFER outputBuffer;
	size_t outputArgumentBufferSize = 32;
	size_t outputBufferSize = FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) + outputArgumentBufferSize;

	WDF_OBJECT_ATTRIBUTES attributes;
	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	attributes.ParentObject = FxDevice;

	status = WdfMemoryCreate(&attributes,
		NonPagedPoolNx,
		0,
		outputBufferSize,
		&outputMemory,
		(PVOID*)&outputBuffer);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	RtlZeroMemory(outputBuffer, outputBufferSize);

	WDF_MEMORY_DESCRIPTOR inputMemDesc;
	WDF_MEMORY_DESCRIPTOR outputMemDesc;
	WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&inputMemDesc, &inputBuffer, (ULONG)sizeof(inputBuffer));
	WDF_MEMORY_DESCRIPTOR_INIT_HANDLE(&outputMemDesc, outputMemory, NULL);

	status = WdfIoTargetSendInternalIoctlSynchronously(
		WdfDeviceGetIoTarget(FxDevice),
		NULL,
		IOCTL_ACPI_EVAL_METHOD_EX,
		&inputMemDesc,
		&outputMemDesc,
		NULL,
		NULL
	);
	if (!NT_SUCCESS(status)) {
		goto Exit;
	}

	if (outputBuffer->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE) {
		goto Exit;
	}

	if (outputBuffer->Count < 1) {
		goto Exit;
	}

	uint32_t uid;
	if (outputBuffer->Argument[0].DataLength >= 4) {
		uid = *(uint32_t*)outputBuffer->Argument->Data;
	}
	else if (outputBuffer->Argument[0].DataLength >= 2) {
		uid = *(uint16_t*)outputBuffer->Argument->Data;
	}
	else {
		uid = *(uint8_t*)outputBuffer->Argument->Data;
	}
	if (PUID) {
		*PUID = uid;
	}
	else {
		status = STATUS_ACPI_INVALID_ARGUMENT;
	}
Exit:
	if (outputMemory != WDF_NO_HANDLE) {
		WdfObjectDelete(outputMemory);
	}
	return status;
}

int IntCSSTArg2 = 1;

VOID
UpdateIntcSSTStatus(
	IN PSSM4567_CONTEXT pDevice,
	int sstStatus
) {
	IntcSSTArg* SSTArg = &pDevice->sstArgTemp;
	RtlZeroMemory(SSTArg, sizeof(IntcSSTArg));

	if (pDevice->IntcSSTHwMultiCodecCallback) {
		if (sstStatus != 1 || pDevice->IntcSSTStatus) {
			SSTArg->chipModel = 4567;
			SSTArg->caller = 0xc0000165; //gmaxcodec
			if (sstStatus) {
				if (sstStatus == 1) {
					if (!pDevice->IntcSSTStatus) {
						return;
					}
					SSTArg->sstQuery = 12;
					SSTArg->dword11 = 2;
					SSTArg->querySize = 21;
				}
				else {
					SSTArg->sstQuery = 11;
					SSTArg->querySize = 20;
				}

				SSTArg->deviceInD0 = (pDevice->DevicePoweredOn != 0);
			}
			else {
				SSTArg->sstQuery = 10;
				SSTArg->querySize = 18;
				SSTArg->deviceInD0 = 1;
			}
			ExNotifyCallback(pDevice->IntcSSTHwMultiCodecCallback, SSTArg, &IntCSSTArg2);
		}
	}
}

VOID
IntcSSTWorkItemFunc(
	IN WDFWORKITEM  WorkItem
)
{
	WDFDEVICE Device = (WDFDEVICE)WdfWorkItemGetParentObject(WorkItem);
	PSSM4567_CONTEXT pDevice = GetDeviceContext(Device);

	UpdateIntcSSTStatus(pDevice, 0);
}

DEFINE_GUID(GUID_SST_RTK_1,
	0xDFF21CE2, 0xF70F, 0x11D0, 0xB9, 0x17, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96); //Headphones
DEFINE_GUID(GUID_SST_RTK_2,
	0xDFF21CE1, 0xF70F, 0x11D0, 0xB9, 0x17, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96); //InsideMobileLid
DEFINE_GUID(GUID_SST_RTK_3,
	0xDFF21BE1, 0xF70F, 0x11D0, 0xB9, 0x17, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96); //Also InsideMobileLid?
DEFINE_GUID(GUID_SST_RTK_4,
	0xDFF21FE3, 0xF70F, 0x11D0, 0xB9, 0x17, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96); //Line out

VOID
IntcSSTCallbackFunction(
	IN WDFWORKITEM  WorkItem,
	IntcSSTArg* SSTArgs,
	PVOID Argument2
) {
	if (!WorkItem) {
		return;
	}
	WDFDEVICE Device = (WDFDEVICE)WdfWorkItemGetParentObject(WorkItem);
	PSSM4567_CONTEXT pDevice = GetDeviceContext(Device);

	if (Argument2 == &IntCSSTArg2) {
		return;
	}

	//gmaxCodec checks that querySize is greater than 0x10 first thing
	if (SSTArgs->querySize <= 0x10) {
		return;
	}

	//Intel Caller: 0xc00000a3 (STATUS_DEVICE_NOT_READY)
	//GMax Caller: 0xc0000165

	if (SSTArgs->chipModel == 4567) {
		/*

		Gmax (no SST driver):
			init:	sstQuery = 10
					dwordc = 18
					deviceInD0 = 1

			stop:	sstQuery = 11
					dwordc = 20
					deviceInD0 = 0
		*/

		/*

		Gmax (SST driver)
			post-init:	sstQuery = 12
						dwordc = 21
						dword11 = 2

		*/
		if (Argument2 != &IntCSSTArg2) { //Intel SST is calling us
			bool checkCaller = (SSTArgs->caller != 0);

			if (SSTArgs->sstQuery == 11) {
				if (SSTArgs->querySize >= 0x15) {
					if (SSTArgs->deviceInD0 == 0) {
						pDevice->IntcSSTStatus = 0; //SST is inactive
						SSTArgs->caller = STATUS_SUCCESS;
						//mark device as inactive?
					}
					else {
						SSTArgs->caller = STATUS_INVALID_PARAMETER_5;
					}
				}
				else {
					SSTArgs->caller = STATUS_BUFFER_TOO_SMALL;
				}
			}

			//SST Query 1:
			//	sstQuery: 10, querySize: 0x9e, dword11: 0x0
			//	deviceInD0: 0x1, byte25: 0

			if (SSTArgs->sstQuery == 10) { //gmax responds no matter what
				if (SSTArgs->querySize >= 0x15) {
					if (SSTArgs->deviceInD0 == 1) {
						pDevice->IntcSSTStatus = 1;
						SSTArgs->caller = STATUS_SUCCESS;
						//mark device as active??
					}
					else {
						SSTArgs->caller = STATUS_INVALID_PARAMETER_5;
					}
				}
				else {
					SSTArgs->caller = STATUS_BUFFER_TOO_SMALL;
				}
			}

			//SST Query 2:
			//	sstQuery: 2048, querySize: 0x9e, dword11: 0x00
			//	deviceInD0: 0, byte25: 0

			if (SSTArgs->sstQuery == 2048) {
				if (SSTArgs->querySize >= 0x11) {
					SSTArgs->deviceInD0 = 1;
					SSTArgs->caller = STATUS_SUCCESS;
				}
				else {
					SSTArgs->caller = STATUS_BUFFER_TOO_SMALL;
				}
			}

			//SST Query 3:
			//	sstQuery: 2051, querySize: 0x9e, dword11: 0x00
			//	deviceInD0: 0, byte25: 0

			if (SSTArgs->sstQuery == 2051) {
				if (SSTArgs->querySize >= 0x9E) {
					if (SSTArgs->deviceInD0) {
						SSTArgs->caller = STATUS_INVALID_PARAMETER;
					}
					else {

						SSTArgs->deviceInD0 = 0;
						SSTArgs->dword11 = (1 << 24) | 0;

						SSTArgs->guid = GUID_SST_RTK_2;

						SSTArgs->byte25 = 1;
						SSTArgs->dword26 = KSAUDIO_SPEAKER_STEREO; //Channel Mapping
						SSTArgs->dword2A = JACKDESC_RGB(255, 174, 201); //Color (gmax sets to 0)
						SSTArgs->dword2E = eConnTypeOtherAnalog; //EPcxConnectionType
						SSTArgs->dword32 = eGeoLocInsideMobileLid; //EPcxGeoLocation
						SSTArgs->dword36 = eGenLocInternal; //genLocation?
						SSTArgs->dword3A = ePortConnIntegratedDevice; //portConnection?
						SSTArgs->dword3E = 1; //isConnected?
						SSTArgs->byte42 = 0;
						SSTArgs->byte43 = 0;
						SSTArgs->caller = STATUS_SUCCESS;
					}
				}
				else {
					SSTArgs->caller = STATUS_BUFFER_TOO_SMALL;
				}
			}

			//This is the minimum for SST to initialize. Everything after is extra
			//SST Query 4:
			//	sstQuery: 2054, querySize: 0x9e, dword11: 0x00
			//	deviceInD0: 0, byte25: 0
			if (SSTArgs->sstQuery == 2054) {
				if (SSTArgs->querySize >= 0x9E) {
					if (SSTArgs->deviceInD0) {
						SSTArgs->caller = STATUS_INVALID_PARAMETER;
					}
					else {
						SSTArgs->dword11 = 2;
						SSTArgs->caller = STATUS_SUCCESS;
					}
				}
				else {
					SSTArgs->caller = STATUS_BUFFER_TOO_SMALL;
				}
			}

			//SST Query 5:
			//	sstQuery: 2055, querySize: 0x9e, dword11: 0x00
			//	deviceInD0: 0, byte25: 0

			if (SSTArgs->sstQuery == 2055) {
				if (SSTArgs->querySize < 0x22) {
					SSTArgs->caller = STATUS_BUFFER_TOO_SMALL;
				}
				else {
					SSTArgs->caller = STATUS_NOT_SUPPORTED;
				}
			}

			//SST Query 6:
			//	sstQuery: 13, querySize: 0x9e, dword11: 0x00
			//	deviceInD0: 1, byte25: 0
			if (SSTArgs->sstQuery == 13) {
				if (SSTArgs->querySize >= 0x14) {
					if (SSTArgs->deviceInD0) {
						pDevice->IntcSSTStatus = 1;
						SSTArgs->caller = STATUS_SUCCESS;

						//UpdateIntcSSTStatus(pDevice, 1);
					}
					else {
						SSTArgs->caller = STATUS_INVALID_PARAMETER;
					}
				}
				else {
					SSTArgs->caller = STATUS_BUFFER_TOO_SMALL;
				}
			}

			//SST Query 7:
			//	sstQuery: 2064, querySize: 0x9e, dword11: 0x00
			//	deviceInD0: 0, byte25: 0
			if (SSTArgs->sstQuery == 2064) {
				if (SSTArgs->querySize >= 0x19) {
					if (!SSTArgs->deviceInD0) {
						unsigned int data1 = SSTArgs->guid.Data1;
						//DbgPrint("data1: %d\n", data1);
						if (data1 != -1 && data1 < 1) {
							SSTArgs->dword11 = 0; //no feedback on ssm4567
							SSTArgs->caller = STATUS_SUCCESS;
						}
						else {
							SSTArgs->caller = STATUS_INVALID_PARAMETER;
						}
					}
					else {
						SSTArgs->caller = STATUS_INVALID_PARAMETER;
					}
				}
				else {
					SSTArgs->caller = STATUS_BUFFER_TOO_SMALL;
				}
			}

			if (checkCaller) {
				if (SSTArgs->caller != STATUS_SUCCESS) {
					//DbgPrint("Warning: Returned error 0x%x; query: %d\n", SSTArgs->caller, SSTArgs->sstQuery);
				}
			}
		}
	}
	else {
		//On SST Init: chipModel = 0, caller = 0xc00000a3, sstQuery = 10, dwordc: 0x9e

		if (SSTArgs->sstQuery == 10 && pDevice->IntcSSTWorkItem) {
			WdfWorkItemEnqueue(pDevice->IntcSSTWorkItem); //SST driver was installed after us...
		}
	}
}

NTSTATUS
OnPrepareHardware(
	_In_  WDFDEVICE     FxDevice,
	_In_  WDFCMRESLIST  FxResourcesRaw,
	_In_  WDFCMRESLIST  FxResourcesTranslated
)
/*++

Routine Description:

This routine caches the SPB resource connection ID.

Arguments:

FxDevice - a handle to the framework device object
FxResourcesRaw - list of translated hardware resources that
the PnP manager has assigned to the device
FxResourcesTranslated - list of raw hardware resources that
the PnP manager has assigned to the device

Return Value:

Status

--*/
{
	PSSM4567_CONTEXT pDevice = GetDeviceContext(FxDevice);
	BOOLEAN fSpbResourceFound = FALSE;
	NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;

	UNREFERENCED_PARAMETER(FxResourcesRaw);

	//
	// Parse the peripheral's resources.
	//

	ULONG resourceCount = WdfCmResourceListGetCount(FxResourcesTranslated);

	for (ULONG i = 0; i < resourceCount; i++)
	{
		PCM_PARTIAL_RESOURCE_DESCRIPTOR pDescriptor;
		UCHAR Class;
		UCHAR Type;

		pDescriptor = WdfCmResourceListGetDescriptor(
			FxResourcesTranslated, i);

		switch (pDescriptor->Type)
		{
		case CmResourceTypeConnection:
			//
			// Look for I2C or SPI resource and save connection ID.
			//
			Class = pDescriptor->u.Connection.Class;
			Type = pDescriptor->u.Connection.Type;
			if (Class == CM_RESOURCE_CONNECTION_CLASS_SERIAL &&
				Type == CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C)
			{
				if (fSpbResourceFound == FALSE)
				{
					status = STATUS_SUCCESS;
					pDevice->I2CContext.I2cResHubId.LowPart = pDescriptor->u.Connection.IdLowPart;
					pDevice->I2CContext.I2cResHubId.HighPart = pDescriptor->u.Connection.IdHighPart;
					fSpbResourceFound = TRUE;
				}
				else
				{
				}
			}
			break;
		default:
			//
			// Ignoring all other resource types.
			//
			break;
		}
	}

	//
	// An SPB resource is required.
	//

	if (fSpbResourceFound == FALSE)
	{
		status = STATUS_NOT_FOUND;
	}

	status = SpbTargetInitialize(FxDevice, &pDevice->I2CContext);

	if (!NT_SUCCESS(status))
	{
		return status;
	}

	status = GetDeviceUID(FxDevice, &pDevice->UID);
	if (!NT_SUCCESS(status)) {
		return status;
	}
	pDevice->SetUID = TRUE;

	if (pDevice->UID == 0) {
		WDF_OBJECT_ATTRIBUTES attributes;
		WDF_WORKITEM_CONFIG workitemConfig;

		WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
		WDF_OBJECT_ATTRIBUTES_SET_CONTEXT_TYPE(&attributes, SSM4567_CONTEXT);
		attributes.ParentObject = FxDevice;
		WDF_WORKITEM_CONFIG_INIT(&workitemConfig, IntcSSTWorkItemFunc);
		status = WdfWorkItemCreate(&workitemConfig,
			&attributes,
			&pDevice->IntcSSTWorkItem);
		if (!NT_SUCCESS(status))
		{
			return status;
		}
	}

	return status;
}

NTSTATUS
OnReleaseHardware(
	_In_  WDFDEVICE     FxDevice,
	_In_  WDFCMRESLIST  FxResourcesTranslated
)
/*++

Routine Description:

Arguments:

FxDevice - a handle to the framework device object
FxResourcesTranslated - list of raw hardware resources that
the PnP manager has assigned to the device

Return Value:

Status

--*/
{
	PSSM4567_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	UNREFERENCED_PARAMETER(FxResourcesTranslated);

	if (pDevice->SetUID && pDevice->UID == 0) {
		UpdateIntcSSTStatus(pDevice, 2);
	}

	SpbTargetDeinitialize(FxDevice, &pDevice->I2CContext);

	if (pDevice->IntcSSTCallbackObj) {
		ExUnregisterCallback(pDevice->IntcSSTCallbackObj);
		pDevice->IntcSSTCallbackObj = NULL;
	}

	if (pDevice->IntcSSTWorkItem) {
		WdfWorkItemFlush(pDevice->IntcSSTWorkItem);
		WdfObjectDelete(pDevice->IntcSSTWorkItem);
		pDevice->IntcSSTWorkItem = NULL;
	}

	if (pDevice->IntcSSTHwMultiCodecCallback) {
		ObfDereferenceObject(pDevice->IntcSSTHwMultiCodecCallback);
		pDevice->IntcSSTHwMultiCodecCallback = NULL;
	}

	return status;
}

NTSTATUS
OnSelfManagedIoInit(
	_In_
	WDFDEVICE FxDevice
) {
	PSSM4567_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	if (!pDevice->SetUID) {
		status = STATUS_INVALID_DEVICE_STATE;
		return status;
	}

	if (pDevice->UID == 0) { //Hook onto the first SSM codec
		UNICODE_STRING IntcAudioSSTMultiHwCodecAPI;
		RtlInitUnicodeString(&IntcAudioSSTMultiHwCodecAPI, L"\\CallBack\\IntcAudioSSTMultiHwCodecAPI");


		OBJECT_ATTRIBUTES attributes;
		InitializeObjectAttributes(&attributes,
			&IntcAudioSSTMultiHwCodecAPI,
			OBJ_KERNEL_HANDLE | OBJ_OPENIF | OBJ_CASE_INSENSITIVE | OBJ_PERMANENT,
			NULL,
			NULL
		);
		status = ExCreateCallback(&pDevice->IntcSSTHwMultiCodecCallback, &attributes, TRUE, TRUE);
		if (!NT_SUCCESS(status)) {

			return status;
		}

		pDevice->IntcSSTCallbackObj = ExRegisterCallback(pDevice->IntcSSTHwMultiCodecCallback,
			IntcSSTCallbackFunction,
			pDevice->IntcSSTWorkItem
		);
		if (!pDevice->IntcSSTCallbackObj) {

			return STATUS_NO_CALLBACK_ACTIVE;
		}

		UpdateIntcSSTStatus(pDevice, 0);
	}

	return status;
}

NTSTATUS
OnD0Entry(
	_In_  WDFDEVICE               FxDevice,
	_In_  WDF_POWER_DEVICE_STATE  FxPreviousState
)
/*++

Routine Description:

This routine allocates objects needed by the driver.

Arguments:

FxDevice - a handle to the framework device object
FxPreviousState - previous power state

Return Value:

Status

--*/
{
	UNREFERENCED_PARAMETER(FxPreviousState);

	PSSM4567_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	if (!pDevice->SetUID) {
		status = STATUS_INVALID_DEVICE_STATE;
		return status;
	}

	//Power on Amp
	status = ssm4567_set_power(pDevice, TRUE);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	//Set Format
	uint8_t fmt = SSM4567_SAI_CTRL_1_BCLK | SSM4567_SAI_CTRL_1_TDM;

	status = ssm4567_reg_update(pDevice, SSM4567_REG_SAI_CTRL_1,
		SSM4567_SAI_CTRL_1_BCLK |
		SSM4567_SAI_CTRL_1_FSYNC |
		SSM4567_SAI_CTRL_1_LJ |
		SSM4567_SAI_CTRL_1_TDM |
		SSM4567_SAI_CTRL_1_PDM,
		fmt);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	unsigned int rate = 48000; //48Khz 24 bit for SST
	uint8_t format = SSM4567_DAC_FS_32000_48000;
	if (rate >= 8000 && rate <= 12000)
		format = SSM4567_DAC_FS_8000_12000;
	else if (rate >= 16000 && rate <= 24000)
		format = SSM4567_DAC_FS_16000_24000;
	else if (rate >= 32000 && rate <= 48000)
		format = SSM4567_DAC_FS_32000_48000;
	else if (rate >= 64000 && rate <= 96000)
		format = SSM4567_DAC_FS_64000_96000;
	else if (rate >= 128000 && rate <= 192000)
		format = SSM4567_DAC_FS_128000_192000;
	status = ssm4567_reg_update(pDevice, SSM4567_REG_DAC_CTRL,
		SSM4567_DAC_FS_MASK,
		format);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	//Set TDM Slot
	uint8_t slot = (uint8_t)pDevice->UID;
	status = ssm4567_reg_update(pDevice, SSM4567_REG_SAI_CTRL_2, SSM4567_SAI_CTRL_2_AUTO_SLOT | SSM4567_SAI_CTRL_2_TDM_SLOT_MASK, SSM4567_SAI_CTRL_2_TDM_SLOT(slot));
	if (!NT_SUCCESS(status)) {
		return status;
	}

	//Set width to 48
	status = ssm4567_reg_update(pDevice, SSM4567_REG_SAI_CTRL_1, SSM4567_SAI_CTRL_1_TDM_BLCKS_MASK, SSM4567_SAI_CTRL_1_TDM_BLCKS_48);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	//Ensure unmuted
	status = ssm4567_reg_update(pDevice, SSM4567_REG_DAC_CTRL, SSM4567_DAC_MUTE, 0);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	//Enable high pass filter & low power mode
	status = ssm4567_reg_update(pDevice, SSM4567_REG_DAC_CTRL, SSM4567_DAC_HPF | SSM4567_DAC_LPM, SSM4567_DAC_HPF | SSM4567_DAC_LPM);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	//Enable Amp Boost
	status = ssm4567_reg_update(pDevice, SSM4567_REG_POWER_CTRL, SSM4567_POWER_BOOST_PWDN, 0);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	//Disable Battery/Voltage/Current Sense
	status = ssm4567_reg_update(pDevice, SSM4567_REG_POWER_CTRL,
		SSM4567_POWER_BSNS_PWDN |
		SSM4567_POWER_VSNS_PWDN |
		SSM4567_POWER_ISNS_PWDN,
		SSM4567_POWER_BSNS_PWDN |
		SSM4567_POWER_VSNS_PWDN |
		SSM4567_POWER_ISNS_PWDN);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	//Set Default Volume
	status = ssm4567_reg_update(pDevice, SSM4567_REG_DAC_VOLUME, 0xFF, 0x40); //0x40 = 0 db
	if (!NT_SUCCESS(status)) {
		return status;
	}

	/*for (int i = 0; i <= 0x16; i++) {
		unsigned int data;
		if (NT_SUCCESS(ssm4567_reg_read(pDevice, i, &data))) {
			DbgPrint("Reg 0x%x: 0x%x\n", i, data);
		}
	}*/

	pDevice->DevicePoweredOn = TRUE;

	return status;
}

NTSTATUS
OnD0Exit(
	_In_  WDFDEVICE               FxDevice,
	_In_  WDF_POWER_DEVICE_STATE  FxPreviousState
)
/*++

Routine Description:

This routine destroys objects needed by the driver.

Arguments:

FxDevice - a handle to the framework device object
FxPreviousState - previous power state

Return Value:

Status

--*/
{
	UNREFERENCED_PARAMETER(FxPreviousState);

	PSSM4567_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	status = ssm4567_set_power(pDevice, FALSE);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	pDevice->DevicePoweredOn = FALSE;

	return STATUS_SUCCESS;
}

NTSTATUS
Ssm4567EvtDeviceAdd(
	IN WDFDRIVER       Driver,
	IN PWDFDEVICE_INIT DeviceInit
)
{
	NTSTATUS                      status = STATUS_SUCCESS;
	WDF_IO_QUEUE_CONFIG           queueConfig;
	WDF_OBJECT_ATTRIBUTES         attributes;
	WDFDEVICE                     device;
	WDFQUEUE                      queue;
	PSSM4567_CONTEXT               devContext;

	UNREFERENCED_PARAMETER(Driver);

	PAGED_CODE();

	Ssm4567Print(DEBUG_LEVEL_INFO, DBG_PNP,
		"Ssm4567EvtDeviceAdd called\n");

	{
		WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
		WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);

		pnpCallbacks.EvtDevicePrepareHardware = OnPrepareHardware;
		pnpCallbacks.EvtDeviceReleaseHardware = OnReleaseHardware;
		pnpCallbacks.EvtDeviceSelfManagedIoInit = OnSelfManagedIoInit;
		pnpCallbacks.EvtDeviceD0Entry = OnD0Entry;
		pnpCallbacks.EvtDeviceD0Exit = OnD0Exit;

		WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);
	}

	//
	// Setup the device context
	//

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, SSM4567_CONTEXT);

	//
	// Create a framework device object.This call will in turn create
	// a WDM device object, attach to the lower stack, and set the
	// appropriate flags and attributes.
	//

	status = WdfDeviceCreate(&DeviceInit, &attributes, &device);

	if (!NT_SUCCESS(status))
	{
		Ssm4567Print(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfDeviceCreate failed with status code 0x%x\n", status);

		return status;
	}

	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);

	queueConfig.EvtIoInternalDeviceControl = Ssm4567EvtInternalDeviceControl;

	status = WdfIoQueueCreate(device,
		&queueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&queue
	);

	if (!NT_SUCCESS(status))
	{
		Ssm4567Print(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfIoQueueCreate failed 0x%x\n", status);

		return status;
	}

	//
	// Create manual I/O queue to take care of hid report read requests
	//

	devContext = GetDeviceContext(device);

	devContext->FxDevice = device;

	WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);

	queueConfig.PowerManaged = WdfFalse;

	status = WdfIoQueueCreate(device,
		&queueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&devContext->ReportQueue
	);

	if (!NT_SUCCESS(status))
	{
		Ssm4567Print(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfIoQueueCreate failed 0x%x\n", status);

		return status;
	}

	return status;
}

VOID
Ssm4567EvtInternalDeviceControl(
	IN WDFQUEUE     Queue,
	IN WDFREQUEST   Request,
	IN size_t       OutputBufferLength,
	IN size_t       InputBufferLength,
	IN ULONG        IoControlCode
)
{
	NTSTATUS            status = STATUS_SUCCESS;
	WDFDEVICE           device;
	PSSM4567_CONTEXT     devContext;

	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(InputBufferLength);

	device = WdfIoQueueGetDevice(Queue);
	devContext = GetDeviceContext(device);

	switch (IoControlCode)
	{
	default:
		status = STATUS_NOT_SUPPORTED;
		break;
	}

	WdfRequestComplete(Request, status);

	return;
}