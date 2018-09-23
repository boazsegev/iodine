#ifndef H_IODINE_PUBSUB_H
#define H_IODINE_PUBSUB_H

#include "iodine.h"

#include "fio.h"

/** Initializes the PubSub::Engine Ruby class. */
void iodine_pubsub_init(void);

extern const rb_data_type_t iodine_pubsub_data_type;

typedef struct {
  fio_pubsub_engine_s do_not_touch;
  VALUE handler;
  fio_pubsub_engine_s *engine;
  void (*dealloc)(fio_pubsub_engine_s *engine);
} iodine_pubsub_s;

static inline iodine_pubsub_s *iodine_pubsub_CData(VALUE obj) {
  iodine_pubsub_s *c = NULL;
  TypedData_Get_Struct(obj, iodine_pubsub_s, &iodine_pubsub_data_type, c);
  return c;
}

#endif
