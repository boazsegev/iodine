#ifndef H___IODINE_PUBSUB_MSG___H
#define H___IODINE_PUBSUB_MSG___H
#include "iodine.h"

/* *****************************************************************************
Ruby PubSub Message Object
***************************************************************************** */

typedef enum {
  IODINE_PUBSUB_MSG_STORE_id,
  IODINE_PUBSUB_MSG_STORE_channel,
  IODINE_PUBSUB_MSG_STORE_filter,
  IODINE_PUBSUB_MSG_STORE_message,
  IODINE_PUBSUB_MSG_STORE_published,
  IODINE_PUBSUB_MSG_STORE_FINISH,
} iodine_pubsub_msg_store_e;

typedef struct iodine_pubsub_msg_s {
  fio_msg_s *msg;
  VALUE store[IODINE_PUBSUB_MSG_STORE_FINISH];
} iodine_pubsub_msg_s;

static size_t iodine_pubsub_msg_data_size(const void *ptr_) {
  iodine_pubsub_msg_s *m = (iodine_pubsub_msg_s *)ptr_;
  return sizeof(*m) + (m->msg ? (sizeof(m->msg[0]) + m->msg->message.len +
                                 m->msg->channel.len)
                              : 0);
}

static void iodine_pubsub_msg_mark(void *m_) {
  iodine_pubsub_msg_s *m = (iodine_pubsub_msg_s *)m_;
  for (size_t i = 0; i < IODINE_PUBSUB_MSG_STORE_FINISH; ++i)
    if (!IODINE_STORE_IS_SKIP(m->store[i]))
      rb_gc_mark(m->store[i]);
}

static void iodine_pubsub_msg_free(void *ptr_) {
  iodine_pubsub_msg_s *c = (iodine_pubsub_msg_s *)ptr_;
  fio_free(c);
  FIO_LEAK_COUNTER_ON_FREE(iodine_pubsub_msg);
}

static const rb_data_type_t IODINE_PUBSUB_MSG_DATA_TYPE = {
    .wrap_struct_name = "IodinePSMessage",
    .function =
        {
            .dmark = iodine_pubsub_msg_mark,
            .dfree = iodine_pubsub_msg_free,
            .dsize = iodine_pubsub_msg_data_size,
        },
    .data = NULL,
    // .flags = RUBY_TYPED_FREE_IMMEDIATELY,
};

static VALUE iodine_pubsub_msg_alloc(VALUE klass) {
  iodine_pubsub_msg_s *m = (iodine_pubsub_msg_s *)fio_malloc(sizeof(*m));
  if (!m)
    goto no_memory;
  *m = (iodine_pubsub_msg_s){0};
  for (size_t i = 0; i < IODINE_PUBSUB_MSG_STORE_FINISH; ++i)
    m->store[i] = Qnil;
  FIO_LEAK_COUNTER_ON_ALLOC(iodine_pubsub_msg);
  return TypedData_Wrap_Struct(klass, &IODINE_PUBSUB_MSG_DATA_TYPE, m);
no_memory:
  FIO_LOG_FATAL("Memory allocation failed");
  fio_io_stop();
  return Qnil;
}

static iodine_pubsub_msg_s *iodine_pubsub_msg_get(VALUE self) {
  iodine_pubsub_msg_s *m;
  TypedData_Get_Struct(self,
                       iodine_pubsub_msg_s,
                       &IODINE_PUBSUB_MSG_DATA_TYPE,
                       m);
  return m;
}

static VALUE iodine_pubsub_msg_new(fio_msg_s *msg) {
  VALUE m = rb_obj_alloc(iodine_rb_IODINE_PUBSUB_MSG);
  STORE.hold(m);
  iodine_pubsub_msg_s *c = iodine_pubsub_msg_get(m);
  c->store[IODINE_PUBSUB_MSG_STORE_id] = ULL2NUM(msg->id);
  c->store[IODINE_PUBSUB_MSG_STORE_channel] =
      (msg->channel.len ? rb_usascii_str_new(msg->channel.buf, msg->channel.len)
                        : Qnil);
  ;
  c->store[IODINE_PUBSUB_MSG_STORE_filter] =
      (msg->filter ? INT2NUM(((int16_t)(msg->filter))) : Qnil);
  c->store[IODINE_PUBSUB_MSG_STORE_message] =
      (msg->message.len ? rb_usascii_str_new(msg->message.buf, msg->message.len)
                        : Qnil);
  c->store[IODINE_PUBSUB_MSG_STORE_published] =
      (msg->published ? ULL2NUM(msg->published) : Qnil);
  return m;
}

