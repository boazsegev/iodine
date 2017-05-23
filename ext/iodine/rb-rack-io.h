/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#ifndef RUBY_RACK_IO_H
#define RUBY_RACK_IO_H
// clang-format off
#include <ruby.h>
// clang-format on

#include "http_request.h"

extern struct _RackIO_ {
  VALUE (*create)(http_request_s *request, VALUE env);
  void (*init)(void);
} RackIO;

#endif /* RUBY_RACK_IO_H */
