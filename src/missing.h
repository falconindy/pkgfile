#pragma once

#ifndef HAVE_MEMPCPY
static inline void *mempcpy(void *dst, const void *src, size_t len) {
  return (void *)(((char *)memcpy(dst, src, len)) + len);
}
#endif