#define IODINE_DEF_GET_SET_FUNC(val_name, ...)                                 \
  /** Returns the message's val_name */                                        \
  static VALUE iodine_pubsub_msg_##val_name##_get(VALUE self) {                \
    iodine_pubsub_msg_s *c = iodine_pubsub_msg_get(self);                      \
    if (!c)                                                                    \
      return Qnil;                                                             \
    return c->store[IODINE_PUBSUB_MSG_STORE_##val_name];                       \
  }                                                                            \
  /** Sets the message's val_name */                                           \
  static VALUE iodine_pubsub_msg_##val_name##_set(VALUE self, VALUE val) {     \
    iodine_pubsub_msg_s *c = iodine_pubsub_msg_get(self);                      \
    if (!c)                                                                    \
      return Qnil;                                                             \
    return (c->store[IODINE_PUBSUB_MSG_STORE_##val_name] = val);               \
  }

IODINE_DEF_GET_SET_FUNC(id);
IODINE_DEF_GET_SET_FUNC(channel);
IODINE_DEF_GET_SET_FUNC(filter);
IODINE_DEF_GET_SET_FUNC(message);
IODINE_DEF_GET_SET_FUNC(published);

#undef IODINE_DEF_GET_SET_FUNC

/**
 * Iodine::PubSub::Message class instances are passed to subscription callbacks.
 */
static void Init_Iodine_PubSub_Message(void) {
  /** Initialize Iodine::PubSub::Message */ // clang-format off
  
  iodine_rb_IODINE_PUBSUB_MSG = rb_define_class_under(iodine_rb_IODINE_PUBSUB, "Message", rb_cObject);
  STORE.hold(iodine_rb_IODINE_PUBSUB_MSG);
  rb_define_alloc_func(iodine_rb_IODINE_PUBSUB_MSG, iodine_pubsub_msg_alloc);
  rb_define_method(iodine_rb_IODINE_PUBSUB_MSG, "id", iodine_pubsub_msg_id_get, 0);
  rb_define_method(iodine_rb_IODINE_PUBSUB_MSG, "channel", iodine_pubsub_msg_channel_get, 0);
  rb_define_method(iodine_rb_IODINE_PUBSUB_MSG, "event", iodine_pubsub_msg_channel_get, 0);
  rb_define_method(iodine_rb_IODINE_PUBSUB_MSG, "filter", iodine_pubsub_msg_filter_get, 0);
  rb_define_method(iodine_rb_IODINE_PUBSUB_MSG, "message", iodine_pubsub_msg_message_get, 0);
  rb_define_method(iodine_rb_IODINE_PUBSUB_MSG, "msg", iodine_pubsub_msg_message_get, 0);
  rb_define_method(iodine_rb_IODINE_PUBSUB_MSG, "data", iodine_pubsub_msg_message_get, 0);
  rb_define_method(iodine_rb_IODINE_PUBSUB_MSG, "published", iodine_pubsub_msg_published_get, 0);

  rb_define_method(iodine_rb_IODINE_PUBSUB_MSG, "to_s", iodine_pubsub_msg_message_get, 0);

  rb_define_method(iodine_rb_IODINE_PUBSUB_MSG, "id=", iodine_pubsub_msg_id_set, 1);
  rb_define_method(iodine_rb_IODINE_PUBSUB_MSG, "channel=", iodine_pubsub_msg_channel_set, 1);
  rb_define_method(iodine_rb_IODINE_PUBSUB_MSG, "event=", iodine_pubsub_msg_channel_set, 1);
  rb_define_method(iodine_rb_IODINE_PUBSUB_MSG, "filter=", iodine_pubsub_msg_filter_set, 1);
  rb_define_method(iodine_rb_IODINE_PUBSUB_MSG, "message=", iodine_pubsub_msg_message_set, 1);
  rb_define_method(iodine_rb_IODINE_PUBSUB_MSG, "msg=", iodine_pubsub_msg_message_set, 1);
  rb_define_method(iodine_rb_IODINE_PUBSUB_MSG, "data=", iodine_pubsub_msg_message_set, 1);
  rb_define_method(iodine_rb_IODINE_PUBSUB_MSG, "published=", iodine_pubsub_msg_published_set, 1);
} // clang-format off


#endif /* H___IODINE_PUBSUB_MSG___H */
