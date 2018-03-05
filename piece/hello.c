#include <stdio.h>
#include <stdlib.h>

#include <mrubyc.h>

#include <piece.h>

#include "define_piece.h"

unsigned char vbuff[128*88];
static unsigned char draw;

extern const uint8_t test[];

#define MEMORY_SIZE (1024*10)
static uint8_t memory_pool[MEMORY_SIZE];

mrb_vm *vm;

int errno;

int write(int fd, const void *buf, int nbytes)
{
	return 0;
}

void *malloc(size_t size)
{
	return pceHeapAlloc(size);
}

void free(void *p)
{
	pceHeapFree(p);
}

void pceAppInit(void)
{
	pceLCDDispStop();
	pceLCDSetBuffer(vbuff);
	pceAppSetProcPeriod(80);
	memset(vbuff, 0, 128*88);

	pceFontSetPos(0, 0);

	mrbc_init_alloc(memory_pool, MEMORY_SIZE);
	init_static();

	vm = mrbc_vm_open(NULL);
	if(!vm)
	{
		pceFontPutStr("VM open Error\n");
		goto end;
	}
	if(mrbc_load_mrb(vm, test) != 0)
	{
		pceFontPrintf("mrb load Error: %08X\n", vm->error_code);
		goto end;
	}

	define_pceLcd();

	mrbc_vm_begin(vm);
	mrbc_vm_run(vm);

end:
	draw = 1;

	pceLCDDispStart();
}


void pceAppProc(int cnt)
{
	if(draw)
	{
		pceLCDTrans();
		draw = 0;
	}
}


void pceAppExit(void)
{
	mrbc_vm_end(vm);
	mrbc_vm_close(vm);
}

