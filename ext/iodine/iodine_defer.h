#ifndef H_IODINE_DEFER_H
#define H_IODINE_DEFER_H

/**
 * Sets (or removes) a callback to be called before forking the process.
 *
 * If `active` is 1 the callback + argumet couplet are registered.
 *
 * If `active` is 0 the callback + argumet couplet are removed (if a match is
 * found).
 *
 * If `func` is NULL, the existing callbacks will be performed (as if forking).
 */
void iodine_before_fork(void (*func)(void *arg), void *arg,
                        unsigned char active);
/**
 * Sets (or removes) a callback to be called after forking the process. These
 * are called in both the parent and the child process.
 *
 * If `active` is 1 the callback + argumet couplet are registered.
 *
 * If `active` is 0 the callback + argumet couplet are removed (if a match is
 * found).
 *
 * If `func` is NULL, the existing callbacks will be performed (as if forking).
 */
void iodine_after_fork(void (*func)(void *arg), void *arg);
void iodine_defer_initialize(void);

#endif
