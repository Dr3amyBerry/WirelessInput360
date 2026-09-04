#include <xtl.h>
#include <time.h>
#include <string>
#include <vector>
#include <stdio.h>
#include <sstream>
#include <fstream>
#include <xkelib.h>
#include "Detours.h"
#include <winsockx.h>
#include <sys/stat.h>
#include <stdarg.h>
#pragma comment(lib, "xnet.lib")

#define WIRELESSINPUT360_NETDLL_VERSION 0x20352400

// internal hard disk
#define MOUNT_HDD "Hdd:"
#define DEVICE_HARDISK0_PART1 "\\Device\\Harddisk0\\Partition1"
// usb memory stick
#define MOUNT_USB "Usb:"
#define MOUNT_USB0 "Usb0:"
#define DEVICE_USB0 "\\Device\\Mass0"    
#define MOUNT_USB1 "Usb1:"
#define DEVICE_USB1 "\\Device\\Mass1"
#define MOUNT_USB2 "Usb2:"
#define DEVICE_USB2 "\\Device\\Mass2"
// internal slim trinity mu
#define MOUNT_INTMU "IntMu:"
#define DEVICE_INTMEM "\\Device\\BuiltInMuUsb\\Storage"
// CD / DVD 
#define MOUNT_DVD "Dvd:"
#define DEVICE_CDROM0 "\\Device\\Cdrom0"
// Nand Flash
#define MOUNT_FLASH "Flash:"
#define DEVICE_NAND_FLASH "\\Device\\Flash"
// DEVKIT folder on Hdd
#define MOUNT_DEVKIT "DEVKIT:"
#define DEVICE_DEVKIT "\\Device\\Harddisk0\\Partition1\\DEVKIT"
// Games folder on Hdd
#define MOUNT_HDDGAMES "HddGames:"
#define DEVICE_HDDGAMES "\\Device\\Harddisk0\\Partition1\\Games"
// Apps folder on Hdd
#define MOUNT_HDDAPPS "HddApps:"
#define DEVICE_HDDAPPS "\\Device\\Harddisk0\\Partition1\\Apps"
// Network share using connectx
#define DEVICE_NETSHARE "Netshare:"
#define DEVICE_SMB "\\Network\\Smb"
// internal corona 4g mu
#define MOUNT_MMCMU "MmcMu:"
#define DEVICE_MMCMU "\\Device\\BuiltInMuMmc\\Storage"
// big block NAND mu
#define MOUNT_FLASHMU "FlashMu:"
#define DEVICE_FLASHMU "\\Device\\BuiltInMuSfc"
// memory unit
#define MOUNT_MU "Mu:"
#define DEVICE_MEMORY_UNIT0 "\\Device\\Mu0"
#define MOUNT_MU1 "Mu1:"
#define DEVICE_MEMORY_UNIT1 "\\Device\\Mu1"    
// USB memory unit
#define MOUNT_USBMU0 "UsbMu0:"
#define DEVICE_USBMU0 "\\Device\\Mass0PartitionFile\\Storage"
#define MOUNT_USBMU1 "UsbMu1:"
#define DEVICE_USBMU1 "\\Device\\Mass1PartitionFile\\Storage"
#define MOUNT_USBMU2 "UsbMu2:"
#define DEVICE_USBMU2 "\\Device\\Mass2PartitionFile\\Storage"

PLDR_DATA_TABLE_ENTRY pDataTable = nullptr;
HINSTANCE g_hModule = nullptr;
char pluginPath[MAX_PATH];
char pluginDir[MAX_PATH];
char logPath[MAX_PATH];
// Un-normalized NT device path to the ini, e.g. \Device\Mass0\WirelessInput360.ini
char rawIniPath[MAX_PATH] = "";
char ip[64] = "";
int port = 3000;
bool gotIp = false;

Detour XamInputGetStateDetour;
Detour XamInputGetCapabilitiesDetour;
Detour XamInactivityDetectRecentActivityDetour;
Detour XamInputSetStateDetour;

// Global Socket for the hook to access
SOCKET g_ServerSocket = INVALID_SOCKET;

// Cache to prevent sending duplicate packets (spam reduction)
XINPUT_VIBRATION g_LastVibration[4] = { {0,0}, {0,0}, {0,0}, {0,0} };

uint16_t swap_endianness_16(uint16_t val) {
	return (val >> 8) | (val << 8);
}

BOOL IsTrayOpen() {
	BYTE Input[0x10] = { 0 }, Output[0x10] = { 0 };
	Input[0] = 0xA;
	HalSendSMCMessage(Input, Output);
	return (Output[1] == 0x60);
}

enum ControllerStatus {
	INACTIVE,
	ACTIVE,
};

#pragma pack(push, 1)
struct Report {
	uint8_t reportId;
};

struct ButtonsReport : Report {
	uint8_t x;
	uint8_t y;
	uint8_t z;
	uint8_t rz;
	uint8_t triangle : 1;
	uint8_t circle : 1;
	uint8_t cross : 1;
	uint8_t square : 1;
	uint8_t hat_switch : 4;
	uint8_t r3 : 1;
	uint8_t l3 : 1;
	uint8_t options : 1;
	uint8_t share : 1;
	uint8_t r2 : 1;
	uint8_t l2 : 1;
	uint8_t r1 : 1;
	uint8_t l1 : 1;
	uint8_t : 6;
	uint8_t touchpad : 1;
	uint8_t ps : 1;
	uint8_t rx;
	uint8_t ry;
	uint8_t vendor_defined;
};
#pragma pack(pop)

