/* core include */
#include "iodine.h"

/* *****************************************************************************
Deprecation Warnings
***************************************************************************** */

/** @deprecated use {Iodine::TLS.add_cert}. */
static VALUE iodine_tls_cert_add_old_name(int argc, VALUE *argv, VALUE self);

/* *****************************************************************************
Initialize module
***************************************************************************** */

static void Init_Iodine(void) {
  rb_define_singleton_method(iodine_rb_IODINE, "start", iodine_start, 0);
  rb_define_singleton_method(iodine_rb_IODINE, "stop", iodine_stop, 0);

  rb_define_singleton_method(iodine_rb_IODINE,
                             "running?",
                             iodine_is_running,
                             0);
  rb_define_singleton_method(iodine_rb_IODINE, "master?", iodine_is_master, 0);
  rb_define_singleton_method(iodine_rb_IODINE, "worker?", iodine_is_worker, 0);

  rb_define_singleton_method(iodine_rb_IODINE, "workers", iodine_workers, 0);
  rb_define_singleton_method(iodine_rb_IODINE,
                             "workers=",
                             iodine_workers_set,
                             1);

  rb_define_singleton_method(iodine_rb_IODINE, "threads", iodine_threads, 0);
  rb_define_singleton_method(iodine_rb_IODINE,
                             "threads=",
                             iodine_threads_set,
                             1);

  rb_define_singleton_method(iodine_rb_IODINE,
                             "verbosity",
                             iodine_verbosity,
                             0);
  rb_define_singleton_method(iodine_rb_IODINE,
                             "verbosity=",
                             iodine_verbosity_set,
                             1);

  rb_define_singleton_method(iodine_rb_IODINE, "secret", iodine_secret, 0);
  rb_define_singleton_method(iodine_rb_IODINE, "secret=", iodine_secret_set, 1);

  rb_define_singleton_method(iodine_rb_IODINE,
                             "shutdown_timeout",
                             iodine_shutdown_timeout,
                             0);
  rb_define_singleton_method(iodine_rb_IODINE,
                             "shutdown_timeout=",
                             iodine_shutdown_timeout_set,
                             1);

  rb_define_module_function(iodine_rb_IODINE, "run", iodine_defer_run, 0);
  rb_define_module_function(iodine_rb_IODINE,
                            "async",
                            iodine_defer_run_async,
                            0);

  rb_define_module_function(iodine_rb_IODINE,
                            "run_after",
                            iodine_defer_run_after,
                            -1);
  rb_define_module_function(iodine_rb_IODINE, "on_state", iodine_on_state, 1);

  rb_define_singleton_method(iodine_rb_IODINE, "listen", iodine_listen_rb, -1);
}

/* *****************************************************************************
Cleanup
***************************************************************************** */

FIO_SFUNC void *iodine___perform_exit_outside_gvl(void *ignr_) {
  (void)ignr_;
  fio_queue_perform_all(fio_io_queue());
  fio_cli_end();
  fio_state_callback_force(FIO_CALL_AT_EXIT);
  fio_state_callback_clear(FIO_CALL_AT_EXIT);
  STORE.destroy();
  return NULL;
}

FIO_SFUNC void iodine___perform_exit(VALUE ignr_) {
  (void)ignr_;
  // rb_gc();
  /* iodine runs outside of GVL, but at_exit runs in GVL.. so... */
  rb_thread_call_without_gvl(iodine___perform_exit_outside_gvl,
                             NULL,
                             NULL,
                             NULL);
}

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
Initialize Extension
***************************************************************************** */

