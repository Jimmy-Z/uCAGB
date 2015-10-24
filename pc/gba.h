#include "pl.h"

u32 xfer32(tDev d, u32 data);
void xfer32wo(tDev d, u32 data);
u32 xfer32ro(tDev d);

void xfer32bw(tDev d, const u8* data, tSize size);
void xfer32br(tDev d, u8* data, tSize size);

int gba_ready(tDev d);
int gba_multiboot(tDev d, u8 *rom, tSize size);
