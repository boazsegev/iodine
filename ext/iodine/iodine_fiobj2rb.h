#ifndef H_RB_FIOBJ2RUBY_H
/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#define H_RB_FIOBJ2RUBY_H
#include <fiobj.h>
#include <ruby.h>

#include "iodine_store.h"

typedef struct {
  FIOBJ stack;
  uintptr_t count;
  VALUE rb;
  uint8_t str2sym;
} fiobj2rb_s;

typedef struct {
  uint8_t str2sym;
} fiobj2rb_settings_s;

static inline VALUE fiobj2rb(FIOBJ o, uint8_t str2sym) {
  VALUE rb;
  if (!o)
    return Qnil;
  switch (FIOBJ_TYPE(o)) {
  case FIOBJ_T_NUMBER:
    rb = LONG2FIX(fiobj_obj2num(o));
    break;
  case FIOBJ_T_TRUE:
    rb = Qtrue;
    break;
  case FIOBJ_T_FALSE:
    rb = Qfalse;
    break;
  case FIOBJ_T_FLOAT:
    rb = rb_float_new(fiobj_obj2float(o));
    break;
  case FIOBJ_T_DATA:    /* fallthrough */
  case FIOBJ_T_UNKNOWN: /* fallthrough */
  case FIOBJ_T_STRING: {
    fio_str_info_s tmp = fiobj_obj2cstr(o);
    if (str2sym) {
      rb = rb_intern2(tmp.data, tmp.len);
      rb = ID2SYM(rb);
    } else {
      rb = rb_str_new(tmp.data, tmp.len);
    }

  } break;
  case FIOBJ_T_ARRAY:
    rb = rb_ary_new();
    break;
  case FIOBJ_T_HASH:
    rb = rb_hash_new();
    break;
  case FIOBJ_T_NULL: /* fallthrough */
  default:
    rb = Qnil;
    break;
  };
  return rb;
}

static int fiobj2rb_task(FIOBJ o, void *data_) {
  fiobj2rb_s *data = data_;
  VALUE rb_tmp;
  rb_tmp = fiobj2rb(o, 0);
  IodineStore.add(rb_tmp);
  if (data->rb) {
    if (RB_TYPE_P(data->rb, T_HASH)) {
      rb_hash_aset(data->rb, fiobj2rb(fiobj_hash_key_in_loop(), data->str2sym),
                   rb_tmp);
    } else {
      rb_ary_push(data->rb, rb_tmp);
    }
    --(data->count);
    IodineStore.remove(rb_tmp);
  } else {
    data->rb = rb_tmp;
    // IodineStore.add(rb_tmp);
  }
  if (FIOBJ_TYPE_IS(o, FIOBJ_T_ARRAY)) {
    fiobj_ary_push(data->stack, (FIOBJ)data->count);
    fiobj_ary_push(data->stack, (FIOBJ)data->rb);
    data->count = fiobj_ary_count(o);
    data->rb = rb_tmp;
  } else if (FIOBJ_TYPE_IS(o, FIOBJ_T_HASH)) {
    fiobj_ary_push(data->stack, (FIOBJ)data->count);
    fiobj_ary_push(data->stack, (FIOBJ)data->rb);
    data->count = fiobj_hash_count(o);
    data->rb = rb_tmp;
  }
  while (data->count == 0 && fiobj_ary_count(data->stack)) {
    data->rb = fiobj_ary_pop(data->stack);
    data->count = fiobj_ary_pop(data->stack);
  }
  return 0;
}

static inline VALUE fiobj2rb_deep(FIOBJ obj, uint8_t str2sym) {
  fiobj2rb_s data = {.stack = fiobj_ary_new2(4), .str2sym = str2sym};

  /* deep copy */
  fiobj_each2(obj, fiobj2rb_task, &data);
  /* cleanup (shouldn't happen, but what the hell)... */
  while (fiobj_ary_pop(data.stack))
    ;
  fiobj_free(data.stack);
  // IodineStore.remove(data.rb); // don't remove data
  return data.rb;
}

// require 'iodine'
// Iodine::JSON.parse "{\"1\":[1,2,3,4]}"
// Iodine::JSON.parse IO.binread("")
#endif /* H_RB_FIOBJ2RUBY_H */
