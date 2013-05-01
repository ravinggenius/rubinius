/* The simple ByteArray class, used to implement String. */

#include <stdint.h>

#include "vm.hpp"
#include "objectmemory.hpp"
#include "primitives.hpp"
#include "builtin/bytearray.hpp"
#include "builtin/class.hpp"
#include "builtin/exception.hpp"
#include "builtin/fixnum.hpp"
#include "builtin/string.hpp"
#include "builtin/tuple.hpp"

#include "object_utils.hpp"

#include "ontology.hpp"

namespace rubinius {

  void ByteArray::init(STATE) {
    GO(bytearray).set(ontology::new_class_under(state,
                        "ByteArray", G(rubinius)));
    G(bytearray)->set_object_type(state, ByteArrayType);
  }

  ByteArray* ByteArray::create(STATE, native_int bytes) {
    if(bytes < 0) {
      rubinius::bug("Invalid byte array size");
    }

    size_t body = bytes;
    ByteArray* ba = state->vm()->new_object_bytes_dirty<ByteArray>(G(bytearray), body);

    if(unlikely(!ba)) {
      Exception::memory_error(state);
    } else {
      ba->full_size_ = body;
      memset(ba->bytes, 0, body - sizeof(ByteArray));
    }
    return ba;
  }

  ByteArray* ByteArray::create_pinned(STATE, native_int bytes) {
    if(bytes < 0) {
      rubinius::bug("Invalid byte array size");
    }

    size_t body = bytes;
    ByteArray* ba = state->memory()->new_object_bytes_mature_dirty<ByteArray>(state, G(bytearray), body);
    if(unlikely(!ba)) {
      Exception::memory_error(state);
      return NULL;
    }

    if(!ba->pin()) {
      rubinius::bug("unable to allocate pinned ByteArray");
    } else {
      ba->full_size_ = body;
      memset(ba->bytes, 0, body - sizeof(ByteArray));
    }
    return ba;
  }

  ByteArray* ByteArray::create_dirty(STATE, native_int bytes) {
    if(bytes < 0) {
      rubinius::bug("Invalid byte array size");
    }

    size_t body = bytes;
    ByteArray* ba = state->vm()->new_object_bytes_dirty<ByteArray>(G(bytearray), body);

    if(unlikely(!ba)) {
      Exception::memory_error(state);
    } else {
      ba->full_size_ = body;
      // Ensure to zero the last bytes that might be more
      // because we always pad the size to a machine word.
      // The caller is responsible to fill either the
      // requested bytes or clear it all if it needs to
      if(bytes > 0) {
        size_t last = bytes_to_fields(body);
        ba->pointer_to_body()[last - 1] = 0;
      }
    }
    return ba;
  }

  ByteArray* ByteArray::allocate(STATE, Fixnum* bytes) {
    native_int size = bytes->to_native();
    if(size < 0) {
      Exception::argument_error(state, "negative byte array size");
    }
    return ByteArray::create(state, size);
  }

  Fixnum* ByteArray::size(STATE) {
    return Fixnum::from(size());
  }

  char* ByteArray::to_chars(STATE, Fixnum* size) {
    native_int sz = size->to_native();
    native_int ba_sz = this->size(state)->to_native();

    if(sz < 0) {
      Exception::object_bounds_exceeded_error(state, "size less than zero");
    } else if(sz > ba_sz) {
      Exception::object_bounds_exceeded_error(state, "size beyond actual size");
    }

    char* str = (char*)(this->bytes);
    char* out = ALLOC_N(char, sz + 1);

    memcpy(out, str, sz);
    out[sz] = 0;

    return out;
  }

  Fixnum* ByteArray::get_byte(STATE, Fixnum* index) {
    native_int idx = index->to_native();

    if(idx < 0 || idx >= size()) {
      Exception::object_bounds_exceeded_error(state, "index out of bounds");
    }

    return Fixnum::from(this->bytes[idx]);
  }

  Fixnum* ByteArray::set_byte(STATE, Fixnum* index, Fixnum* value) {
    native_int idx = index->to_native();

    if(idx < 0 || idx >= size()) {
      Exception::object_bounds_exceeded_error(state, "index out of bounds");
    }

    this->bytes[idx] = value->to_native();
    return Fixnum::from(this->bytes[idx]);
  }

  Fixnum* ByteArray::move_bytes(STATE, Fixnum* start, Fixnum* count, Fixnum* dest) {
    native_int src = start->to_native();
    native_int cnt = count->to_native();
    native_int dst = dest->to_native();

    if(src < 0) {
      Exception::object_bounds_exceeded_error(state, "start less than zero");
    } else if(dst < 0) {
      Exception::object_bounds_exceeded_error(state, "dest less than zero");
    } else if(cnt < 0) {
      Exception::object_bounds_exceeded_error(state, "count less than zero");
    } else if((dst + cnt) > size()) {
      Exception::object_bounds_exceeded_error(state, "move is beyond end of bytearray");
    } else if((src + cnt) > size()) {
      Exception::object_bounds_exceeded_error(state, "move is more than available bytes");
    }

    memmove(this->bytes + dst, this->bytes + src, cnt);

    return count;
  }

  ByteArray* ByteArray::fetch_bytes(STATE, Fixnum* start, Fixnum* count) {
    native_int src = start->to_native();
    native_int cnt = count->to_native();

    if(src < 0) {
      Exception::object_bounds_exceeded_error(state, "start less than zero");
    } else if(cnt < 0) {
      Exception::object_bounds_exceeded_error(state, "count less than zero");
    } else if((src + cnt) > size()) {
      Exception::object_bounds_exceeded_error(state, "fetch is more than available bytes");
    }

    ByteArray* ba = ByteArray::create_dirty(state, cnt + 1);
    memcpy(ba->bytes, this->bytes + src, cnt);
    ba->bytes[cnt] = 0;

    return ba;
  }

