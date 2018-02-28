/*! @file
  @brief
  mruby bytecode executor.

  <pre>
  Copyright (C) 2015-2018 Kyushu Institute of Technology.
  Copyright (C) 2015-2018 Shimane IT Open-Innovation Center.

  This file is distributed under BSD 3-Clause License.

  Fetch mruby VM bytecodes, decode and execute.

  </pre>
*/

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include "vm.h"
#include "alloc.h"
#include "static.h"
#include "vm_config.h"
#include "opcode.h"
#include "class.h"
#include "symbol.h"
#include "console.h"

#include "c_string.h"
#include "c_range.h"

static uint32_t free_vm_bitmap[MAX_VM_COUNT / 32 + 1];
#define FREE_BITMAP_WIDTH 32
#define Num(n) (sizeof(n)/sizeof((n)[0]))


//================================================================
/*! Number of leading zeros.

  @param	x	target (32bit unsined)
  @retval	int	nlz value
*/
static inline int nlz32(uint32_t x)
{
  int n;

  if( x == 0 ) return 32;

  n = 1;
  if((x >> 16) == 0 ) { n += 16; x <<= 16; }
  if((x >> 24) == 0 ) { n +=  8; x <<=  8; }
  if((x >> 28) == 0 ) { n +=  4; x <<=  4; }
  if((x >> 30) == 0 ) { n +=  2; x <<=  2; }
  return n - (x >> 31);
}


//================================================================
/*!@brief
  find sym[n] from symbol table in irep

  @param  p
  @param  n
  @return  symbol string
*/
static char *find_irep_symbol( uint8_t *p, int n )
{
  int cnt = bin_to_uint32(p);
  if( n >= cnt ) return 0;
  p += 4;
  while( n > 0 ) {
    uint16_t s = bin_to_uint16(p);
    p += 2+s+1;   // size(2 bytes) + symbol len + '\0'
    n--;
  }
  return (char *)p+2;  // skip size(2 bytes)
}


//================================================================
/*!@brief

*/
static void not_supported(void)
{
  console_printf("Not supported!\n");
}


