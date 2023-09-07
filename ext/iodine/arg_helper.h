#ifndef H___IODINE_ARG_HELPER___H
#define H___IODINE_ARG_HELPER___H
#include "iodine.h"

/* *****************************************************************************
Ruby Multi-Argument Helper
***************************************************************************** */

typedef struct {
  VALUE *target;
  ID id;
  fio_buf_info_s name;
  VALUE default_value;
  int required;
} fio_rb_multi_arg_s;

/** Reads and validates method arguments (either "splat" or Hash Map). */
static int fio_rb_multi_arg(int argc, const VALUE *argv,
                            fio_rb_multi_arg_s *a) {
  int i = 0;
  if (argc == 1 && TYPE((argv[0])) == RUBY_T_HASH) {
    /* named parameters (hash table) */
    for (i = 0; a[i].target; ++i) {
      VALUE tmp = Qnil;
      if (a[i].id)
        tmp = rb_hash_aref(argv[0], rb_id2sym(a[i].id));
      else {
        tmp = rb_id2sym(rb_intern2(a[i].name.buf, a[i].name.len));
        tmp = rb_hash_aref(argv[0], tmp);
      }
      if (tmp == Qnil) {
        if (a[i].required)
          goto missing_required;
        tmp = a[i].default_value;
      }
      a[i].target[0] = tmp;
    }
    return 0;
  } else {
    /* unnamed parameters (list of parameters) */
    for (i = 0; i < argc || a[i].target; ++i) {
      if (!a[i].target)
        goto too_many_arguments;
      VALUE tmp = Qnil;
      tmp = (i < argc) ? argv[i] : a[i].default_value;
      if (tmp == Qnil) {
        if (a[i].required)
          goto missing_required;
        tmp = a[i].default_value;
      }
      a[i].target[0] = tmp;
    }
    return 0;
  }
too_many_arguments:
  rb_raise(rb_eArgError, "too many arguments in method call.");
  return -1;
missing_required:
  rb_raise(rb_eArgError, "missing required argument %s.", a[i].name.buf);
  return -1;
}
/**
 * Reads and validates method arguments (either "splat" or Hash Map).
 *
 * Use:
 *
 *      fio_rb_multi_arg(argc, argv,
 *           FIO_RB_ARG(var_name, id_if_known, "named_var", 0, 1), // required
 *           FIO_RB_ARG(var_name, id_if_known, "named_var2", default_val2, 0),
 *           FIO_RB_ARG(var_name, id_if_known, "named_var3", default_val3, 0));
 */
#define fio_rb_multi_arg(argc, argv, ...)                                      \
  fio_rb_multi_arg(argc, argv, (fio_rb_multi_arg_s[]){__VA_ARGS__, {0}})
#define FIO_RB_ARG(t_var, id_, name_, default_, required_)                     \
  {                                                                            \
    .target = &t_var, .id = id_, .name = FIO_BUF_INFO1(((char *)name_)),       \
    .default_value = default_, .required = required_                           \
  }

#endif /* H___IODINE_ARG_HELPER___H */
