#include <iostream>
#include <Windows.h>
#include "C:\\Users\\pompompurin\\source\\repos\\Zero\\Zero\\ZeroCommon.h"

int Error(const char* msg)
{
	printf("%s: error=%u\n", msg, ::GetLastError());
	return 1;
}

int main() {
	HANDLE hDevice = CreateFile(L"\\\\.\\Zero", GENERIC_READ | GENERIC_WRITE,
		0, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hDevice == INVALID_HANDLE_VALUE) {
		return Error("Failed to open device");
	}

	//test read
	BYTE buffer[64];

	//store some non-zero data
	for (int i = 0; i < sizeof(buffer); ++i)
		buffer[i] = i + 1;

	DWORD bytes;
	BOOL ok = ReadFile(hDevice, buffer, sizeof(buffer), &bytes, nullptr);
	if (!ok)
		return Error("failed to read");
	//if (bytes != sizeof(buffer))
		//printf("Wrong number of bytes\n");

	//check all bytes are non-zero
	for (auto n : buffer)
		if (n != 0) {
			printf("Wrong data\n");
			break;
		}

	//test write
	BYTE buffer2[1024]; //contains junk
	ok = WriteFile(hDevice, buffer2, sizeof(buffer2), &bytes, nullptr);
	if (!ok)
		return Error("failed to write");
	//if (bytes != sizeof(buffer))
		//printf("Wrong byte count!\n");

	ZeroStats stats;
	if (!DeviceIoControl(hDevice, IOCTL_ZERO_GET_STATS,
		nullptr, 0, &stats, sizeof(stats), &bytes, nullptr))
		return Error("failed in DeviceIoControl");

	printf("Total Read: %lld, TotalWrite: %lld\n", stats.TotalRead, stats.TotalWritten);


	CloseHandle(hDevice);
}