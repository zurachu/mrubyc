/*! @file
  @brief
  mruby/c value definitions

  <pre>
  Copyright (C) 2015-2018 Kyushu Institute of Technology.
  Copyright (C) 2015-2018 Shimane IT Open-Innovation Center.

  This file is distributed under BSD 3-Clause License.


  </pre>
*/

#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "value.h"
#include "class.h"
#include "static.h"
#include "symbol.h"
#include "alloc.h"
#include "c_string.h"
#include "c_range.h"
#include "vm.h"


mrb_object *mrbc_obj_alloc(mrb_vm *vm, mrb_vtype tt)
{
  mrb_object *ptr = (mrb_object *)mrbc_alloc(vm, sizeof(mrb_object));
  if( ptr ){
    ptr->tt = tt;
  }
  return ptr;
}

mrb_class *mrbc_class_alloc(mrb_vm *vm, const char *name, mrb_class *super)
{
  mrb_class *ptr = (mrb_class *)mrbc_alloc(vm, sizeof(mrb_class));
  mrb_value v;
  if( ptr ){
    // new class
    mrb_sym sym_id = add_sym(name);
    ptr->super = super;
    ptr->name = sym_id;
    ptr->procs = 0;
    // create value
    v.tt = MRB_TT_CLASS;
    v.u.cls = ptr;
    // Add to global
    const_object_add(sym_id, &v);
  }
  return ptr;
}

mrb_proc *mrbc_rproc_alloc(mrb_vm *vm, const char *name)
{
  mrb_proc *ptr = (mrb_proc *)mrbc_alloc(vm, sizeof(mrb_proc));
  if( ptr ) {
    ptr->sym_id = add_sym(name);
    ptr->next = 0;
  }
  return ptr;
}

mrb_proc *mrbc_rproc_alloc_to_class(mrb_vm *vm, const char *name, mrb_class *cls)
{
  mrb_proc *rproc = mrbc_rproc_alloc(vm, name);
  if( rproc != 0 ){
    rproc->next = cls->procs;
    cls->procs = rproc;
  }
  return rproc;
}


// EQ? two objects
// EQ: return true
// NEQ: return false
int mrbc_eq(mrb_value *v1, mrb_value *v2)
{
  // TT_XXX is different
  if( v1->tt != v2->tt ) return 0;
  // check value
  switch( v1->tt ){
  case MRB_TT_TRUE:
  case MRB_TT_FALSE:
  case MRB_TT_NIL:
    return 1;
  case MRB_TT_FIXNUM:
  case MRB_TT_SYMBOL:
    return v1->u.i == v2->u.i;
  case MRB_TT_FLOAT:
    return v1->u.d == v2->u.d;
  case MRB_TT_STRING:
    if( v1->u.h_str->size != v2->u.h_str->size ) return 0;
    return !memcmp(v1->u.h_str->str, v2->u.h_str->str, v1->u.h_str->size);
  case MRB_TT_ARRAY: {
    mrb_value *array1 = v1->u.array;
    mrb_value *array2 = v2->u.array;
    int i, len = array1[0].u.i;
    if( len != array2[0].u.i ) return 0;
    for( i=1 ; i<=len ; i++ ){
      if( !mrbc_eq(array1+i, array2+i) ) break;
    }
    if( i > len ){
      return 1;
    } else {
      return 0;
    }
  } break;
  default:
    return 0;
  }
}


//================================================================
/*!@brief

  Duplicate mrb_value

  @param   vm    Pointer to VM
  @param   v     Pointer to mrb_value
*/
void mrbc_dup(mrb_vm *vm, mrb_value *v)
{
  switch( v->tt ){
  case MRB_TT_PROC:
  case MRB_TT_STRING:
  case MRB_TT_RANGE:
    mrbc_inc_ref_count(v->u.handle);
    break;
  default:
    // Nothing
    break;
  }
}

//================================================================
/*!@brief

  Release object related memory (reference counter)

  @param   vm    Pointer to VM
  @param   v     Pointer to target mrb_value
*/
void mrbc_release(mrb_vm *vm, mrb_value *v)
{
  switch( v->tt ) {
  case MRB_TT_PROC:
    if( mrbc_dec_ref_count(v->u.handle) == 0 ) {
      mrbc_free(vm, v->u.handle);
    }
    break;

#if MRBC_USE_STRING
  case MRB_TT_STRING:
    if( mrbc_dec_ref_count(v->u.h_str) == 0 ) {
      mrbc_string_delete(vm, v);
    }
    break;
#endif

  case MRB_TT_RANGE:
    if( mrbc_dec_ref_count(v->u.handle) == 0 ) {
      mrbc_range_delete(vm, v);
    }
    break;

  default:
    // Nothing
    break;
  }

  v->tt = MRB_TT_EMPTY;
  v->u.handle = NULL;
}



//================================================================
/*!@brief

  convert ASCII string to integer mruby/c version

  @param  s	source string.
  @param  base	n base.
  @return	result.
*/
int32_t mrbc_atoi( const char *s, int base )
{
  int ret = 0;
  int sign = 0;
  int ch;

 REDO:
  switch( *s ) {
  case '-':
    sign = 1;
    // fall through.
  case '+':
    s++;
    break;

  case ' ':
    s++;
    goto REDO;
  }

  while( (ch = *s++) != '\0' ) {
    int n;

    if( 'a' <= ch ) {
      n = ch - 'a' + 10;
    } else
    if( 'A' <= ch ) {
      n = ch - 'A' + 10;
    } else
    if( '0' <= ch && ch <= '9' ) {
      n = ch - '0';
    } else {
      break;
    }
    if( n >= base ) break;

    ret = ret * base + n;
  }

  if( sign ) ret = -ret;

  return ret;
}



//================================================================
/*!@brief

  mrb_instance constructor

  @param  vm    Pointer to VM.
  @param  cls	Pointer to Class (mrb_class).
  @param  size	size of additional data.
  @return       mrb_instance object.
*/
mrb_value mrbc_instance_new(struct VM *vm, mrb_class *cls, int size)
{
  mrb_value v = {.tt = MRB_TT_OBJECT};
  v.u.instance = (mrb_instance *)mrbc_alloc(vm, sizeof(mrb_instance) + size);
  if( v.u.instance == NULL ) return v;	// ENOMEM
  v.u.instance->cls = cls;

  return v;
}



//================================================================
/*!@brief

  mrb_instance destructor

  @param  vm	pointer to VM.
  @param  v	pointer to target value
*/
void mrbc_instance_delete(struct VM *vm, mrb_value *v)
{
  mrbc_raw_free( v->u.instance );
}