typedef struct _XINPUT_VIBRATIONEX
{
	WORD                                wLeftMotorSpeed;
	WORD                                wRightMotorSpeed;
} XINPUT_VIBRATION_EX, *PXINPUT_VIBRATION_EX;

typedef struct _XINPUT_CAPABILITIESEX
{
	BYTE                                Type;
	BYTE                                SubType;
	WORD                                Flags;
	XINPUT_GAMEPAD                      Gamepad;
	XINPUT_VIBRATION                    Vibration;
	DWORD unk1;
	DWORD unk2;
	DWORD unk3;
} XINPUT_CAPABILITIES_EX, *PXINPUT_CAPABILITIES_EX;

typedef int(*xam_user_bind_device_callback_func_t)(unsigned int controllerId, unsigned int context, unsigned __int8 category, bool disconnect, unsigned __int8* userIndex);
typedef void(*mm_free_physical_memory_func_t)(DWORD type, DWORD address);

xam_user_bind_device_callback_func_t XamUserBindDeviceCallback = nullptr;
mm_free_physical_memory_func_t MmFreePhysicalMemory = nullptr;

DWORD* XampInputRoutedToSysapp = nullptr;

struct Controller {
	ButtonsReport currentState;
	uint8_t userIndex;
	uint32_t packetNumber;
	ControllerStatus ControllerStatus;

	Controller()
		: ControllerStatus(INACTIVE)
	{
	}
} __declspec(align(4));

volatile int g_ClientThreadRunning = 1;
bool g_HooksInstalled = false;

Controller connectedControllers[4];

void NormalizePath(char* pluginPath);

HANDLE MakeThread(LPTHREAD_START_ROUTINE address, PVOID arg) {
	HANDLE handle = 0;
	ExCreateThread(
		&handle,
		0x10000,   // 64 KB de pila. Con 0 el hilo recibe la minima, y
		           // ReadConfig usa 512 bytes de locales mas el stdio
		0,
		XapiThreadStartup,
		address,
		arg,
		EX_CREATE_FLAG_SUSPENDED | EX_CREATE_FLAG_SYSTEM | 0x18000424
	);

	if (handle) {
		XSetThreadProcessor(handle, 4);
		SetThreadPriority(handle, THREAD_PRIORITY_NORMAL);
		ResumeThread(handle);
	}

	return handle;
}

void InitializePluginPaths(HINSTANCE hModule) {
	strcpy(pluginDir, "Usb:\\");
	strcpy(logPath, "Usb:\\WirelessInput360.log");
	strcpy(pluginPath, "Usb:\\WirelessInput360.ini");

	LDR_DATA_TABLE_ENTRY* moduleDataTable = reinterpret_cast<LDR_DATA_TABLE_ENTRY*>(hModule);

	if (!moduleDataTable || !moduleDataTable->FullDllName.Buffer) {
		return;
	}

	WideCharToMultiByte(CP_ACP, 0, moduleDataTable->FullDllName.Buffer, -1, pluginPath, MAX_PATH, nullptr, nullptr);

	char* lastSlash = strrchr(pluginPath, '\\');
	if (lastSlash) {
		*(lastSlash + 1) = '\0';
	}

	strcpy(rawIniPath, pluginPath);
	strcat(rawIniPath, "WirelessInput360.ini");

	NormalizePath(pluginPath);

	strcpy(pluginDir, pluginPath);
	strcpy(logPath, pluginDir);
	strcat(logPath, "WirelessInput360.log");

	strcat(pluginPath, "WirelessInput360.ini");
}

// UDP log channel.
//
// Why UDP: it is connectionless, so it needs no successful connect() and no
// server to accept anything. It therefore still reports even when the TCP
// connection to the controller server is exactly what is failing.
//
// Why broadcast: it does not depend on having read the ini, so a config failure
// still gets reported. Any machine on the link can listen.
//
// Why not the alternatives: file logging has never worked from this plugin, and
// every XAM UI call (XNotifyQueueUI, XShowMessageBoxUI) kills Aurora.
#define WI360_LOG_PORT 3001

SOCKET g_LogSocket = INVALID_SOCKET;

void InitNetLog() {
	if (g_LogSocket != INVALID_SOCKET) {
		return;
	}

	g_LogSocket = NetDll_socket(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP), AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (g_LogSocket == INVALID_SOCKET) {
		return;
	}

	// Same option the TCP path already sets: tells the Xbox network stack this
	// peer is not a secure Xbox Live endpoint, otherwise traffic is dropped.
	BOOL opt_true = TRUE;
	NetDll_setsockopt(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP), g_LogSocket, SOL_SOCKET, 0x5801, (PCSTR)&opt_true, sizeof(BOOL));

	BOOL bcast = TRUE;
	NetDll_setsockopt(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP), g_LogSocket, SOL_SOCKET, SO_BROADCAST, (PCSTR)&bcast, sizeof(BOOL));
}

void NetLog(const char* message) {
	if (g_LogSocket == INVALID_SOCKET || !message) {
		return;
	}

	SOCKADDR_IN dest;
	memset(&dest, 0, sizeof(dest));
	dest.sin_family = AF_INET;
	dest.sin_port = htons(WI360_LOG_PORT);
	dest.sin_addr.s_addr = INADDR_BROADCAST;

	NetDll_sendto(
		static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP),
		g_LogSocket,
		message,
		(int)strlen(message),
		0,
		(SOCKADDR*)&dest,
		sizeof(dest)
	);
}

