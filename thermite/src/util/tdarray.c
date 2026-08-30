#include <stdio.h>
#include <string.h>

#include "tdarray.h"

#define HEADER_SIZE (_TDARRAY_FIELD_LENGTH * sizeof(size_t))

size_t* _tdarray_get_fields(void* darray) {
  if (darray == NULL) {
    return NULL;
  }
  return (size_t*)((char*)darray - HEADER_SIZE);
}

void* _tdarray_create(size_t stride) {
  void* raw = malloc(HEADER_SIZE + stride);
  if (raw == NULL) {
    return NULL;
  }
  void* r = (char*)raw + HEADER_SIZE;
  size_t* fields = _tdarray_get_fields(r);
  fields[_TDARRAY_LENGTH] = 0;
  fields[_TDARRAY_STRIDE] = stride;
  fields[_TDARRAY_CAPACITY] = 1;

  return r;
}

void _tdarray_insert(void** darray, size_t i, const void* value_ptr) {
  if (darray == NULL || *darray == NULL || value_ptr == NULL) {
    return;
  }
  size_t* fields = _tdarray_get_fields(*darray);
  if (i > fields[_TDARRAY_LENGTH]) {
    i = fields[_TDARRAY_LENGTH];
  }
  if (fields[_TDARRAY_LENGTH] >= fields[_TDARRAY_CAPACITY]) {
    size_t new_cap = fields[_TDARRAY_CAPACITY] * 2;
    void* beg = (char*)*darray - HEADER_SIZE;
    void* new_beg = realloc(beg, HEADER_SIZE + new_cap * fields[_TDARRAY_STRIDE]);
    if (new_beg == NULL) {
      return;
    }
    *darray = (char*)new_beg + HEADER_SIZE;
    fields = _tdarray_get_fields(*darray);
    fields[_TDARRAY_CAPACITY] = new_cap;
  }
  char* data = (char*)*darray;
  size_t stride = fields[_TDARRAY_STRIDE];
  memmove(data + (i + 1) * stride,
          data + i * stride,
          (fields[_TDARRAY_LENGTH] - i) * stride);
  fields[_TDARRAY_LENGTH]++;
  memcpy(data + i * stride, value_ptr, stride);
}

void _tdarray_push(void** darray, const void* value_ptr) {
  if (darray == NULL || *darray == NULL || value_ptr == NULL) {
    return;
  }
  size_t* fields = _tdarray_get_fields(*darray);

  if (fields[_TDARRAY_LENGTH] >= fields[_TDARRAY_CAPACITY]) {
    size_t new_cap = fields[_TDARRAY_CAPACITY] * 2;
    void* beg = (char*)*darray - HEADER_SIZE;
    void* new_beg = realloc(beg, HEADER_SIZE + new_cap * fields[_TDARRAY_STRIDE]);
    if (new_beg == NULL) {
      return;
    }
    *darray = (char*)new_beg + HEADER_SIZE;
    fields = _tdarray_get_fields(*darray);
    fields[_TDARRAY_CAPACITY] = new_cap;
  }

  char* data = (char*)*darray;
  size_t stride = fields[_TDARRAY_STRIDE];
  memcpy(data + fields[_TDARRAY_LENGTH] * stride,
         value_ptr, stride);
  fields[_TDARRAY_LENGTH]++;
}

void _tdarray_pop(void* darray) {
  if (darray == NULL) {
    return;
  }
  size_t* fields = _tdarray_get_fields(darray);
  if (fields[_TDARRAY_LENGTH] > 0) {
    fields[_TDARRAY_LENGTH]--;
  }
}

void _tdarray_remove(void* darray, size_t i) {
  if (darray == NULL) {
    return;
  }
  size_t* fields = _tdarray_get_fields(darray);
  if (i >= fields[_TDARRAY_LENGTH]) {
    return;
  }
  fields[_TDARRAY_LENGTH]--;
  char* data = (char*)darray;
  size_t stride = fields[_TDARRAY_STRIDE];
  memmove(data + i * stride,
          data + (i + 1) * stride,
          (fields[_TDARRAY_LENGTH] - i) * stride);
}

size_t _tdarray_length(void* darray) {
  if (darray == NULL) {
    return 0;
  }
  return _tdarray_get_fields(darray)[_TDARRAY_LENGTH];
}

size_t _tdarray_memory(void* darray) {
  if (darray == NULL) {
    return 0;
  }
  size_t* fields = _tdarray_get_fields(darray);
  return fields[_TDARRAY_CAPACITY] * fields[_TDARRAY_STRIDE];
}

int _tdarray_find(void* darray, const void* value_ptr) {
  if (darray == NULL || value_ptr == NULL) {
    return -1;
  }
  size_t* fields = _tdarray_get_fields(darray);
  char* data = (char*)darray;
  size_t stride = fields[_TDARRAY_STRIDE];
  for (size_t i = 0; i < fields[_TDARRAY_LENGTH]; i++) {
    if (memcmp(data + i * stride, value_ptr, stride) == 0) {
      return (int)i;
    }
  }
  return -1;
}

void _tdarray_clear(void* darray) {
  if (darray == NULL) {
    return;
  }
  size_t* fields = _tdarray_get_fields(darray);
  fields[_TDARRAY_LENGTH] = 0;
}

void _tdarray_destroy(void* darray) {
  if (darray == NULL) {
    return;
  }
  free((char*)darray - HEADER_SIZE);
}