//================================================================
/*!@brief
  Execute OP_NOP

  No operation

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_nop( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  return 0;
}


//================================================================
/*!@brief
  Execute OP_MOVE

  R(A) := R(B)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_move( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int rb = GETARG_B(code);

  mrbc_release(vm, &regs[ra]);
  mrbc_dup(vm, &regs[rb]);
  regs[ra] = regs[rb];

  return 0;
}


//================================================================
/*!@brief
  Execute OP_LOADL

  R(A) := Pool(Bx)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_loadl( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int rb = GETARG_Bx(code);
  mrb_object *pool_obj;

  mrbc_release(vm, &regs[ra]);

  // regs[ra] = vm->pc_irep->pools[rb];

  pool_obj = vm->pc_irep->pools[rb];
  regs[ra] = *pool_obj;

  return 0;
}


//================================================================
/*!@brief
  Execute OP_LOADI

  R(A) := sBx

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_loadi( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);

  mrbc_release(vm, &regs[ra]);
  regs[ra].tt = MRB_TT_FIXNUM;
  regs[ra].u.i = GETARG_sBx(code);

  return 0;
}


//================================================================
/*!@brief
  Execute OP_LOADSYM

  R(A) := Syms(Bx)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_loadsym( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int rb = GETARG_Bx(code);
  char *sym = find_irep_symbol(vm->pc_irep->ptr_to_sym, rb);

  mrb_sym sym_id = add_sym(sym);

  mrbc_release(vm, &regs[ra]);
  regs[ra].tt = MRB_TT_SYMBOL;
  regs[ra].u.i = sym_id;

  return 0;
}


//================================================================
/*!@brief
  Execute OP_LOADNIL

  R(A) := nil

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_loadnil( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);

  mrbc_release(vm, &regs[ra]);
  regs[ra].tt = MRB_TT_NIL;

  return 0;
}


//================================================================
/*!@brief
  Execute OP_LOADSELF

  R(A) := self

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_loadself( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);

  mrbc_release(vm, &regs[ra]);
  mrbc_dup(vm, &regs[0]);       // TODO: Need?
  regs[ra] = regs[0];

  return 0;
}


//================================================================
/*!@brief
  Execute OP_LOADT

  R(A) := true

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_loadt( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);

  mrbc_release(vm, &regs[ra]);
  regs[ra].tt = MRB_TT_TRUE;

  return 0;
}


//================================================================
/*!@brief
  Execute OP_LOADF

  R(A) := false

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_loadf( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);

  mrbc_release(vm, &regs[ra]);
  regs[ra].tt = MRB_TT_FALSE;

  return 0;
}


//================================================================
/*!@brief
  Execute OP_GETGLOBAL

  R(A) := getglobal(Syms(Bx))

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_getglobal( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int rb = GETARG_Bx(code);
  char *sym = find_irep_symbol(vm->pc_irep->ptr_to_sym, rb);
  mrb_sym sym_id = add_sym(sym);

  mrbc_release(vm, &regs[ra]);
  regs[ra] = global_object_get(sym_id);

  return 0;
}


//================================================================
/*!@brief
  Execute OP_SETGLOBAL

  setglobal(Syms(Bx), R(A))

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_setglobal( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int rb = GETARG_Bx(code);
  char *sym = find_irep_symbol(vm->pc_irep->ptr_to_sym, rb);
  mrb_sym sym_id = add_sym(sym);
  global_object_add(sym_id, regs[ra]);

  return 0;
}


//================================================================
/*!@brief
  Execute OP_GETCONST

  R(A) := constget(Syms(Bx))

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_getconst( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int rb = GETARG_Bx(code);
  char *sym = find_irep_symbol(vm->pc_irep->ptr_to_sym, rb);
  mrb_sym sym_id = add_sym(sym);

  mrbc_release(vm, &regs[ra]);
  regs[ra] = const_object_get(sym_id);

  return 0;
}


//================================================================
/*!@brief
  Execute OP_SETCONST

  constset(Syms(Bx),R(A))

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/

inline static int op_setconst( mrb_vm *vm, uint32_t code, mrb_value *regs ) {
  int ra = GETARG_A(code);
  int rb = GETARG_Bx(code);
  char *sym = find_irep_symbol(vm->pc_irep->ptr_to_sym, rb);
  mrb_sym sym_id = add_sym(sym);
  const_object_add(sym_id, &regs[ra]);

  return 0;
}


//================================================================
/*!@brief
  Execute OP_JMP

  pc += sBx

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_jmp( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  vm->pc += GETARG_sBx(code) - 1;
  return 0;
}


//================================================================
/*!@brief
  Execute OP_JMPIF

  if R(A) pc += sBx

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_jmpif( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int tt = regs[GETARG_A(code)].tt;
  if( tt != MRB_TT_FALSE && tt != MRB_TT_NIL ) {
    vm->pc += GETARG_sBx(code) - 1;
  }
  return 0;
}


//================================================================
/*!@brief
  Execute OP_JMPNOT

  if not R(A) pc += sBx

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_jmpnot( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int tt = regs[GETARG_A(code)].tt;
  if( tt == MRB_TT_FALSE || tt == MRB_TT_NIL ) {
    vm->pc += GETARG_sBx(code) - 1;
  }
  return 0;
}


//================================================================
/*!@brief
  Execute OP_SEND / OP_SENDB

  OP_SEND   R(A) := call(R(A),Syms(B),R(A+1),...,R(A+C))
  OP_SENDB  R(A) := call(R(A),Syms(B),R(A+1),...,R(A+C),&R(A+C+1))

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_send( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  mrb_value recv = regs[ra];
  int rb = GETARG_B(code);  // index of method sym
  int rc = GETARG_C(code);  // number of params
  char *sym;
  mrb_sym sym_id;
  mrb_proc *m;
  mrb_callinfo *callinfo;

  // Block param
  int bidx = ra + rc + 1;
  if( GET_OPCODE(code) == OP_SEND ){
    // OP_SEND: set nil
    regs[bidx].tt = MRB_TT_NIL;
  } else {
    // OP_SENDB: set Proc objec
    if( regs[bidx].tt != MRB_TT_NIL && regs[bidx].tt != MRB_TT_PROC ){
      // convert to Proc
    }
  }

  sym = find_irep_symbol(vm->pc_irep->ptr_to_sym, rb);
  sym_id = str_to_symid(sym);
  m = find_method(vm, recv, sym_id);

  if( m == 0 ) {
    console_printf("No method. vtype=%d method='%s'\n", recv.tt, sym);
    return 0;
  }

  // m is C func
  if( m->c_func ) {
    int r;

    m->u.func(vm, regs + ra, rc);

    r = ra + rc;
    while( ra < r ) {
      mrbc_release(vm, &regs[r]);
      r--;
    }
    return 0;
  }

  // m is Ruby method.
  // callinfo
  callinfo = vm->callinfo + vm->callinfo_top;
  callinfo->reg_top = vm->reg_top;
  callinfo->pc_irep = vm->pc_irep;
  callinfo->pc = vm->pc;
  callinfo->n_args = rc;
  callinfo->target_class = vm->target_class;
  vm->callinfo_top++;

  // target irep
  vm->pc = 0;
  vm->pc_irep = m->u.irep;

  // new regs
  vm->reg_top += ra;

  return 0;
}


//================================================================
/*!@brief
  Execute OP_ENTER

  arg setup according to flags (23=5:5:1:5:5:1:1)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_enter( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  mrb_callinfo *callinfo = vm->callinfo + vm->callinfo_top - 1;
  uint32_t enter_param = GETARG_Ax(code);
  int def_args = (enter_param >> 13) & 0x1f;
  int args = (enter_param >> 18) & 0x1f;
  if( def_args > 0 ){
    vm->pc += callinfo->n_args - args;
  }
  return 0;
}


//================================================================
/*!@brief
  Execute OP_RETURN

  return R(A) (B=normal,in-block return/break)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_return( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  mrb_callinfo *callinfo;
  int reg_top;

  // return value
  int ra = GETARG_A(code);
  if( ra != 0 ){
    mrb_value v = regs[ra];
    mrbc_dup(vm, &v);
    regs[0] = v;
  }
  // restore irep,pc,regs
  vm->callinfo_top--;
  callinfo = vm->callinfo + vm->callinfo_top;
  reg_top = vm->reg_top;
  vm->reg_top = callinfo->reg_top;
  // clear regs and restore vm->reg_top
  while( reg_top > callinfo->reg_top ){
    mrbc_release(vm, &regs[reg_top]);
    regs[reg_top].tt = MRB_TT_EMPTY;
    reg_top--;
  }
  // restore others
  vm->pc_irep = callinfo->pc_irep;
  vm->pc = callinfo->pc;
  vm->target_class = callinfo->target_class;
  return 0;
}


//================================================================
/*!@brief
  Execute OP_BLKPUSH

  R(A) := block (16=6:1:5:4)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_blkpush( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);

  mrbc_release(vm, &regs[ra]);
  mrbc_dup(vm, &regs[ra-1] );
  regs[ra] = regs[ra-1];

  return 0;
}



//================================================================
/*!@brief
  Execute OP_ADD

  R(A) := R(A)+R(A+1) (Syms[B]=:+,C=1)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_add( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);

  if( regs[ra].tt == MRB_TT_FIXNUM ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {	// in case of Fixnum, Fixnum
      regs[ra].u.i += regs[ra+1].u.i;
      return 0;
    }
#if MRBC_USE_FLOAT
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {	// in case of Fixnum, Float
      regs[ra].tt = MRB_TT_FLOAT;
      regs[ra].u.d = regs[ra].u.i + regs[ra+1].u.d;
      return 0;
    }
  }
  if( regs[ra].tt == MRB_TT_FLOAT ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {	// in case of Float, Fixnum
      regs[ra].u.d += regs[ra+1].u.i;
      return 0;
    }
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {	// in case of Float, Float
      regs[ra].u.d += regs[ra+1].u.d;
      return 0;
    }
#endif
  }

  // other case
  op_send(vm, code, regs);
  mrbc_release(vm, &regs[ra+1]);
  return 0;
}


//================================================================
/*!@brief
  Execute OP_ADDI

  R(A) := R(A)+C (Syms[B]=:+)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_addi( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);

  if( regs[ra].tt == MRB_TT_FIXNUM ) {
    regs[ra].u.i += GETARG_C(code);
    return 0;
  }

#if MRBC_USE_FLOAT
  if( regs[ra].tt == MRB_TT_FLOAT ) {
    regs[ra].u.d += GETARG_C(code);
    return 0;
  }
#endif

  not_supported();
  return 0;
}


//================================================================
/*!@brief
  Execute OP_SUB

  R(A) := R(A)-R(A+1) (Syms[B]=:-,C=1)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_sub( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);

  if( regs[ra].tt == MRB_TT_FIXNUM ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {	// in case of Fixnum, Fixnum
      regs[ra].u.i -= regs[ra+1].u.i;
      return 0;
    }
#if MRBC_USE_FLOAT
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {	// in case of Fixnum, Float
      regs[ra].tt = MRB_TT_FLOAT;
      regs[ra].u.d = regs[ra].u.i - regs[ra+1].u.d;
      return 0;
    }
  }
  if( regs[ra].tt == MRB_TT_FLOAT ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {	// in case of Float, Fixnum
      regs[ra].u.d -= regs[ra+1].u.i;
      return 0;
    }
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {	// in case of Float, Float
      regs[ra].u.d -= regs[ra+1].u.d;
      return 0;
    }
#endif
  }

  // other case
  op_send(vm, code, regs);
  mrbc_release(vm, &regs[ra+1]);
  return 0;
}


//================================================================
/*!@brief
  Execute OP_SUBI

  R(A) := R(A)-C (Syms[B]=:-)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_subi( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);

  if( regs[ra].tt == MRB_TT_FIXNUM ) {
    regs[ra].u.i -= GETARG_C(code);
    return 0;
  }

#if MRBC_USE_FLOAT
  if( regs[ra].tt == MRB_TT_FLOAT ) {
    regs[ra].u.d -= GETARG_C(code);
    return 0;
  }
#endif

  not_supported();
  return 0;
}


//================================================================
/*!@brief
  Execute OP_MUL

  R(A) := R(A)*R(A+1) (Syms[B]=:*)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_mul( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);

  if( regs[ra].tt == MRB_TT_FIXNUM ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {	// in case of Fixnum, Fixnum
      regs[ra].u.i *= regs[ra+1].u.i;
      return 0;
    }
#if MRBC_USE_FLOAT
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {	// in case of Fixnum, Float
      regs[ra].tt = MRB_TT_FLOAT;
      regs[ra].u.d = regs[ra].u.i * regs[ra+1].u.d;
      return 0;
    }
  }
  if( regs[ra].tt == MRB_TT_FLOAT ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {	// in case of Float, Fixnum
      regs[ra].u.d *= regs[ra+1].u.i;
      return 0;
    }
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {	// in case of Float, Float
      regs[ra].u.d *= regs[ra+1].u.d;
      return 0;
    }
#endif
  }

  // other case
  op_send(vm, code, regs);
  mrbc_release(vm, &regs[ra+1]);
  return 0;
}


//================================================================
/*!@brief
  Execute OP_DIV

  R(A) := R(A)/R(A+1) (Syms[B]=:/)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_div( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);

  if( regs[ra].tt == MRB_TT_FIXNUM ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {	// in case of Fixnum, Fixnum
      regs[ra].u.i /= regs[ra+1].u.i;
      return 0;
    }
#if MRBC_USE_FLOAT
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {	// in case of Fixnum, Float
      regs[ra].tt = MRB_TT_FLOAT;
      regs[ra].u.d = regs[ra].u.i / regs[ra+1].u.d;
      return 0;
    }
  }
  if( regs[ra].tt == MRB_TT_FLOAT ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {	// in case of Float, Fixnum
      regs[ra].u.d /= regs[ra+1].u.i;
      return 0;
    }
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {	// in case of Float, Float
      regs[ra].u.d /= regs[ra+1].u.d;
      return 0;
    }
#endif
  }

  // other case
  op_send(vm, code, regs);
  mrbc_release(vm, &regs[ra+1]);
  return 0;
}


//================================================================
/*!@brief
  Execute OP_EQ

  R(A) := R(A)==R(A+1)  (Syms[B]=:==,C=1)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_eq( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int result = mrbc_eq(&regs[ra], &regs[ra+1]);

  mrbc_release(vm, &regs[ra+1]);
  mrbc_release(vm, &regs[ra]);
  regs[ra].tt = result ? MRB_TT_TRUE : MRB_TT_FALSE;

  return 0;
}


//================================================================
/*!@brief
  Execute OP_LT

  R(A) := R(A)<R(A+1)  (Syms[B]=:<,C=1)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_lt( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int result;

  if( regs[ra].tt == MRB_TT_FIXNUM ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {
      result = regs[ra].u.i < regs[ra+1].u.i;	// in case of Fixnum, Fixnum
      goto DONE;
    }
#if MRBC_USE_FLOAT
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {
      result = regs[ra].u.i < regs[ra+1].u.d;	// in case of Fixnum, Float
      goto DONE;
    }
  }
  if( regs[ra].tt == MRB_TT_FLOAT ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {
      result = regs[ra].u.d < regs[ra+1].u.i;	// in case of Float, Fixnum
      goto DONE;
    }
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {
      result = regs[ra].u.d < regs[ra+1].u.d;	// in case of Float, Float
      goto DONE;
    }
#endif
  }

  // other case
  op_send(vm, code, regs);
  mrbc_release(vm, &regs[ra+1]);
  return 0;

DONE:
  regs[ra].tt = result ? MRB_TT_TRUE : MRB_TT_FALSE;
  return 0;
}


//================================================================
/*!@brief
  Execute OP_LE

  R(A) := R(A)<=R(A+1)  (Syms[B]=:<=,C=1)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_le( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int result;

  if( regs[ra].tt == MRB_TT_FIXNUM ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {
      result = regs[ra].u.i <= regs[ra+1].u.i;	// in case of Fixnum, Fixnum
      goto DONE;
    }
#if MRBC_USE_FLOAT
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {
      result = regs[ra].u.i <= regs[ra+1].u.d;	// in case of Fixnum, Float
      goto DONE;
    }
  }
  if( regs[ra].tt == MRB_TT_FLOAT ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {
      result = regs[ra].u.d <= regs[ra+1].u.i;	// in case of Float, Fixnum
      goto DONE;
    }
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {
      result = regs[ra].u.d <= regs[ra+1].u.d;	// in case of Float, Float
      goto DONE;
    }
#endif
  }

  // other case
  op_send(vm, code, regs);
  mrbc_release(vm, &regs[ra+1]);
  return 0;

DONE:
  regs[ra].tt = result ? MRB_TT_TRUE : MRB_TT_FALSE;
  return 0;
}


//================================================================
/*!@brief
  Execute OP_GT

  R(A) := R(A)>=R(A+1) (Syms[B]=:>=,C=1)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_gt( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int result;

  if( regs[ra].tt == MRB_TT_FIXNUM ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {
      result = regs[ra].u.i > regs[ra+1].u.i;	// in case of Fixnum, Fixnum
      goto DONE;
    }
#if MRBC_USE_FLOAT
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {
      result = regs[ra].u.i > regs[ra+1].u.d;	// in case of Fixnum, Float
      goto DONE;
    }
  }
  if( regs[ra].tt == MRB_TT_FLOAT ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {
      result = regs[ra].u.d > regs[ra+1].u.i;	// in case of Float, Fixnum
      goto DONE;
    }
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {
      result = regs[ra].u.d > regs[ra+1].u.d;	// in case of Float, Float
      goto DONE;
    }
#endif
  }

  // other case
  op_send(vm, code, regs);
  mrbc_release(vm, &regs[ra+1]);
  return 0;

DONE:
  regs[ra].tt = result ? MRB_TT_TRUE : MRB_TT_FALSE;
  return 0;
}


//================================================================
/*!@brief
  Execute OP_GE

  R(A) := R(A)>=R(A+1) (Syms[B]=:>=,C=1)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_ge( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int result;

  if( regs[ra].tt == MRB_TT_FIXNUM ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {
      result = regs[ra].u.i >= regs[ra+1].u.i;	// in case of Fixnum, Fixnum
      goto DONE;
    }
#if MRBC_USE_FLOAT
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {
      result = regs[ra].u.i >= regs[ra+1].u.d;	// in case of Fixnum, Float
      goto DONE;
    }
  }
  if( regs[ra].tt == MRB_TT_FLOAT ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {
      result = regs[ra].u.d >= regs[ra+1].u.i;	// in case of Float, Fixnum
      goto DONE;
    }
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {
      result = regs[ra].u.d >= regs[ra+1].u.d;	// in case of Float, Float
      goto DONE;
    }
#endif
  }

  // other case
  op_send(vm, code, regs);
  mrbc_release(vm, &regs[ra+1]);
  return 0;

DONE:
  regs[ra].tt = result ? MRB_TT_TRUE : MRB_TT_FALSE;
  return 0;
}


//================================================================
/*!@brief
  Make Array

  R(A) := ary_new(R(B),R(B+1)..R(B+C))

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_array( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int arg_a = GETARG_A(code);
  int arg_b = GETARG_B(code);
  int arg_c = GETARG_C(code);
  mrb_value *ptr;

  mrb_value v;
  v.tt = MRB_TT_ARRAY;
  v.u.array = 0;

  if( arg_c >= 0 ){
    mrb_object *p;

    // Handle
    mrb_value *handle = (mrb_value *)mrbc_alloc(vm, sizeof(mrb_value));
    if( handle == NULL ) return 0;  // ENOMEM
    v.u.array = handle;
    handle->tt = MRB_TT_HANDLE;

    // ptr[0] : array info
    // ptr[1..] : array elements
    ptr = (mrb_value*)mrbc_alloc(vm, sizeof(mrb_value)*(arg_c + 1));
    if( ptr == NULL ) return 0;  // ENOMEM

    handle->u.array = ptr;
    ptr->tt = MRB_TT_FIXNUM;
    ptr->u.i = arg_c;

    p = ptr + 1;
    while( arg_c > 0 ){
      *p++ = regs[arg_b++];
      arg_c--;
    }
  }

  mrbc_release(vm, &regs[arg_a]);
  regs[arg_a] = v;

  return 0;
}


//================================================================
/*!@brief
  Create string object

  R(A) := str_dup(Lit(Bx))

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_string( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
#if MRBC_USE_STRING
  int ra = GETARG_A(code);
  int rb = GETARG_Bx(code);
  mrb_object *pool_obj = vm->pc_irep->pools[rb];

  /* CAUTION: pool_obj->str - 2. see IREP POOL structure. */
  int len = bin_to_uint16(pool_obj->u.str - 2);
  mrb_value value = mrbc_string_new(vm, pool_obj->u.str, len);
  if( value.u.h_str == NULL ) return -1;		// ENOMEM

  mrbc_release(vm, &regs[ra]);
  regs[ra] = value;