void WriteLogLine(const char* path, const char* message) {
	if (!path || path[0] == '\0') {
		return;
	}

	FILE* logFile = fopen(path, "a");
	if (!logFile) {
		return;
	}

	fputs(message, logFile);
	fclose(logFile);
}

void LogMessage(const char* format, ...) {
	char message[512];

	va_list args;
	va_start(args, format);
	vsnprintf(message, sizeof(message), format, args);
	va_end(args);

	DbgPrint("%s", message);

	NetLog(message);

	WriteLogLine(logPath, message);
	WriteLogLine("Usb:\\WirelessInput360.log", message);
	WriteLogLine("Usb0:\\WirelessInput360.log", message);
	WriteLogLine("Hdd:\\WirelessInput360.log", message);
}

void RemoveDevice(int controllerIndex) {
	if (controllerIndex < 0 || controllerIndex >= 4 || !XamUserBindDeviceCallback) {
		return;
	}

	connectedControllers[controllerIndex].ControllerStatus = INACTIVE;
	XamUserBindDeviceCallback(0xa7553952 + controllerIndex, 0x0000000010000005 + controllerIndex, 0, true, 0);
}

void AddDevice(int controllerIndex) {
	if (controllerIndex < 0 || controllerIndex >= 4 || !XamUserBindDeviceCallback) {
		return;
	}

	Controller c = Controller();
	c.packetNumber = 0;

	uint8_t userIndex = -1;
	XamUserBindDeviceCallback(0xa7553952 + controllerIndex, 0x0000000010000005 + controllerIndex, 0, false, &userIndex);
	if (userIndex >= 4) {
		return;
	}

	c.ControllerStatus = ACTIVE;
	c.userIndex = userIndex;

	connectedControllers[controllerIndex] = c;
}

int16_t ConvertToFullRange(uint8_t input, bool invert_y = false) {
	if (!invert_y)
		return static_cast<int16_t>((input - 128) * 256);
	else
		return static_cast<int16_t>((~(input)-128) * 256);
}

DWORD XamInputGetStateHook(DWORD user, DWORD flags, XINPUT_STATE* input_state) {
	DWORD status = XamInputGetStateDetour.GetOriginal<decltype(&XamInputGetStateHook)>()(user, flags, input_state);

	if ((user & 0xFF) == 0xFF)
		user = 0;

	if (!input_state)
		return status;

	static DWORD lastPressTime = 0;
	static const DWORD cooldownDuration = 1000;

	if (status == ERROR_DEVICE_NOT_CONNECTED) {
		ButtonsReport b;
		Controller* c = nullptr;
		for (int i = 0; i < (sizeof(connectedControllers) / sizeof(Controller)); i++) {
			if (connectedControllers[i].ControllerStatus == ACTIVE) {
				if (connectedControllers[i].userIndex == user) {
					c = &connectedControllers[i];
					b = connectedControllers[i].currentState;
					break;
				}
			}
		}

		if (!c)
			return status;

		memset(input_state, 0, sizeof(XINPUT_STATE));

		// HUD is open.
		if (XampInputRoutedToSysapp && c->userIndex < 4 && XampInputRoutedToSysapp[c->userIndex]) {
			// 0x1 is used for titles, 0x0 is used by some offhosts and debug input.
			if ((flags == 0x1) || (flags == 0x0)) {
				return ERROR_SUCCESS;
			}
		}

		if (b.ps) {
			DWORD now = GetTickCount();
			if (now - lastPressTime >= cooldownDuration) {
				lastPressTime = now;
				XamInputSendXenonButtonPress(user);
			}
		}

		if (b.cross)
			input_state->Gamepad.wButtons |= XINPUT_GAMEPAD_A;

		if (b.circle)
			input_state->Gamepad.wButtons |= XINPUT_GAMEPAD_B;

		if (b.triangle)
			input_state->Gamepad.wButtons |= XINPUT_GAMEPAD_Y;

		if (b.square)
			input_state->Gamepad.wButtons |= XINPUT_GAMEPAD_X;

		if (b.options)
			input_state->Gamepad.wButtons |= XINPUT_GAMEPAD_START;

		if (b.share)
			input_state->Gamepad.wButtons |= XINPUT_GAMEPAD_BACK;

		if (b.r3)
			input_state->Gamepad.wButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;

		if (b.l3)
			input_state->Gamepad.wButtons |= XINPUT_GAMEPAD_LEFT_THUMB;

		if (b.r1)
			input_state->Gamepad.wButtons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;

		if (b.l1)
			input_state->Gamepad.wButtons |= XINPUT_GAMEPAD_LEFT_SHOULDER;

		if (b.hat_switch == 0)
			input_state->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP;

		if (b.hat_switch == 2)
			input_state->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;

		if (b.hat_switch == 4)
			input_state->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;

		if (b.hat_switch == 6)
			input_state->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;

		input_state->Gamepad.sThumbRX = ConvertToFullRange(b.z);
		input_state->Gamepad.sThumbRY = ConvertToFullRange(b.rz, true);

		input_state->Gamepad.sThumbLX = ConvertToFullRange(b.x);
		input_state->Gamepad.sThumbLY = ConvertToFullRange(b.y, true);

		input_state->Gamepad.bRightTrigger = b.ry;
		input_state->Gamepad.bLeftTrigger = b.rx;
		input_state->dwPacketNumber = ++c->packetNumber;

		return ERROR_SUCCESS;
	}
	return status;
}

