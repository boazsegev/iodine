#include "iodine.h"

/* *****************************************************************************
Ruby PubSub Message Object
***************************************************************************** */

FIO_LEAK_COUNTER_DEF(iodine_pubsub_msg)

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

VALUE IODINE_PUBSUB_MSG_KLASS;

static void iodine_pubsub_msg_mark(struct iodine_pubsub_msg_s *m) {
  for (size_t i = 0; i < IODINE_PUBSUB_MSG_STORE_FINISH; ++i)
    if (!IODINE_STORE_IS_SKIP(m->store[i]))
      rb_gc_mark(m->store[i]);
}

static void iodine_pubsub_msg_free(void *ptr_) {
  iodine_pubsub_msg_s *c = (iodine_pubsub_msg_s *)ptr_;
  if (!c)
    return;
  FIO_LEAK_COUNTER_ON_FREE(iodine_pubsub_msg);
  for (size_t i = IODINE_PUBSUB_MSG_STORE_FINISH; i;)
    STORE.release(c->store[--i]);
}

static VALUE iodine_pubsub_msg_alloc(VALUE klass) {
  iodine_pubsub_msg_s *wrapper;
  VALUE o = Data_Make_Struct(klass,
                             iodine_pubsub_msg_s,
                             iodine_pubsub_msg_mark,
                             iodine_pubsub_msg_free,
                             wrapper);
  *wrapper = (iodine_pubsub_msg_s){NULL};
  FIO_LEAK_COUNTER_ON_ALLOC(iodine_pubsub_msg);
  for (size_t i = 0; i < IODINE_PUBSUB_MSG_STORE_FINISH; ++i)
    wrapper->store[i] = Qnil;
  return o;
}

static iodine_pubsub_msg_s *iodine_pubsub_msg_get(VALUE self) {
  iodine_pubsub_msg_s *c;
  Data_Get_Struct(self, iodine_pubsub_msg_s, c);
  return c;
}

static VALUE iodine_pubsub_msg_create(fio_msg_s *msg) {
  VALUE m = rb_funcall(IODINE_PUBSUB_MSG_KLASS, IODINE_NEW_ID, 0, NULL);
  STORE.hold(m);
  iodine_pubsub_msg_s *c = iodine_pubsub_msg_get(m);
  c->store[IODINE_PUBSUB_MSG_STORE_id] = ULL2NUM(msg->id);
  c->store[IODINE_PUBSUB_MSG_STORE_channel] =
      (msg->channel.len ? rb_str_new(msg->channel.buf, msg->channel.len)
                        : Qnil);
  ;
  c->store[IODINE_PUBSUB_MSG_STORE_filter] =
      (msg->filter ? INT2NUM(((int16_t)(msg->filter))) : Qnil);
  c->store[IODINE_PUBSUB_MSG_STORE_message] =
      (msg->message.len ? rb_str_new(msg->message.buf, msg->message.len)
                        : Qnil);
  c->store[IODINE_PUBSUB_MSG_STORE_published] =
      (msg->published ? ULL2NUM(msg->published) : Qnil);
  return m;
}

#define IODINE_DEF_GET_FUNC(val_name, ...)                                     \
  /** Returns the message's val_name */                                        \
  static VALUE iodine_pubsub_msg_##val_name##_get(VALUE self) {                \
    iodine_pubsub_msg_s *c = iodine_pubsub_msg_get(self);                      \
    if (!c)                                                                    \
      return Qnil;                                                             \
    return c->store[IODINE_PUBSUB_MSG_STORE_##val_name];                       \
  }

IODINE_DEF_GET_FUNC(id);
IODINE_DEF_GET_FUNC(channel);
IODINE_DEF_GET_FUNC(filter);
IODINE_DEF_GET_FUNC(message);
IODINE_DEF_GET_FUNC(published);

#undef IODINE_DEF_GET_FUNC

static void iodine_pubsub_msg_init(void) {
  IODINE_PUBSUB_MSG_KLASS =
      rb_define_class_under(iodine_rb_IODINE_BASE, "Message", rb_cObject);
  STORE.hold(IODINE_PUBSUB_MSG_KLASS);
  rb_define_alloc_func(IODINE_PUBSUB_MSG_KLASS, iodine_pubsub_msg_alloc);
  rb_define_method(IODINE_PUBSUB_MSG_KLASS, "id", iodine_pubsub_msg_id_get, 0);
  rb_define_method(IODINE_PUBSUB_MSG_KLASS,
                   "channel",
                   iodine_pubsub_msg_channel_get,
                   0);
  rb_define_method(IODINE_PUBSUB_MSG_KLASS,
                   "filter",
                   iodine_pubsub_msg_filter_get,
                   0);
  rb_define_method(IODINE_PUBSUB_MSG_KLASS,
                   "message",
                   iodine_pubsub_msg_message_get,
                   0);
  rb_define_method(IODINE_PUBSUB_MSG_KLASS,
                   "published",
                   iodine_pubsub_msg_published_get,
                   0);
}
