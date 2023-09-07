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

  rb_define_module_function(iodine_rb_IODINE, "run", iodine_defer_run_async, 0);
  rb_define_module_function(iodine_rb_IODINE, "defer", iodine_defer_run, 0);

  rb_define_module_function(iodine_rb_IODINE,
                            "run_after",
                            iodine_defer_run_after,
                            -1);
  rb_define_module_function(iodine_rb_IODINE, "on_state", iodine_on_state, 1);

  rb_define_singleton_method(iodine_rb_IODINE, "listen", iodine_listen_rb, -1);
}

/* *****************************************************************************
Initialize Extension
***************************************************************************** */

void Init_iodine(void) {
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
  IODINE_CONST_ID_STORE(IODINE_STATE_ON_START, "on_start");
  IODINE_CONST_ID_STORE(IODINE_STATE_ON_PARENT_CRUSH, "on_parent_crush");
  IODINE_CONST_ID_STORE(IODINE_STATE_ON_CHILD_CRUSH, "on_child_crush");
  IODINE_CONST_ID_STORE(IODINE_STATE_ON_SHUTDOWN, "on_shutdown");
  IODINE_CONST_ID_STORE(IODINE_STATE_ON_STOP, "on_stop");

  STORE.hold(IODINE_RACK_HIJACK_SYM = rb_id2sym(IODINE_RACK_HIJACK_ID));
  STORE.hold(IODINE_RACK_HIJACK_STR = rb_str_new_static("rack.hijack", 11));

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
  Init_Iodine_Mustache();
  Init_Iodine_Utils();
  Init_Iodine_JSON();
  Init_Iodine_MiniMap();
  Init_Iodine_PubSub_Engine();
  Init_Iodine_PubSub_Message();
  Init_Iodine_TLS();
  Init_Iodine_Connection();

  fio_io_async_attach(&IODINE_THREAD_POOL, (uint32_t)fio_cli_get_i("-t"));
}