DWORD XamInputGetCapabilitiesExHook(DWORD unk, DWORD user, DWORD flags, XINPUT_CAPABILITIES_EX* capabilities) {
	DWORD status = XamInputGetCapabilitiesDetour.GetOriginal<decltype(&XamInputGetCapabilitiesExHook)>()(unk, user, flags, capabilities);

	if ((user & 0xFF) == 0xFF)
		user = 0;

	if (!capabilities)
		return status;

	if (status == ERROR_DEVICE_NOT_CONNECTED) {
		Controller* c = nullptr;
		for (int i = 0; i < (sizeof(connectedControllers) / sizeof(Controller)); i++) {
			if (connectedControllers[i].ControllerStatus == ACTIVE) {
				if (connectedControllers[i].userIndex == user) {
					c = &connectedControllers[i];
					break;
				}
			}
		}

		if (!c)
			return status;

		capabilities->Type = XINPUT_DEVTYPE_GAMEPAD;
		capabilities->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
		capabilities->Flags = 0;

		XINPUT_STATE state;
		memset(&state, 0, sizeof(XINPUT_STATE));
		XamInputGetStateHook(user, 0, &state);
		capabilities->Gamepad = state.Gamepad;
		capabilities->Vibration.wLeftMotorSpeed = 0;
		capabilities->Vibration.wRightMotorSpeed = 0;
		return ERROR_SUCCESS;
	}

	return status;
}

void SendVibrationUpdate(int userIndex, unsigned short left, unsigned short right) {
	if (g_ServerSocket == INVALID_SOCKET) return;
	if (userIndex < 0 || userIndex >= 4) return;

	// OPTIMIZATION: Only send if the values changed
	if (g_LastVibration[userIndex].wLeftMotorSpeed == left &&
		g_LastVibration[userIndex].wRightMotorSpeed == right) {
		return;
	}

	// Update the cache
	g_LastVibration[userIndex].wLeftMotorSpeed = left;
	g_LastVibration[userIndex].wRightMotorSpeed = right;

	// 1. Format the message: "V:Index:Left:Right"
	char msgBuf[64];
	sprintf(msgBuf, "V:%d:%d:%d", userIndex, left, right);
	std::string msg = msgBuf;

	// 2. Build the WebSocket Frame (Masked)
	std::vector<uint8_t> frame;
	frame.push_back(0x81); // Byte 0: FIN + Text Opcode

	// Byte 1: Mask Bit (0x80) + Payload Length
	// (Assuming payload is short, < 126 bytes, which it is for this string)
	frame.push_back(0x80 | (uint8_t)msg.length());

	// Bytes 2-5: Generate Random Mask Key
	uint8_t mask[4];
	for (int i = 0; i < 4; i++) mask[i] = (uint8_t)(rand() % 0xFF);

	frame.push_back(mask[0]);
	frame.push_back(mask[1]);
	frame.push_back(mask[2]);
	frame.push_back(mask[3]);

	// Bytes 6+: Payload (XOR Encrypted with Mask)
	for (size_t i = 0; i < msg.length(); ++i) {
		frame.push_back(msg[i] ^ mask[i % 4]);
	}

	// 3. Send using NetDll
	NetDll_send(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP),
		g_ServerSocket,
		(const char*)frame.data(),
		frame.size(),
		0);
}

DWORD XamInputSetStateHook(DWORD user, DWORD flags, PXINPUT_VIBRATION pVibration, BYTE bAmplitude, BYTE bFrequency, BYTE bOffset) {
    // Call the original first so the actual controller vibrates locally
    DWORD status = XamInputSetStateDetour.GetOriginal<decltype(&XamInputSetStateHook)>()(user, flags, pVibration, bAmplitude, bFrequency, bOffset);

	if ((user & 0xFF) == 0xFF)
		user = 0;

	if (status == ERROR_DEVICE_NOT_CONNECTED) {
		ButtonsReport b;
		Controller* c = nullptr;
		for (int i = 0; i < (sizeof(connectedControllers) / sizeof(Controller)); i++) {
			if (connectedControllers[i].ControllerStatus == ACTIVE) {
				if (connectedControllers[i].userIndex == user) {
					c = &connectedControllers[i];
					b = connectedControllers[i].currentState;
					break;
				}
			}
		}

		if (!c)
			return status;

		// Send to server
		if (pVibration != nullptr) {
			// userIndex might have flags (like 0xFF), mask them out if necessary, 
			// though usually SetState receives a clean index (0-3).
			int cleanIndex = user & 0xFF;

			if (cleanIndex < 4) {
				SendVibrationUpdate(cleanIndex, pVibration->wLeftMotorSpeed, pVibration->wRightMotorSpeed);
			}
		}
		else {
			// If pVibration is null, it usually implies stop (0,0)
			int cleanIndex = user & 0xFF;
			if (cleanIndex < 4) {
				SendVibrationUpdate(cleanIndex, 0, 0);
			}
		}

		return ERROR_SUCCESS;
	}

    return status;
}

