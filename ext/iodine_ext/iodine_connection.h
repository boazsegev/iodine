#ifndef H_IODINE_CONNECTION_H
#define H_IODINE_CONNECTION_H

#include "iodine.h"

typedef enum {
  IODINE_CONNECTION_RAW,
  IODINE_CONNECTION_WEBSOCKET,
  IODINE_CONNECTION_SSE
} iodine_connection_type_e;

typedef struct {
  iodine_connection_type_e type;
  intptr_t uuid;
  void *arg; /* holds the connection pointer (ws_s / sse_s) */
  VALUE handler;
  VALUE env;
} iodine_connection_s;

/**
 * Creates a new connection object.
 */
VALUE iodine_connection_new(iodine_connection_s args);
#define iodine_connection_new(...)                                             \
  iodine_connection_new((iodine_connection_s){__VA_ARGS__})

typedef enum {
  IODINE_CONNECTION_ON_OPEN,
  IODINE_CONNECTION_ON_MESSAGE,
  IODINE_CONNECTION_ON_DRAINED,
  IODINE_CONNECTION_PING,
  IODINE_CONNECTION_ON_SHUTDOWN,
  IODINE_CONNECTION_ON_CLOSE
} iodine_connection_event_type_e;

/**
 * Fires a connection object's event. `data` is only for the on_message event.
 */
void iodine_connection_fire_event(VALUE connection,
                                  iodine_connection_event_type_e ev,
                                  VALUE data);

/** Initializes the Connection Ruby class. */
void iodine_connection_init(void);

extern const rb_data_type_t iodine_connection_data_type;

static inline iodine_connection_s *iodine_connection_CData(VALUE self) {
  iodine_connection_s *c = NULL;
  TypedData_Get_Struct(self, iodine_connection_s, &iodine_connection_data_type,
                       c);
  return c;
}

#endif
