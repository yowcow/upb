/*
 * upb - a minimalist implementation of protocol buffers.
 *
 * Copyright (c) 2010 Joshua Haberman.  See LICENSE for details.
 *
 * Data structure for storing a message of protobuf data.
 */

#ifndef UPB_MSG_H
#define UPB_MSG_H

#include "upb.h"
#include "upb_def.h"
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

upb_value upb_field_tryrecycle(upb_valueptr p, upb_value v, upb_fielddef *f,
                               upb_valuetype_t type);

INLINE void _upb_value_ref(upb_value v) { upb_atomic_ref(v.refcount); }

void _upb_field_free(upb_value v, upb_fielddef *f);
void _upb_elem_free(upb_value v, upb_fielddef *f);
INLINE void _upb_field_unref(upb_value v, upb_fielddef *f) {
  assert(upb_field_ismm(f));
  if (v.refcount && upb_atomic_unref(v.refcount))
    _upb_field_free(v, f);
}
INLINE void _upb_elem_unref(upb_value v, upb_fielddef *f) {
  assert(upb_elem_ismm(f));
  if (v.refcount && upb_atomic_unref(v.refcount))
    _upb_elem_free(v, f);
}

/* upb_array ******************************************************************/

typedef uint32_t upb_arraylen_t;
struct _upb_array {
  upb_atomic_refcount_t refcount;
  upb_arraylen_t len;
  upb_arraylen_t size;
  upb_valueptr elements;
};

void _upb_array_free(upb_array *a, upb_fielddef *f);
INLINE upb_valueptr _upb_array_getptr(upb_array *a, upb_fielddef *f,
                                      uint32_t elem) {
  upb_valueptr p;
  p._void = &a->elements.uint8[elem * upb_types[f->type].size];
  return p;
}

upb_array *upb_array_new(void);

INLINE void upb_array_unref(upb_array *a, upb_fielddef *f) {
  if (upb_atomic_unref(&a->refcount)) _upb_array_free(a, f);
}

INLINE uint32_t upb_array_len(upb_array *a) {
  return a->len;
}

INLINE upb_value upb_array_get(upb_array *a, upb_fielddef *f, uint32_t elem) {
  assert(elem < upb_array_len(a));
  return upb_value_read(_upb_array_getptr(a, f, elem), f->type);
}

// For string or submessages, will release a ref on the previously set value.
// and take a ref on the new value.  The array must already be at least "elem"
// long; to append use append_mutable.
INLINE void upb_array_set(upb_array *a, upb_fielddef *f, uint32_t elem,
                          upb_value val) {
  assert(elem < upb_array_len(a));
  upb_valueptr p = _upb_array_getptr(a, f, elem);
  if (upb_elem_ismm(f)) {
    _upb_elem_unref(upb_value_read(p, f->type), f);
    _upb_value_ref(val);
  }
  upb_value_write(p, val, f->type);
}

INLINE void upb_array_resize(upb_array *a, upb_fielddef *f) {
  if (a->len == a->size) {
    a->len *= 2;
    a->elements._void = realloc(a->elements._void,
                                a->len * upb_types[f->type].size);
  }
}

// Append an element to an array of string or submsg with the default value,
// returning it.  This will try to reuse previously allocated memory.
INLINE upb_value upb_array_appendmutable(upb_array *a, upb_fielddef *f) {

  assert(upb_elem_ismm(f));
  upb_array_resize(a, f);
  upb_valueptr p = _upb_array_getptr(a, f, a->len++);
  upb_valuetype_t type = upb_elem_valuetype(f);
  upb_value val = upb_value_read(p, type);
  val = upb_field_tryrecycle(p, val, f, type);
  return val;
}


/* upb_msg ********************************************************************/

struct _upb_msg {
  upb_atomic_refcount_t refcount;
  uint8_t data[4];  // We allocate the appropriate amount per message.
};

// INTERNAL-ONLY FUNCTIONS.

void _upb_msg_free(upb_msg *msg, upb_msgdef *md);

// Returns a pointer to the given field.
INLINE upb_valueptr _upb_msg_getptr(upb_msg *msg, upb_fielddef *f) {
  upb_valueptr p;
  p._void = &msg->data[f->byte_offset];
  return p;
}

// PUBLIC FUNCTIONS.

// Creates a new msg of the given type.
upb_msg *upb_msg_new(upb_msgdef *md);

// Unrefs the given message.
INLINE void upb_msg_unref(upb_msg *msg, upb_msgdef *md) {
  if (msg && upb_atomic_unref(&msg->refcount)) _upb_msg_free(msg, md);
}

// Tests whether the given field is explicitly set, or whether it will return a
// default.
INLINE bool upb_msg_has(upb_msg *msg, upb_fielddef *f) {
  return (msg->data[f->field_index/8] & (1 << (f->field_index % 8))) != 0;
}

// Unsets all field values back to their defaults.
INLINE void upb_msg_clear(upb_msg *msg, upb_msgdef *md) {
  memset(msg->data, 0, md->set_flags_bytes);
}

// Used to obtain an empty message of the given type, attempting to reuse the
// memory pointed to by msg if it has no other referents.
void upb_msg_recycle(upb_msg **_msg, upb_msgdef *md);

// For a repeated field, appends the given scalar value (ie. not a message or
// array) to the field's array; for non-repeated fields, overwrites the
// existing value with this one.
// REQUIRES: !upb_issubmsg(f)
void upb_msg_appendval(upb_msg *msg, upb_fielddef *f, upb_value val);

upb_msg *upb_msg_append_emptymsg(upb_msg *msg, upb_fielddef *f);

// Returns the current value of the given field if set, or the default value if
// not set.  The returned value is not mutable!  (In practice this only matters
// for submessages and arrays).
INLINE upb_value upb_msg_get(upb_msg *msg, upb_fielddef *f) {
  if (upb_msg_has(msg, f)) {
    return upb_value_read(_upb_msg_getptr(msg, f), f->type);
  } else {
    return f->default_value;
  }
}

// If the given string, submessage, or array is already set, returns it.
// Otherwise sets it and returns an empty instance, attempting to reuse any
// previously allocated memory.
INLINE upb_value upb_msg_getmutable(upb_msg *msg, upb_fielddef *f);

// Sets the current value of the field.  If this is a string, array, or
// submessage field, releases a ref on the value (if any) that was previously
// set.
INLINE void upb_msg_set(upb_msg *msg, upb_fielddef *f, upb_value val);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif