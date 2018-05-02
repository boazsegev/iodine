#include "iodine_core.h"

#include "facil.h"
/* *****************************************************************************
OS specific patches
***************************************************************************** */

#ifdef __APPLE__
#include <dlfcn.h>
#endif

/** Any patches required by the running environment for consistent behavior */
static void patch_env(void) {
#ifdef __APPLE__
  /* patch for dealing with the High Sierra `fork` limitations */
  void *obj_c_runtime = dlopen("Foundation.framework/Foundation", RTLD_LAZY);
  (void)obj_c_runtime;
#endif
}

/* *****************************************************************************
Constants and State
***************************************************************************** */

VALUE IodineModule;
VALUE IodineBaseModule;

/* *****************************************************************************
Core API
***************************************************************************** */

typedef struct {
  int16_t threads;
  int16_t workers;
} iodine_start_params_s;

static void *iodine_run_outside_GVL(void *params_) {
  iodine_start_params_s *params = params_;
  facil_run(.threads = params->threads, .processes = params->workers,
            .on_idle = NULL, .on_finish = NULL);
  return NULL;
}

/* *****************************************************************************
Core API
***************************************************************************** */

static VALUE iodine_threads_get(VALUE self) {
  VALUE i = rb_ivar_get(self, rb_intern2("@threads", 8));
  if (i == Qnil)
    i = INT2NUM(0);
  return i;
}
static VALUE iodine_threads_set(VALUE self, VALUE val) {
  Check_Type(val, T_FIXNUM);
  if (NUM2SSIZET(val) >= (1 << 12)) {
    rb_raise(rb_eRangeError, "requsted thread count is out of range.");
  }
  rb_ivar_set(self, rb_intern2("@threads", 8), val);
  return val;
}
static VALUE iodine_workers_get(VALUE self) {
  VALUE i = rb_ivar_get(self, rb_intern2("@workers", 8));
  if (i == Qnil)
    i = INT2NUM(0);
  return i;
}
static VALUE iodine_workers_set(VALUE self, VALUE val) {
  Check_Type(val, T_FIXNUM);
  if (NUM2SSIZET(val) >= (1 << 9)) {
    rb_raise(rb_eRangeError, "requsted worker process count is out of range.");
  }
  rb_ivar_set(self, rb_intern2("@workers", 8), val);
  return val;
}

static VALUE iodine_start(VALUE self) {
  if (facil_is_running()) {
    rb_raise(rb_eRuntimeError, "Iodine already running!");
  }
  VALUE threads_rb = iodine_threads_get(self);
  VALUE workers_rb = iodine_workers_get(self);
  iodine_start_params_s params = {
      .threads = NUM2SHORT(threads_rb), .workers = NUM2SHORT(threads_rb),
  };
  IodineCaller.leaveGVL(iodine_run_outside_GVL, &params);
  return self;
}

/* *****************************************************************************
Ruby loads the library and invokes the Init_<lib_name> function...

Here we connect all the C code to the Ruby interface, completing the bridge
between Lib-Server and Ruby.
***************************************************************************** */
void Init_iodine(void) {
  // load any environment specific patches
  patch_env();
  // force the GVL state for the main thread
  IodineCaller.set_GVL(1);
  // Create the Iodine module (namespace)
  IodineModule = rb_define_module("Iodine");
  IodineBaseModule = rb_define_module_under(IodineModule, "Base");
  // register core methods
  rb_define_module_function(IodineModule, "threads", iodine_threads_get, 0);
  rb_define_module_function(IodineModule, "threads=", iodine_threads_set, 1);
  rb_define_module_function(IodineModule, "workers", iodine_workers_get, 0);
  rb_define_module_function(IodineModule, "workers=", iodine_workers_set, 1);
  rb_define_module_function(IodineModule, "workers", iodine_start, 0);

  // initialize Object storage for GC protection
  iodine_storage_init();
}
