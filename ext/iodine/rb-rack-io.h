/*
copyright: Boaz segev, 2016
license: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#ifndef RUBY_RACK_IO_H
#define RUBY_RACK_IO_H
#include <ruby.h>
#include "http-request.h"

extern struct _RackIO_ {
  VALUE (*new)(struct HttpRequest* request, VALUE env);
  void (*init)(void);
} RackIO;

#endif /* RUBY_RACK_IO_H */