#else
  not_supported();
#endif
  return 0;
}


//================================================================
/*!@brief
  Create HASH object

  R(A) := hash_new(R(B),R(B+1)..R(B+C))

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_hash( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  mrb_value *handle, *hash, *src, *dst;

  int arg_a = GETARG_A(code);
  int arg_b = GETARG_B(code);
  int arg_c = GETARG_C(code);

  mrb_value v; // return value
  v.tt = MRB_TT_HASH;

  // make handle for hash pair
  handle = (mrb_value *)mrbc_alloc(vm, sizeof(mrb_value));
  if( handle == NULL ) return 0;  // ENOMEM

  v.u.hash = handle;
  handle->tt = MRB_TT_HANDLE;

  // make hash
  hash = (mrb_value *)mrbc_alloc(vm, sizeof(mrb_value)*(arg_c*2+1));
  if( hash == NULL ) return 0;  // ENOMEM
  handle->u.hash = hash;

  hash[0].tt = MRB_TT_FIXNUM;
  hash[0].u.i = arg_c;

  src = &regs[arg_b];
  dst = &hash[1];
  while( arg_c > 0 ){
    // copy key
    *dst++ = *src++;

    // copy value
    *dst++ = *src++;

    arg_c--;
  }

  mrbc_release(vm, &regs[arg_a]);
  regs[arg_a] = v;

  return 0;
}


//================================================================
/*!@brief
  Execute OP_LAMBDA

  R(A) := lambda(SEQ[Bz],Cz)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_lambda( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int rb = GETARG_Bz(code);      // sequence position in irep list
  // int c = GETARG_C(code);    // TODO: Add flags support for OP_LAMBDA
  mrb_proc *proc = mrbc_rproc_alloc(vm, "(lambda)");

  proc->c_func = 0;
  proc->u.irep = vm->pc_irep->reps[rb];

  mrbc_release(vm, &regs[ra]);
  regs[ra].tt = MRB_TT_PROC;
  regs[ra].u.proc = proc;

  return 0;
}


//================================================================
/*!@brief
  Execute OP_RANGE

  R(A) := range_new(R(B),R(B+1),C)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_range( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  mrb_value value;

  int ra = GETARG_A(code);
  int rb = GETARG_B(code);
  int rc = GETARG_C(code);

  mrbc_dup(vm, &regs[rb]);
  mrbc_dup(vm, &regs[rb+1]);

  value = mrbc_range_new(vm, &regs[rb], &regs[rb+1], rc);
  if( value.u.range == NULL ) return -1;		// ENOMEM

  mrbc_release(vm, &regs[ra]);
  regs[ra] = value;

  return 0;
}


//================================================================
/*!@brief
  Execute OP_CLASS

    R(A) := newclass(R(A),Syms(B),R(A+1))
    Syms(B): class name
    R(A+1): super class

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_class( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  mrb_class *cls;
  mrb_value ret;

  int ra = GETARG_A(code);
  int rb = GETARG_B(code);

  // sym_id : class name
  mrb_irep *cur_irep = vm->pc_irep;
  char *sym = find_irep_symbol(cur_irep->ptr_to_sym, rb);

  // super: pointer to super class
  mrb_class *super  = mrbc_class_object;
  if( regs[ra+1].tt == MRB_TT_CLASS ){
    super = regs[ra+1].u.cls;
  }

  cls = mrbc_class_alloc(vm, sym, super);

  ret.tt = MRB_TT_CLASS;
  ret.u.cls = cls;

  regs[ra] = ret;

  return 0;
}


//================================================================
/*!@brief
  Execute OP_EXEC

  R(A) := blockexec(R(A),SEQ[Bx])

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_exec( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int rb = GETARG_Bx(code);

  mrb_value recv = regs[ra];

  // prepare callinfo
  mrb_callinfo *callinfo = vm->callinfo + vm->callinfo_top;
  callinfo->reg_top = vm->reg_top;
  callinfo->pc_irep = vm->pc_irep;
  callinfo->pc = vm->pc;
  callinfo->target_class = vm->target_class;
  callinfo->n_args = 0;
  vm->callinfo_top++;

  vm->pc = 0;
  vm->pc_irep = vm->irep->reps[rb];

  vm->target_class = find_class_by_object(vm, &recv);

  return 0;
}



//================================================================
/*!@brief
  Execute OP_METHOD

  R(A).newmethod(Syms(B),R(A+1))

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_method( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int rb = GETARG_B(code);
  mrb_proc *rproc = regs[ra+1].u.proc;

  if( regs[ra].tt == MRB_TT_CLASS ) {
    mrb_class *cls = regs[ra].u.cls;
    // sym_id : method name
    mrb_irep *cur_irep = vm->pc_irep;
    char *sym = find_irep_symbol(cur_irep->ptr_to_sym, rb);
    int sym_id = add_sym( sym );
    // add proc to class
    rproc->c_func = 0;
    rproc->sym_id = sym_id;
    rproc->next = cls->procs;
    cls->procs = rproc;
    mrbc_inc_ref_count(rproc);
  }

  return 0;
}


//================================================================
/*!@brief
  Execute OP_TCLASS

  R(A) := target_class

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_tclass( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);

  mrbc_release(vm, &regs[ra]);
  regs[ra].tt = MRB_TT_CLASS;
  regs[ra].u.cls = vm->target_class;

  return 0;
}


//================================================================
/*!@brief
  Execute OP_STOP

  stop VM

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_stop( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int i;
  for( i = 0; i < MAX_REGS_SIZE; i++ ) {
    mrbc_release( vm, &vm->regs[i] );
  }

  vm->flag_preemption = 1;
  return -1;
}


//================================================================
/*!@brief
  Allocate new IREP

  @param  vm	Pointer of VM.
  @return	Pointer of new IREP.
*/
mrb_irep *new_irep(mrb_vm *vm)
{
  mrb_irep *p = (mrb_irep *)mrbc_alloc(vm, sizeof(mrb_irep));
  if( p )
    memset(p, 0, sizeof(mrb_irep));	// caution: assume NULL is zero.
  return p;
}



