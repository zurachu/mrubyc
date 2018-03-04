#include "hal/hal.h"

#include <piece.h>

int hal_write(int fd, const void *buf, int nbytes)
{
	char out[64];
	memcpy(out, buf, nbytes);
	out[nbytes] = '\0';
	pceFontPutStr(out);
	return 0;
}

int hal_flush(int fd)
{
    return 0;
}
