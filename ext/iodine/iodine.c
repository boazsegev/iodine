/* core include */
#include "iodine.h"
/* modules */
#include "mustache.h"

VALUE iodine_rb_IODINE;

void Init_Iodine_ext(void) {
  iodine_rb_IODINE = rb_define_class("Iodine", rb_cObject);
  iodine_setup_value_reference_counter(
      rb_define_class_under(iodine_rb_IODINE, "Base", rb_cObject));
  Init_musta_ext();
}