//================================================================
/*!@brief
  Open the VM.

  @param vm     Pointer to mrb_vm or NULL.
  @return	Pointer to mrb_vm.
  @retval NULL	error.
*/
mrb_vm *mrbc_vm_open(mrb_vm *vm_arg)
{
  int vm_id;
  int i;

  mrb_vm *vm;
  if( (vm = vm_arg) == NULL ) {
    // allocate memory.
    vm = (mrb_vm *)mrbc_raw_alloc( sizeof(mrb_vm) );
    if( vm == NULL ) return NULL;
  }

  // allocate vm id.
  vm_id = 0;
  for( i = 0; i < Num(free_vm_bitmap); i++ ) {
    int n = nlz32( ~free_vm_bitmap[i] );
    if( n < FREE_BITMAP_WIDTH ) {
      free_vm_bitmap[i] |= (1 << (FREE_BITMAP_WIDTH - n - 1));
      vm_id = i * FREE_BITMAP_WIDTH + n + 1;
      break;
    }
  }
  if( vm_id == 0 ) {
    if( vm_arg == NULL ) mrbc_raw_free(vm);
    return NULL;
  }

  // initialize attributes.
  memset(vm, 0, sizeof(mrb_vm));	// caution: assume NULL is zero.
  if( vm_arg == NULL ) vm->flag_need_memfree = 1;
  vm->vm_id = vm_id;

  return vm;
}



