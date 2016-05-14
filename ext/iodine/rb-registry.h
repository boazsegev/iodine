/*
copyright: Boaz segev, 2016
license: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#ifndef RB_REGISTRY_HELPER_H
#define RB_REGISTRY_HELPER_H
#include <ruby.h>

/**
This is a "Registry" helper for Ruby extentions.

The registry allows "registering" Ruby objects to be marked by the Ruby's GC.
This prevents the need for global variables such as Ruby arrays or Hashes
with Ruby objects and allows easy management of Ruby objects owned by C code.

GC requires a callback "mark" to inform it which objects are still
referenced.

Our library creates, holds and releases myriad Ruby objects.

Hence, our server class needs to include an object binary tree to
handle ruby registry and keep references to the Ruby objects.
*/
extern struct ___RegistryClass___ {
  void (*init)(VALUE owner);
  void (*remove)(VALUE obj);
  VALUE (*add)(VALUE obj);
  void (*print)(void);
} Registry;

#endif  // RB_REGISTRY_HELPER_H