// fix for inactivity (screen dimming)
int XamInactivityDetectRecentActivityHook(DWORD r3) {
	// check if a controller is connected
	for (int i = 0; i < 4; i++) {
		if (connectedControllers[i].ControllerStatus == ACTIVE) {
			// return active
			return 1;
		}
	}
	return XamInactivityDetectRecentActivityDetour.GetOriginal<decltype(&XamInactivityDetectRecentActivityHook)>()(r3);
}
// Native NT file read.
//
// Win32 CreateFileA resolves paths through the \??\ DOS device namespace, so it
// cannot open a raw NT object path like \Device\Mass0\file. The UDP probes proved
// that: every Win32 candidate failed, including the raw device path.
//
// NtOpenFile takes an OBJECT_ATTRIBUTES with a native name, so it addresses the
// device object directly and needs no mount alias to exist in this process.
#ifndef OBJ_CASE_INSENSITIVE
#define OBJ_CASE_INSENSITIVE 0x00000040
#endif
#ifndef FILE_SYNCHRONOUS_IO_NONALERT
#define FILE_SYNCHRONOUS_IO_NONALERT 0x00000020
#endif
#ifndef FILE_NON_DIRECTORY_FILE
#define FILE_NON_DIRECTORY_FILE 0x00000040
#endif

DWORD ReadWholeFileNt(const char* ntPath, char* out, DWORD outSize) {
	if (!ntPath || ntPath[0] == '\0' || !out || outSize == 0) {
		return 0;
	}

	OBJECT_STRING name;
	name.Buffer = (PCHAR)ntPath;
	name.Length = (USHORT)strlen(ntPath);
	name.MaximumLength = (USHORT)(name.Length + 1);

	OBJECT_ATTRIBUTES oa;
	InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE, NULL);

	HANDLE h = NULL;
	IO_STATUS_BLOCK iosb;
	memset(&iosb, 0, sizeof(iosb));

	NTSTATUS st = NtOpenFile(
		&h,
		GENERIC_READ | SYNCHRONIZE,
		&oa,
		&iosb,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE
	);

	if (st < 0 || h == NULL) {
		return 0;
	}

	LARGE_INTEGER offset;
	offset.QuadPart = 0;
	memset(&iosb, 0, sizeof(iosb));

	st = NtReadFile(h, NULL, NULL, NULL, &iosb, out, outSize - 1, &offset);

	NtClose(h);

	if (st < 0) {
		return 0;
	}

	DWORD read = (DWORD)iosb.Information;
	if (read >= outSize) {
		read = outSize - 1;
	}
	out[read] = '\0';
	return read;
}


// Reads a whole small file with raw Win32.
//
// CRT fopen() fails on this console for every mount alias the plugin knows about
// (Usb:, Usb0:, Hdd:) -- proven by the UDP probes: "about to fopen Usb0:\..."
// followed by "Failed to open config file". Those aliases are created per process
// by whoever needs them (Aurora mounts its own), and the dashboard process this
// plugin lives in does not necessarily have them.
//
// The raw NT device path from FullDllName needs no alias at all, so it is tried
// first. CreateFileA also lets us pass an explicit share mode.
//
// Returns bytes read, or 0 on failure. Always null terminates.
DWORD ReadWholeFile(const char* path, char* out, DWORD outSize) {
	if (!path || path[0] == '\0' || !out || outSize == 0) {
		return 0;
	}

	HANDLE h = CreateFileA(
		path,
		GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr
	);

	if (h == INVALID_HANDLE_VALUE) {
		return 0;
	}

	DWORD read = 0;
	BOOL ok = ReadFile(h, out, outSize - 1, &read, nullptr);
	CloseHandle(h);

	if (!ok) {
		return 0;
	}

	out[read] = '\0';
	return read;
}

bool ReadConfig()
{
	if (gotIp) return true;

	// Candidates, most likely to work first. rawIniPath is the un-normalized NT
	// device path (e.g. \Device\Mass0\WirelessInput360.ini) and needs no mount.
	const char* candidates[5] = {
		rawIniPath,
		pluginPath,
		"Usb:\\WirelessInput360.ini",
		"Usb0:\\WirelessInput360.ini",
		"Hdd:\\WirelessInput360.ini"
	};

	static char contents[2048];
	DWORD len = 0;
	const char* usedPath = nullptr;

	for (int i = 0; i < 5; i++) {
		if (!candidates[i] || candidates[i][0] == '\0') {
			continue;
		}

		LogMessage("[WirelessInput360] probe: trying %s\n", candidates[i]);

			// Native first: it is the only call that can open a raw \Device\ path.
			len = ReadWholeFileNt(candidates[i], contents, sizeof(contents));
			if (len == 0) {
				len = ReadWholeFile(candidates[i], contents, sizeof(contents));
			}
		if (len > 0) {
			usedPath = candidates[i];
			break;
		}
	}

	if (!usedPath)
	{
		LogMessage("[WirelessInput360] Failed to open config from any path\n");
		return false;
	}

	LogMessage("[WirelessInput360] Config read from %s (%d bytes)\n", usedPath, (int)len);

	// Parse in memory, one line at a time.
	char* cursor = contents;
	while (*cursor)
	{
		char* line = cursor;

		char* eol = strpbrk(cursor, "\r\n");
		if (eol) {
			*eol = '\0';
			cursor = eol + 1;
			while (*cursor == '\r' || *cursor == '\n') cursor++;
		}
		else {
			cursor += strlen(cursor);
		}

		while (*line == ' ' || *line == '\t')
			line++;

		if (strlen(line) == 0 || line[0] == '#' || line[0] == ';' || line[0] == '[')
			continue;

		static char lower[256];
		strncpy(lower, line, sizeof(lower) - 1);
		lower[sizeof(lower) - 1] = '\0';
		for (char* p = lower; *p; ++p)
			*p = (char)tolower(*p);

		if (strncmp(lower, "ip=", 3) == 0)
		{
			strncpy(ip, line + 3, sizeof(ip) - 1);
			ip[sizeof(ip) - 1] = '\0';
		}
		else if (strncmp(lower, "port=", 5) == 0)
		{
			port = atoi(line + 5);
		}
	}

	gotIp = true;
	LogMessage("[WirelessInput360] Loaded config. ip=%s port=%d\n", ip, port);

	if (ip[0] == '\0') {
		LogMessage("[WirelessInput360] Config is missing ip=\n");
		return false;
	}

	return TRUE;
}