//================================================================
/*!@brief
  release mrb_irep holds memory
*/
static void irep_destructor( mrb_irep *irep )
{
  int i;

  // release pools.
  for( i = 0; i < irep->plen; i++ ) {
    mrbc_raw_free( irep->pools[i] );
  }
  if( irep->plen ) mrbc_raw_free( irep->pools );

  // release child ireps.
  for( i = 0; i < irep->rlen; i++ ) {
    irep_destructor( irep->reps[i] );
  }
  if( irep->rlen ) mrbc_raw_free( irep->reps );

  mrbc_raw_free( irep );
}


//================================================================
/*!@brief
  Close the VM.

  @param  vm  Pointer to mrb_vm
*/
void mrbc_vm_close(mrb_vm *vm)
{
  // free vm id.
  int i = (vm->vm_id-1) / FREE_BITMAP_WIDTH;
  int n = (vm->vm_id-1) % FREE_BITMAP_WIDTH;
  assert( i < Num(free_vm_bitmap) );
  free_vm_bitmap[i] &= ~(1 << (FREE_BITMAP_WIDTH - n - 1));

  // free irep and vm
  irep_destructor( vm->irep );
  if( vm->flag_need_memfree ) mrbc_raw_free(vm);
}



//================================================================
/*!@brief
  VM initializer.

  @param  vm  Pointer to VM
*/
void mrbc_vm_begin(mrb_vm *vm)
{
  mrb_class *cls;

  vm->pc_irep = vm->irep;
  vm->pc = 0;
  vm->reg_top = 0;
  memset(vm->regs, 0, sizeof(vm->regs));

  cls = mrbc_class_alloc(vm, "UserTop", mrbc_class_object);
  vm->user_top = cls;

  // set self to reg[0]
  vm->regs[0].tt = MRB_TT_USERTOP;
  vm->regs[0].u.cls = cls;

  vm->callinfo_top = 0;
  memset(vm->callinfo, 0, sizeof(vm->callinfo));

  // target_class
  vm->target_class = cls;

  vm->error_code = 0;
  vm->flag_preemption = 0;
}


