#ifdef WINDOWS
#include <windows.h>
typedef HANDLE tDev;
#endif

typedef unsigned int tSize;
typedef unsigned int uint;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#define get_rtime() GetTickCount()
#define sleep(_x) Sleep((_x))

tDev open_serial(const char* devname);
boolean validate_serial(tDev d);
void setup_serial(tDev d);
void write_serial(tDev d, const void *data, tSize size);
void read_serial(tDev d, void *data, tSize size);
