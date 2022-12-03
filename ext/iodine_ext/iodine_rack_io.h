/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#ifndef RUBY_RACK_IO_H
#define RUBY_RACK_IO_H

#include <ruby.h>

#include "http.h"

extern struct IodineRackIO {
  VALUE (*create)(http_s *h, VALUE env);
  void (*close)(VALUE rack_io);
  void (*init)(void);
} IodineRackIO;

#endif /* RUBY_RACK_IO_H */