bool IsHexChar(char value) {
	return (value >= '0' && value <= '9') ||
		(value >= 'a' && value <= 'f') ||
		(value >= 'A' && value <= 'F');
}

DWORD WINAPI StartWSConnection(LPVOID) {
    XNetStartupParams xnsp;
    memset(&xnsp, 0, sizeof(xnsp));
    xnsp.cfgSizeOfStruct = sizeof(XNetStartupParams);
    xnsp.cfgFlags = XNET_STARTUP_BYPASS_SECURITY;

    int xnetResult = NetDll_XNetStartupEx(
        static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP),
        &xnsp,
        WIRELESSINPUT360_NETDLL_VERSION
    );

    if (xnetResult != 0) {
        LogMessage("[WirelessInput360] CRITICAL: NetDll_XNetStartupEx failed: %d\n", xnetResult);
        return 0;
    }

    WSADATA wsaData;
    int wsaResult = NetDll_WSAStartupEx(
        static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP),
        2,
        &wsaData,
        WIRELESSINPUT360_NETDLL_VERSION
    );
    
    if (wsaResult != 0) {
        LogMessage("[WirelessInput360] CRITICAL: NetDll_WSAStartupEx failed: %d\n", wsaResult);
        NetDll_XNetCleanup(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP));
        return 0; // Cannot run without network stack
    }

    InitNetLog();

    DWORD linkStatus = NetDll_XNetGetEthernetLinkStatus(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP));
    LogMessage("[WirelessInput360] XNet initialized. Ethernet link status: 0x%08X\n", linkStatus);

    LogMessage("[WirelessInput360] probe: entering ReadConfig\n");

    // 2. Check config once (or move inside if config changes dynamically)
    if (!ReadConfig()) {
        NetDll_XNetCleanup(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP));
        NetDll_WSACleanup(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP));
        return 0;
    }

    // Outer loop for Reconnection Logic
    while (g_ClientThreadRunning) {
        
        // Create Socket
        SOCKET sock = NetDll_socket(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP), AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) {
            LogMessage("[WirelessInput360] socket creation failed: %d\n", NetDll_WSAGetLastError());
            // Do NOT Cleanup WSA here, just sleep and retry the socket creation
            Sleep(3000);
            continue;
        }

        BOOL opt_true = TRUE;
        NetDll_setsockopt(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP), sock, SOL_SOCKET, 0x5801, (PCSTR)&opt_true, sizeof(BOOL));

        // --- Connect to server ---
        SOCKADDR_IN target;
        target.sin_family = AF_INET;
        target.sin_port = htons(port);
        target.sin_addr.s_addr = inet_addr(ip);

        LogMessage("[WirelessInput360] Attempting connect to %s:%d\n", ip, port);

        if (NetDll_connect(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP), sock, (SOCKADDR*)&target, sizeof(target)) == SOCKET_ERROR) {
            LogMessage(
                "[WirelessInput360] connect failed to %s:%d. wsa=%d. Retrying in 3s...\n",
                ip,
                port,
                NetDll_WSAGetLastError()
            );
            NetDll_closesocket(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP), sock);
            Sleep(3000);
            continue;
        }
        LogMessage("[WirelessInput360] Connected to server\n");

        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

        char wsHandshake[512];
        sprintf(
            wsHandshake,
            "GET / HTTP/1.1\r\n"
            "Host: %s:%d\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n",
            ip, port
        );

        if (NetDll_send(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP), sock, wsHandshake, strlen(wsHandshake), 0) == SOCKET_ERROR) {
            LogMessage("[WirelessInput360] Handshake send failed. wsa=%d\n", NetDll_WSAGetLastError());
            NetDll_closesocket(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP), sock);
            Sleep(3000);
            continue;
        }

        const int BUFSIZE = 4096;
        char buf[BUFSIZE];
        int bytes = NetDll_recv(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP), sock, buf, BUFSIZE - 1, 0);

        if (bytes <= 0) {
            LogMessage("[WirelessInput360] Handshake recv failed / server closed. wsa=%d\n", NetDll_WSAGetLastError());
            NetDll_closesocket(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP), sock);
            Sleep(3000);
            continue;
        }
        // ... Handshake processing ...
        LogMessage("[WirelessInput360] Handshake response received\n");

		g_ServerSocket = sock;

        // --- WebSocket main loop ---
        bool connectionActive = true;
        
        while (connectionActive && g_ClientThreadRunning) { // Add global check here too
            bytes = NetDll_recv(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP), sock, buf, BUFSIZE - 1, 0);
            if (bytes > 0) {
				// simple text frame decoding
				unsigned char* data = (unsigned char*)buf;
				int opcode = data[0] & 0x0F;
				int masked = data[1] & 0x80;
				int payloadLen = data[1] & 0x7F;
				int offset = 2;

				if (payloadLen == 126 && bytes >= 4) {
					payloadLen = (data[2] << 8) | data[3];
					offset += 2;
				}
				else if (payloadLen == 127 && bytes >= 10) {
					payloadLen = 0;
					for (int i = 0; i < 8; ++i)
						payloadLen = (payloadLen << 8) | data[offset++];
				}

				if (masked) {
					if ((offset + 4) > bytes) {
						continue;
					}

					unsigned char mask[4];
					memcpy(mask, data + offset, 4);
					offset += 4;

					if ((offset + payloadLen) > bytes) {
						continue;
					}

					for (int i = 0; i < payloadLen; ++i)
						data[offset + i] ^= mask[i % 4];
				}
				else if ((offset + payloadLen) > bytes) {
					continue;
				}

				// Print text frames only
				if (opcode == 1) {
					char text[256];
					if (payloadLen > 255) payloadLen = 255;
					memcpy(text, data + offset, payloadLen);
					text[payloadLen] = '\0';

					if (text[0] != '\0') {
						if (payloadLen < 2 || (text[0] != '0' && text[0] != '1') || text[1] < '0' || text[1] > '3') {
							continue;
						}

						int pStatus = text[0] - '0'; // State
						int pNum = text[1] - '0';

						if (pNum < 0 || pNum >= 4) {
							continue;
						}

						if (pStatus == 1) {
							if (connectedControllers[pNum].ControllerStatus == ACTIVE) {
								RemoveDevice(pNum);
							}
							continue;
						}

						if (connectedControllers[pNum].ControllerStatus == INACTIVE) {
							AddDevice(pNum);
						}

						uint8_t raw[128];
						int raw_len = 0;

						for (int i = 0; text[i] && text[i + 1]; i += 2) {
							if (!IsHexChar(text[i]) || !IsHexChar(text[i + 1])) {
								raw_len = 0;
								break;
							}

							char buf[3] = { text[i], text[i + 1], 0 }; // take 2 chars + null
							raw[raw_len++] = (uint8_t)strtol(buf, NULL, 16);
						}

						if (raw_len < 10) {
							continue;
						}

						ButtonsReport buttonReport;
						memset(&buttonReport, 0, sizeof(buttonReport));
						int copyLen = raw_len;
						if (copyLen > (int)sizeof(buttonReport)) {
							copyLen = sizeof(buttonReport);
						}
						memcpy(&buttonReport, raw, copyLen);

						connectedControllers[pNum].currentState = buttonReport;
					}
				}
				else if (opcode == 8) {
					connectionActive = false;
				}
			}
			else {
				// Connection lost or error
				connectionActive = false;
			}
		}

		g_ServerSocket = INVALID_SOCKET;

		// Close the socket, but keep WSA loaded for the next attempt
		NetDll_closesocket(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP), sock);
		LogMessage("[WirelessInput360] Connection lost. Retrying in 3s...\n");
		Sleep(3000);
	}
	NetDll_XNetCleanup(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP));
	NetDll_WSACleanup(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP));
	return 0;
}

