/* core include */
#include "iodine.h"
/* modules */
#include "iodine_cli.h"
#include "iodine_connection.h"
#include "iodine_mustache.h"
#include "iodine_utils.h"

static VALUE iodine_verbosity(VALUE klass) {
  return RB_INT2FIX(((long)FIO_LOG_LEVEL_GET()));
  (void)klass;
}

static VALUE iodine_verbosity_set(VALUE klass, VALUE num) {
  FIO_LOG_LEVEL_SET(RB_FIX2INT(num));
  return klass;
}

void Init_iodine_ext(void) {
  fio_state_callback_force(FIO_CALL_ON_INITIALIZE);
  IodineUTF8Encoding = rb_enc_find("UTF-8");
  iodine_rb_IODINE = rb_define_module("Iodine");
  /** The Iodine::Base module is for internal concerns. */
  VALUE base = rb_define_class_under(iodine_rb_IODINE, "Base", rb_cObject);
  rb_define_singleton_method(iodine_rb_IODINE,
                             "verbosity",
                             iodine_verbosity,
                             0);
  rb_define_singleton_method(iodine_rb_IODINE,
                             "verbosity=",
                             iodine_verbosity_set,
                             1);

  iodine_setup_value_reference_counter(base);
  Init_iodine_cli();
  Init_iodine_musta();
  Init_iodine_utils();
  Init_iodine_json();
  Init_iodine_connection();
}