void Init_iodine(void) {
  patch_env();
  IODINE_THREAD_POOL = FIO_IO_ASYN_INIT;
  fio_state_callback_force(FIO_CALL_ON_INITIALIZE);

  IodineUTF8Encoding = rb_enc_find("UTF-8");
  IodineBinaryEncoding = rb_enc_find("binary");

  /** The Iodine module is where it all happens. */
  iodine_rb_IODINE = rb_define_module("Iodine");
  STORE.hold(iodine_rb_IODINE);
  /** The PubSub module contains Pub/Sub related classes / data. */
  iodine_rb_IODINE_PUBSUB = rb_define_module_under(iodine_rb_IODINE, "PubSub");
  STORE.hold(iodine_rb_IODINE_PUBSUB);
  /** The Iodine::Base module is for internal concerns. */
  iodine_rb_IODINE_BASE =
      rb_define_class_under(iodine_rb_IODINE, "Base", rb_cObject);
  rb_undef_alloc_func(iodine_rb_IODINE_BASE);
  STORE.hold(iodine_rb_IODINE_BASE);
  /** The Iodine::Base::App404 module is for static file only. */
  iodine_rb_IODINE_BASE_APP404 =
      rb_define_module_under(iodine_rb_IODINE_BASE, "App404");
  STORE.hold(iodine_rb_IODINE_BASE_APP404);
  { /** Initialize `STORE` and object reference counting. */
    iodine_setup_value_reference_counter(iodine_rb_IODINE_BASE);
    rb_define_singleton_method(iodine_rb_IODINE_BASE,
                               "print_debug",
                               iodine_store___print_debug,
                               0);
  }

  IODINE_CONST_ID_STORE(IODINE_CALL_ID, "call");
  IODINE_CONST_ID_STORE(IODINE_CLOSE_ID, "close");
  IODINE_CONST_ID_STORE(IODINE_EACH_ID, "each");
  IODINE_CONST_ID_STORE(IODINE_FILENO_ID, "fileno");
  IODINE_CONST_ID_STORE(IODINE_NEW_ID, "new");
  IODINE_CONST_ID_STORE(IODINE_TO_PATH_ID, "to_path");
  IODINE_CONST_ID_STORE(IODINE_TO_S_ID, "to_s");
  IODINE_CONST_ID_STORE(IODINE_TO_JSON_ID, "to_json");

  IODINE_CONST_ID_STORE(IODINE_INDEX_ID, "index");
  IODINE_CONST_ID_STORE(IODINE_SHOW_ID, "show");
  IODINE_CONST_ID_STORE(IODINE_EDIT_ID, "edit");
  IODINE_CONST_ID_STORE(IODINE_CREATE_ID, "create");
  IODINE_CONST_ID_STORE(IODINE_UPDATE_ID, "update");
  IODINE_CONST_ID_STORE(IODINE_DELETE_ID, "delete");

  IODINE_CONST_ID_STORE(IODINE_RACK_HIJACK_ID, "rack_hijack");
  IODINE_CONST_ID_STORE(IODINE_ON_AUTHENTICATE_ID, "on_authenticate");
  IODINE_CONST_ID_STORE(IODINE_ON_AUTHENTICATE_SSE_ID, "on_authenticate_sse");
  IODINE_CONST_ID_STORE(IODINE_ON_AUTHENTICATE_WEBSOCKET_ID,
                        "on_authenticate_websocket");
  IODINE_CONST_ID_STORE(IODINE_ON_CLOSE_ID, "on_close");
  IODINE_CONST_ID_STORE(IODINE_ON_DATA_ID, "on_data");
  IODINE_CONST_ID_STORE(IODINE_ON_DRAINED_ID, "on_drained");
  IODINE_CONST_ID_STORE(IODINE_ON_EVENTSOURCE_ID, "on_eventsource");
  IODINE_CONST_ID_STORE(IODINE_ON_EVENTSOURCE_RECONNECT_ID,
                        "on_eventsource_reconnect");
  IODINE_CONST_ID_STORE(IODINE_ON_FINISH_ID, "on_finish");
  IODINE_CONST_ID_STORE(IODINE_ON_HTTP_ID, "on_http");
  IODINE_CONST_ID_STORE(IODINE_ON_MESSAGE_ID, "on_message");
  IODINE_CONST_ID_STORE(IODINE_ON_OPEN_ID, "on_open");
  IODINE_CONST_ID_STORE(IODINE_ON_SHUTDOWN_ID, "on_shutdown");
  IODINE_CONST_ID_STORE(IODINE_ON_TIMEOUT_ID, "on_timeout");

  IODINE_CONST_ID_STORE(IODINE_STATE_PRE_START, "pre_start");
  IODINE_CONST_ID_STORE(IODINE_STATE_BEFORE_FORK, "before_fork");
  IODINE_CONST_ID_STORE(IODINE_STATE_AFTER_FORK, "after_fork");
  IODINE_CONST_ID_STORE(IODINE_STATE_ENTER_CHILD, "enter_child");
  IODINE_CONST_ID_STORE(IODINE_STATE_ENTER_MASTER, "enter_master");
  IODINE_CONST_ID_STORE(IODINE_STATE_ON_START, "start");
  IODINE_CONST_ID_STORE(IODINE_STATE_ON_PARENT_CRUSH, "parent_crush");
  IODINE_CONST_ID_STORE(IODINE_STATE_ON_CHILD_CRUSH, "child_crush");
  IODINE_CONST_ID_STORE(IODINE_STATE_ON_SHUTDOWN, "shutdown");
  IODINE_CONST_ID_STORE(IODINE_STATE_ON_STOP, "stop");
  IODINE_CONST_ID_STORE(IODINE_STATE_ON_IDLE, "idle");
  IODINE_CONST_ID_STORE(IODINE_STATE_ON_EXIT, "exit");

  STORE.hold(IODINE_RACK_HIJACK_ID_SYM = rb_id2sym(IODINE_RACK_HIJACK_ID));
  STORE.hold(IODINE_RACK_HIJACK_SYM = rb_id2sym(rb_intern("rack.hijack")));
  STORE.hold(IODINE_RACK_HIJACK_STR = rb_sym_to_s(IODINE_RACK_HIJACK_SYM));

  IODINE_RACK_PROTOCOL_STR =
      STORE.frozen_str(FIO_STR_INFO1((char *)"rack.protocol"));
  IODINE_RACK_UPGRADE_STR =
      STORE.frozen_str(FIO_STR_INFO1((char *)"rack.upgrade"));
  IODINE_RACK_UPGRADE_Q_STR =
      STORE.frozen_str(FIO_STR_INFO1((char *)"rack.upgrade?"));
  STORE.hold(IODINE_RACK_UPGRADE_WS_SYM = rb_id2sym(rb_intern("websocket")));
  STORE.hold(IODINE_RACK_UPGRADE_SSE_SYM = rb_id2sym(rb_intern("sse")));
  STORE.hold(IODINE_RACK_AFTER_RPLY_STR =
                 rb_str_new_static("rack.after_reply", 16));

  Init_Iodine();
  Init_Iodine_Base_CLI();
  Init_Iodine_Compression();
  Init_Iodine_Crypto();
  Init_Iodine_Mustache();
  Init_Iodine_Utils();
  Init_Iodine_JSON();
  Init_Iodine_Listener();
  Init_Iodine_MiniMap();
  Init_Iodine_PubSub_Engine();
  Init_Iodine_PubSub_Subscription();
  Init_Iodine_PubSub_History();
  Init_Iodine_Redis();
  Init_Iodine_PubSub_Message();
  Init_Iodine_TLS();
  Init_Iodine_Connection();
  /* make sure the Iodine.run / async methods are available pre-start */
  fio_io_async_attach(&IODINE_THREAD_POOL, (uint32_t)fio_cli_get_i("-t"));
#ifdef SIGUSR1
  /* support hot restarting of workers */
  fio_io_restart_on_signal(SIGUSR1);
#endif
  rb_set_end_proc(iodine___perform_exit, (VALUE)Qnil);
}