void* XamInputGetState = nullptr;
void* XamInputGetCapabilitiesEx = nullptr;
void* XamInputSetState = nullptr;
bool isDevkit = true;

bool initFunctionPointers() {
	isDevkit = *(uint32_t*)(0x8010D334) == 0x00000000;
	HANDLE kernelHandle = GetModuleHandleA("xboxkrnl.exe");

	if (!kernelHandle) {
		LogMessage("[WirelessInput360] COULDNT GET KERNEL HANDLE!\n");
		return false;
	}

	HANDLE xamHandle = GetModuleHandleA("xam.xex");
	if (!xamHandle) {
		LogMessage("[WirelessInput360] COULDNT GET XAM HANDLE!\n");
		return false;
	}

	XexGetProcedureAddress(kernelHandle, 189, &MmFreePhysicalMemory);

	XexGetProcedureAddress(xamHandle, 685, &XamInputGetCapabilitiesEx);
	XexGetProcedureAddress(xamHandle, 401, &XamInputGetState);
	XexGetProcedureAddress(xamHandle, 402, &XamInputSetState);
	if (!XamInputGetCapabilitiesEx || !XamInputGetState || !XamInputSetState) {
		LogMessage("[WirelessInput360] Failed to resolve XInput exports\n");
		return false;
	}

	if (isDevkit) {
		LogMessage("[WirelessInput360] Running in devkit mode\n");
		XamUserBindDeviceCallback = (xam_user_bind_device_callback_func_t)0x817A34B8; // 7C 8B 23 78 7C A4 2B 78 54 CA 06 3F
		XampInputRoutedToSysapp = (DWORD*)0x81D4F650;
	}
	else {
		LogMessage("[WirelessInput360] Running in retail mode\n");
		XamUserBindDeviceCallback = (xam_user_bind_device_callback_func_t)0x816D9060; // 7C 8B 23 78 7C A4 2B 78 54 CA 06 3F
		XampInputRoutedToSysapp = (DWORD*)0x81AAC2A0;
	}
	return true;
}