//================================================================
/*!@brief
  VM finalizer.

  @param  vm  Pointer to VM
*/
void mrbc_vm_end(mrb_vm *vm)
{
  mrbc_global_clear_vm_id();
  mrbc_free_all(vm);
}


//================================================================
/*!@brief
  Fetch a bytecode and execute

  @param  vm    A pointer of VM.
  @retval 0  No error.
*/
int mrbc_vm_run( mrb_vm *vm )
{
  int ret = 0;

  do {
    mrb_value *regs;
    int opcode;

    // get one bytecode
    uint32_t code = bin_to_uint32(vm->pc_irep->code + vm->pc * 4);
    vm->pc++;

    // regs
    regs = vm->regs + vm->reg_top;

    // Dispatch
    opcode = GET_OPCODE(code);
    switch( opcode ) {
    case OP_NOP:        ret = op_nop       (vm, code, regs); break;
    case OP_MOVE:       ret = op_move      (vm, code, regs); break;
    case OP_LOADL:      ret = op_loadl     (vm, code, regs); break;
    case OP_LOADI:      ret = op_loadi     (vm, code, regs); break;
    case OP_LOADSYM:    ret = op_loadsym   (vm, code, regs); break;
    case OP_LOADNIL:    ret = op_loadnil   (vm, code, regs); break;
    case OP_LOADSELF:   ret = op_loadself  (vm, code, regs); break;
    case OP_LOADT:      ret = op_loadt     (vm, code, regs); break;
    case OP_LOADF:      ret = op_loadf     (vm, code, regs); break;
    case OP_GETGLOBAL:  ret = op_getglobal (vm, code, regs); break;
    case OP_SETGLOBAL:  ret = op_setglobal (vm, code, regs); break;
    case OP_GETCONST:   ret = op_getconst  (vm, code, regs); break;
    case OP_SETCONST:   ret = op_setconst  (vm, code, regs); break;
    case OP_JMP:        ret = op_jmp       (vm, code, regs); break;
    case OP_JMPIF:      ret = op_jmpif     (vm, code, regs); break;
    case OP_JMPNOT:     ret = op_jmpnot    (vm, code, regs); break;
    case OP_SEND:       ret = op_send      (vm, code, regs); break;
    case OP_SENDB:      ret = op_send      (vm, code, regs); break;
    case OP_ENTER:      ret = op_enter     (vm, code, regs); break;
    case OP_RETURN:     ret = op_return    (vm, code, regs); break;
    case OP_BLKPUSH:    ret = op_blkpush   (vm, code, regs); break;
    case OP_ADD:        ret = op_add       (vm, code, regs); break;
    case OP_ADDI:       ret = op_addi      (vm, code, regs); break;
    case OP_SUB:        ret = op_sub       (vm, code, regs); break;
    case OP_SUBI:       ret = op_subi      (vm, code, regs); break;
    case OP_MUL:        ret = op_mul       (vm, code, regs); break;
    case OP_DIV:        ret = op_div       (vm, code, regs); break;
    case OP_EQ:         ret = op_eq        (vm, code, regs); break;
    case OP_LT:         ret = op_lt        (vm, code, regs); break;
    case OP_LE:         ret = op_le        (vm, code, regs); break;
    case OP_GT:         ret = op_gt        (vm, code, regs); break;
    case OP_GE:         ret = op_ge        (vm, code, regs); break;
    case OP_ARRAY:      ret = op_array     (vm, code, regs); break;
    case OP_STRING:     ret = op_string    (vm, code, regs); break;
    case OP_HASH:       ret = op_hash      (vm, code, regs); break;
    case OP_LAMBDA:     ret = op_lambda    (vm, code, regs); break;
    case OP_RANGE:      ret = op_range     (vm, code, regs); break;
    case OP_CLASS:      ret = op_class     (vm, code, regs); break;
    case OP_EXEC:       ret = op_exec      (vm, code, regs); break;
    case OP_METHOD:     ret = op_method    (vm, code, regs); break;
    case OP_TCLASS:     ret = op_tclass    (vm, code, regs); break;
    case OP_STOP:       ret = op_stop      (vm, code, regs); break;
    default:
      console_printf("Skip OP=%02x\n", GET_OPCODE(code));
      break;
    }
  } while( !vm->flag_preemption );

  return ret;
}