  ByteArray* ByteArray::prepend(STATE, String* str) {
    ByteArray* ba = ByteArray::create(state, size() + str->byte_size());

    memcpy(ba->bytes, str->data()->bytes, str->byte_size());
    memcpy(ba->bytes + str->byte_size(), bytes, size());

    return ba;
  }

  ByteArray* ByteArray::reverse(STATE, Fixnum* o_start, Fixnum* o_total) {
    native_int start = o_start->to_native();
    native_int total = o_total->to_native();

    if(total <= 0 || start < 0 || start >= size()) return this;

    uint8_t* pos1 = this->bytes + start;
    uint8_t* pos2 = this->bytes + total - 1;
    register uint8_t tmp;

    while(pos1 < pos2) {
      tmp = *pos1;
      *pos1++ = *pos2;
      *pos2-- = tmp;
    }

    return this;
  }

  Fixnum* ByteArray::compare_bytes(STATE, ByteArray* other, Fixnum* a, Fixnum* b) {
    native_int slim = a->to_native();
    native_int olim = b->to_native();

    if(slim < 0) {
      Exception::object_bounds_exceeded_error(state,
          "bytes of self to compare is less than zero");
    } else if(olim < 0) {
      Exception::object_bounds_exceeded_error(state,
          "bytes of other to compare is less than zero");
    }

    // clamp limits to actual sizes
    native_int m = size() < slim ? size() : slim;
    native_int n = other->size() < olim ? other->size() : olim;

    // only compare the shortest string
    native_int len = m < n ? m : n;

    native_int cmp = memcmp(this->bytes, other->bytes, len);

    // even if substrings are equal, check actual requested limits
    // of comparison e.g. "xyz", "xyzZ"
    if(cmp == 0) {
      if(m < n) {
        return Fixnum::from(-1);
      } else if(m > n) {
        return Fixnum::from(1);
      } else {
        return Fixnum::from(0);
      }
    } else {
      return cmp < 0 ? Fixnum::from(-1) : Fixnum::from(1);
    }
  }

  Object* ByteArray::locate(STATE, String* pattern, Fixnum* start, Fixnum* max_o) {
    const uint8_t* pat = pattern->byte_address();
    native_int len = pattern->byte_size();
    native_int max = max_o->to_native();

    if(len == 0) return start;
    if(max == 0) return cNil;

    if(max > size()) max = size();
    max -= (len - 1);

    for(native_int i = start->to_native(); i < max; i++) {
      if(this->bytes[i] == pat[0]) {
        native_int j;
        // match the rest of the pattern string
        for(j = 1; j < len; j++) {
          if(this->bytes[i+j] != pat[j]) break;
        }

        // if the full pattern matched, return the index
        // of the end of the pattern in 'this'.
        if(j == len) return Fixnum::from(i + len);
      }
    }

    return cNil;
  }

  // Ripped from 1.8.7 and cleaned up

  static const uint32_t utf8_limits[] = {
    0x0,        /* 1 */
    0x80,       /* 2 */
    0x800,      /* 3 */
    0x10000,    /* 4 */
    0x200000,   /* 5 */
    0x4000000,  /* 6 */
    0x80000000, /* 7 */
  };

  static bool utf8_to_uv(char* p, uint32_t* uv, native_int* lenp) {
    uint32_t c = *p++ & 0xff;
    native_int n;

    *uv = c;

    if(!(*uv & 0x80)) {
      *lenp = 1;
      return true;
    }
    if(!(*uv & 0x40)) {
      *lenp = 1;
      return false;
    }

    if      (!(*uv & 0x20)) { n = 2; *uv &= 0x1f; }
    else if(!(*uv & 0x10)) { n = 3; *uv &= 0x0f; }
    else if(!(*uv & 0x08)) { n = 4; *uv &= 0x07; }
    else if(!(*uv & 0x04)) { n = 5; *uv &= 0x03; }
    else if(!(*uv & 0x02)) { n = 6; *uv &= 0x01; }
    else {
      *lenp = 1;
      return false;
    }
    if(n > *lenp) return false;

    *lenp = n--;
    if(n != 0) {
      while (n--) {
        c = *p++ & 0xff;
        if((c & 0xc0) != 0x80) {
          *lenp -= n + 1;
          return -1;
        }
        else {
          c &= 0x3f;
          *uv = *uv << 6 | c;
        }
      }
    }
    n = *lenp - 1;
    if(*uv < utf8_limits[n]) return false;
    return true;
  }

  Object* ByteArray::get_utf8_char(STATE, Fixnum* offset) {
    native_int o = offset->to_native();

    if(o >= (native_int)size()) return Primitives::failure();

    char* start = (char*)bytes + o;
    native_int len = size() - o;

    uint32_t uv;
    if(utf8_to_uv(start, &uv, &len)) {
      return Tuple::from(state, 2, Integer::from(state, uv), Fixnum::from(len));
    } else {
      return Primitives::failure();
    }
  }

  size_t ByteArray::Info::object_size(const ObjectHeader* obj) {
    const ByteArray *ba = static_cast<const ByteArray*>(obj);
    assert(ba);

    return ba->full_size_;
  }

  void ByteArray::Info::mark(Object* t, ObjectMark& mark) {
    // @todo implement
  }
}
