#ifndef RUBY_RACK_IO_H
#define RUBY_RACK_IO_H
#include <ruby.h>
#include "http-request.h"

extern struct _RackIO_ {
  VALUE (*new)(struct HttpRequest* request);
  void (*init)(VALUE owner);
} RackIO;

#endif  // RUBY_RACK_IO_H
