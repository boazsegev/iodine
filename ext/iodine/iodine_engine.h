#ifndef H_IODINE_ENGINE_H
#define H_IODINE_ENGINE_H
/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#include "iodine.h"

extern VALUE IodineEngine;

void Iodine_init_engine(void);

typedef struct pubsub_engine_s pubsub_engine_s;

pubsub_engine_s *iodine_engine_ruby2facil(VALUE engine);

#endif