void NormalizePath(char* pluginPath)
{
	// 1. Define struct locally
	typedef struct {
		const char* mount;
		const char* device;
	} MountMapping;

	// 2. Define data locally as STATIC (initialized only once)
	// Put the longest paths (like DEVKIT or Games) ABOVE generic Hdd/Mass paths
	static const MountMapping driveMappings[] = {
		{ MOUNT_DEVKIT,    DEVICE_DEVKIT },
		{ MOUNT_HDDGAMES,  DEVICE_HDDGAMES },
		{ MOUNT_HDDAPPS,   DEVICE_HDDAPPS },
		{ MOUNT_USBMU0,    DEVICE_USBMU0 },
		{ MOUNT_USBMU1,    DEVICE_USBMU1 },
		{ MOUNT_USBMU2,    DEVICE_USBMU2 },
		{ MOUNT_HDD,       DEVICE_HARDISK0_PART1 },
		{ MOUNT_USB0,      DEVICE_USB0 },
		{ MOUNT_USB1,      DEVICE_USB1 },
		{ MOUNT_USB2,      DEVICE_USB2 },
		{ MOUNT_INTMU,     DEVICE_INTMEM },
		{ MOUNT_DVD,       DEVICE_CDROM0 },
		{ MOUNT_FLASH,     DEVICE_NAND_FLASH },
		{ MOUNT_MMCMU,     DEVICE_MMCMU },
		{ MOUNT_FLASHMU,   DEVICE_FLASHMU },
		{ MOUNT_MU,        DEVICE_MEMORY_UNIT0 },
		{ MOUNT_MU1,       DEVICE_MEMORY_UNIT1 },
		{ NULL, NULL }
	};

	int i;

	// 3. The Loop
	for (i = 0; driveMappings[i].mount != NULL; i++)
	{
		const char* devicePrefix = driveMappings[i].device;
		const char* mountPoint = driveMappings[i].mount;
		size_t devLen = strlen(devicePrefix);

		if (strncmp(pluginPath, devicePrefix, devLen) == 0)
		{
			// Ensure we matched a full directory name
			if (pluginPath[devLen] == '\\' || pluginPath[devLen] == '\0')
			{
				char temp[MAX_PATH];
				const char* remainingPath = pluginPath + devLen;

				// Skip the leading slash if present so we don't get "Hdd:\\folder"
				if (*remainingPath == '\\') {
					remainingPath++;
				}

				// Combine: "Hdd:" + "\" + "MyPath"
				sprintf(temp, "%s\\%s", mountPoint, remainingPath);

				strcpy(pluginPath, temp);
				return; // Stop processing once match is found
			}
		}
	}
}

DWORD WINAPI BootstrapThread(LPVOID)
{
	Sleep(8000);

	InitializePluginPaths(g_hModule);
	LogMessage("[WirelessInput360] Bootstrap entered\n");
	LogMessage("[WirelessInput360] Plugin directory: %s\n", pluginDir);
	LogMessage("[WirelessInput360] Config path: %s\n", pluginPath);

	if ((XboxKrnlVersion->Build != 17559 && XboxKrnlVersion->Build != 17489) || IsTrayOpen()) {
		LogMessage("[WirelessInput360] Only 17559 and 17489 dashboards are currently supported or the disk tray is open. Aborting launch...\n");
		return 0;
	}

	if (!initFunctionPointers())
		return 0;

	if (isDevkit) {
		XamInactivityDetectRecentActivityDetour = Detour((void*)0x81750588, (void*)XamInactivityDetectRecentActivityHook); // 3D 60 81 ?? 3D 40 81 ?? E8 6B ?? ?? E9 6A ?? ?? 7F 23 58 40 40 98 00 0C
	}
	else {
		XamInactivityDetectRecentActivityDetour = Detour((void*)0x81695DE8, (void*)XamInactivityDetectRecentActivityHook); // 3D 60 81 ?? 3D 40 81 ?? E8 6B ?? ?? E9 6A ?? ?? 7F 23 58 40 40 98 00 0C
	}

	XamInputGetStateDetour = Detour(XamInputGetState, (void*)XamInputGetStateHook);
	XamInputGetCapabilitiesDetour = Detour(XamInputGetCapabilitiesEx, (void*)XamInputGetCapabilitiesExHook);
	XamInputSetStateDetour = Detour(XamInputSetState, (void*)XamInputSetStateHook);

	XamInputGetStateDetour.Install();
	XamInputGetCapabilitiesDetour.Install();
	XamInputSetStateDetour.Install();
	XamInactivityDetectRecentActivityDetour.Install();

	g_HooksInstalled = true;
	LogMessage("[WirelessInput360] Hooks installed\n");

	return StartWSConnection(nullptr);
}

BOOL DllMain(HINSTANCE hModule, DWORD reason, void* pReserved)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		g_hModule = hModule;
		MakeThread(BootstrapThread, nullptr);
	}
	else if (reason == DLL_PROCESS_DETACH) {
		if (g_HooksInstalled) {
			XamInputGetStateDetour.Remove();
			XamInputGetCapabilitiesDetour.Remove();
			XamInputSetStateDetour.Remove();
			XamInactivityDetectRecentActivityDetour.Remove();
		}

		port = 3000;
		gotIp = false;
		g_ClientThreadRunning = 0;

		for (int i = 0; i < (sizeof(connectedControllers) / sizeof(Controller)); i++) {
			if (connectedControllers[i].ControllerStatus == ACTIVE) {
				RemoveDevice(i);
			}
		}
	}
	return TRUE;
}
