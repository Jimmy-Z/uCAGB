#include "pl.h"
#include "stdio.h"

#ifdef WINDOWS
tDev open_serial(const char* devname){
	return CreateFile(devname, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

boolean validate_serial(tDev d){
	return d == INVALID_HANDLE_VALUE;
}

void setup_serial(tDev d){
	// https://www.pjrc.com/teensy/serial_read.c
	COMMCONFIG cfg;
	COMMTIMEOUTS timeout;

	DWORD n = sizeof(cfg);
	GetCommConfig(d, &cfg, &n);
	cfg.dcb.BaudRate = 115200;
	cfg.dcb.fBinary = TRUE;
	cfg.dcb.fParity = FALSE;
	cfg.dcb.fOutxCtsFlow = FALSE;
	cfg.dcb.fOutxDsrFlow = FALSE;
	cfg.dcb.fDtrControl = DTR_CONTROL_ENABLE;
	cfg.dcb.fDsrSensitivity = FALSE;
	cfg.dcb.fTXContinueOnXoff = TRUE;
	cfg.dcb.fOutX = FALSE;
	cfg.dcb.fInX = FALSE;
	cfg.dcb.fErrorChar = FALSE;
	cfg.dcb.fNull = FALSE;
	cfg.dcb.fRtsControl = RTS_CONTROL_ENABLE;
	cfg.dcb.fAbortOnError = FALSE;
	cfg.dcb.XonLim = 0x8000;
	cfg.dcb.XoffLim = 20;
	cfg.dcb.ByteSize = 8;
	cfg.dcb.Parity = NOPARITY;
	cfg.dcb.StopBits = ONESTOPBIT;
	SetCommConfig(d, &cfg, n);

	GetCommTimeouts(d, &timeout);
	timeout.ReadIntervalTimeout = 250;
	timeout.ReadTotalTimeoutMultiplier = 1;
	timeout.ReadTotalTimeoutConstant = 500;
	timeout.WriteTotalTimeoutMultiplier = 1;
	timeout.WriteTotalTimeoutConstant = 500;
	SetCommTimeouts(d, &timeout);
}

void last_err(void){
	char *buf;
	DWORD err = GetLastError();
	FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER
		| FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, err, 0, (LPTSTR)&buf, 0, NULL);
	fprintf(stderr, "error code: 0x%08x, message: %s\n", err, buf);
	exit(err);
}

void write_serial(tDev d, const void *data, tSize size){
	tSize written;
	BOOL ret = WriteFile(d, data, size, &written, NULL);
	if(!ret){
		last_err();
	}
}

void read_serial(tDev d, void *data, tSize size){
	tSize read;
	BOOL ret = ReadFile(d, data, size, &read, NULL);
	if(!ret){
		last_err();
	}
}
#endif

