#include "pch.h"
#include "ZeroCommon.h"
#define DRIVER_PREFIX "ZERO: "

long long g_TotalRead;
long long g_TotalWritten;

//prototypes
NTSTATUS CompleteIrp(
	PIRP Irp,
	NTSTATUS status = STATUS_SUCCESS,
	ULONG_PTR info = 0
) {
	Irp->IoStatus.Status = status;		//default value
	Irp->IoStatus.Information = info;	//default value
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}

NTSTATUS ZeroCreateClose(PDEVICE_OBJECT, PIRP Irp)
{
	return CompleteIrp(Irp);
}

NTSTATUS ZeroRead(PDEVICE_OBJECT, PIRP Irp) {
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto len = stack->Parameters.Read.Length;
	if (len == 0)	//if length is zero complete the irp with a failure status
		return CompleteIrp(Irp, STATUS_INVALID_BUFFER_SIZE);

	//we have configured direct i/o so we need to map the locked buffer to sys space using mmgetaddressformdlsafe
	NT_ASSERT(Irp->MdlAddress); //making sure the direct i/o flag was set
	auto buffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
	if (!buffer)
		return CompleteIrp(Irp, STATUS_INSUFFICIENT_RESOURCES);

	//the functionality we need to implement is to zero out the given buffer. we can use simple memset to call to fill buffer with zeros and complete request
	memset(buffer, 0, len);

	InterlockedAdd64(&g_TotalRead, len);
	return CompleteIrp(Irp, STATUS_SUCCESS, len);
}

NTSTATUS ZeroWrite(PDEVICE_OBJECT, PIRP Irp)
{
	//complete the request with buffer length provided by client (swallows the buffer)
		//we dont use mmgetsystemaddressformdlsafe bc we dont access the actual buffer. the driver may not need it
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto len = stack->Parameters.Write.Length;
	
	//update number of bytes written
	InterlockedAdd64(&g_TotalWritten, len);
	return CompleteIrp(Irp, STATUS_SUCCESS, len);
}

NTSTATUS ZeroDeviceControl(PDEVICE_OBJECT, PIRP Irp) {
	auto irpSp = IoGetCurrentIrpStackLocation(Irp);	//details for devicecontrol are in current i/o stack location in the param.deviceiocontrol struct
	auto& dic = irpSp->Parameters.DeviceIoControl;
	auto status = STATUS_INVALID_DEVICE_REQUEST;	//initalized to error in case the control code is unsupported
	ULONG_PTR len = 0;	//keeps trac of number of valid bytes returned to output buffer

	switch (dic.IoControlCode) {
	case IOCTL_ZERO_GET_STATS:
		{	//artificial scope so the compiler does not complain about defining variables skipped by a case
		if (dic.OutputBufferLength < sizeof(ZeroStats)) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
			}

		auto stats = (ZeroStats*)Irp->AssociatedIrp.SystemBuffer;
		if (stats == nullptr)
		{
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		//fill in the output buffer
		stats->TotalRead = g_TotalRead;
		stats->TotalWritten = g_TotalWritten;
		len = sizeof(ZeroStats);

		//change status to indicate success
		status = STATUS_SUCCESS;
		break;
		}

	case IOCTL_ZERO_CLEAR_STATS:
		g_TotalRead = g_TotalWritten = 0;
		status = STATUS_SUCCESS;
		break;
	}

	return CompleteIrp(Irp, status, len);
}

//driverEntry
extern "C" NTSTATUS
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
	UNREFERENCED_PARAMETER(RegistryPath);

	//DriverObject->DriverUnload = ZeroUnload;
	DriverObject->MajorFunction[IRP_MJ_CREATE] =
		DriverObject->MajorFunction[IRP_MJ_CLOSE] = ZeroCreateClose;
	DriverObject->MajorFunction[IRP_MJ_READ] = ZeroRead;
	DriverObject->MajorFunction[IRP_MJ_WRITE] = ZeroWrite;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = ZeroDeviceControl;

	UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\Zero");
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\Zero");
	PDEVICE_OBJECT DeviceObject = nullptr;
	auto status = STATUS_SUCCESS;

	do {
		status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, FALSE, &DeviceObject);
		if (!NT_SUCCESS(status))
		{
			KdPrint((DRIVER_PREFIX "failed to create device (0x%08X)\n", status));
			break;
		}

		//set up direct i/o
		DeviceObject->Flags |= DO_DIRECT_IO;

		status = IoCreateSymbolicLink(&symLink, &devName);
		if (!NT_SUCCESS(status))
		{
			KdPrint((DRIVER_PREFIX "failed to create symbolic link (0x%08X)\n", status));
			break;
		}

	} while (false);

	if (!NT_SUCCESS(status))
	{
		if (DeviceObject)
			IoDeleteDevice(DeviceObject);
	}
	return STATUS_SUCCESS;
}