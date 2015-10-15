#include "pl_win.h"

tDev open_serial(const char* devname){
	return CreateFile(devname, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

boolean validate_serial(tDev d){
	return d != INVALID_HANDLE_VALUE;
}

void write_serial(tDev d, const void *data, tSize size){
	tSize written;
	WriteFile(d, data, size, &written, NULL);
}

void read_serial(tDev d, void *data, tSize size){
	tSize read;
	ReadFile(d, data, size, &read, NULL);
}

