/*
** ritehash.c - Rite Hash for mruby
**
** See Copyright Notice in mruby.h
*/

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t khint_t;
typedef khint_t khiter_t;

#define INITIAL_HASH_SIZE 32
#define UPPER_BOUND(x) ((x)>>2|(x>>1))

//extern uint8_t __m[];

/* mask for flags */
static uint8_t __m[8] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};


#define __ac_isempty(e_flag, d_flag, i) (e_flag[(i)/8]&__m[(i)%8])
#define __ac_isdel(e_flag, d_flag, i) (d_flag[(i)/8]&__m[(i)%8])
#define __ac_iseither(e_flag, d_flag, i) (__ac_isempty(e_flag,d_flag,i)||__ac_isdel(e_flag,d_flag,i))


/* struct kh_xxx

   name: ash name
   khkey_t: key data type
   khval_t: value data type
   kh_is_map: (not implemented / not used in RiteVM )
   __hash_func: hash function
   __hash_equal: hash comparation function
*/
#define KHASH_INIT(name, khkey_t, khval_t, kh_is_map, __hash_func, __hash_equal) \
  typedef struct kh_##name {                                            \
    khint_t n_buckets;                                                  \
    khint_t size;                                                       \
    khint_t n_occupied;                                                 \
    khint_t upper_bound;                                                \
    uint8_t *e_flags;                                                   \
    uint8_t *d_flags;                                                   \
    khkey_t *keys;                                                      \
    khval_t *vals;                                                      \
    khint_t mask;                                                       \
    khint_t inc;                                                        \
    mrb_state *mrb;                                                     \
  } kh_##name##_t;                                                      \
  static void kh_alloc_##name(kh_##name##_t *h)                         \
  {                                                                     \
    khint_t sz = h->n_buckets;                                          \
    h->size = h->n_occupied = 0;                                        \
    h->upper_bound = UPPER_BOUND(sz);                                   \
    h->e_flags = (uint8_t *)mrb_malloc(h->mrb, sizeof(uint8_t)*sz/4);   \
    h->d_flags = h->e_flags + sz/8;                                     \
    memset(h->e_flags, 0xff, sz/8*sizeof(uint8_t));                     \
    memset(h->d_flags, 0x00, sz/8*sizeof(uint8_t));                     \
    h->keys = (khkey_t *)mrb_malloc(h->mrb, sizeof(khkey_t)*sz);        \
    h->vals = (khval_t *)mrb_malloc(h->mrb, sizeof(khval_t)*sz);        \
    h->mask = sz-1;                                                     \
    h->inc = sz/2-1;                                                    \
  }                                                                     \
  static inline kh_##name##_t *kh_init_##name(mrb_state *mrb){          \
    kh_##name##_t *h = (kh_##name##_t*)mrb_calloc(mrb, 1, sizeof(kh_##name##_t)); \
    h->n_buckets = INITIAL_HASH_SIZE;                                   \
    h->mrb = mrb;                                                       \
    kh_alloc_##name(h);                                                 \
    return h;                                                           \
  }                                                                     \
  static inline void kh_destroy_##name(kh_##name##_t *h)                \
  {                                                                     \
    if( h ){                                                            \
      mrb_free(h->mrb, h->keys);                                        \
      mrb_free(h->mrb, h->vals);                                        \
      mrb_free(h->mrb, h->e_flags);                                     \
      mrb_free(h->mrb, h);                                              \
    }                                                                   \
  }                                                                     \
  static inline void kh_clear_##name(kh_##name##_t *h)                  \
  {                                                                     \
    if( h && h->e_flags ){                                              \
      memset(h->e_flags, 0xff, h->n_buckets/8*sizeof(uint8_t));         \
      memset(h->d_flags, 0x00, h->n_buckets/8*sizeof(uint8_t));         \
      h->size = h->n_occupied = 0;                                      \
    }                                                                   \
  }                                                                     \
  static inline khint_t kh_get_##name(kh_##name##_t *h, khkey_t key)    \
  {                                                                     \
    khint_t k = __hash_func(h->mrb,key) & (h->mask);                    \
    while( !__ac_isempty(h->e_flags, h->d_flags, k) ){                  \
      if( !__ac_isdel(h->e_flags, h->d_flags, k) ){                     \
        if( __hash_equal(h->mrb,h->keys[k], key) ) return k;            \
      }                                                                 \
      k = (k+h->inc) & (h->mask);                                       \
    }                                                                   \
    return h->n_buckets;                                                \
  }                                                                     \
  static inline khint_t kh_put_##name(kh_##name##_t *h, khkey_t key);   \
  static void kh_resize_##name(kh_##name##_t *h, khint_t new_n_buckets) \
  {                                                                     \
    if( new_n_buckets<INITIAL_HASH_SIZE ){                              \
      new_n_buckets = INITIAL_HASH_SIZE;                                \
    } else {                                                            \
      khint_t limit = new_n_buckets;                                    \
      new_n_buckets = INITIAL_HASH_SIZE;                                \
      while( new_n_buckets < limit ) new_n_buckets *= 2;                \
    }                                                                   \
    uint8_t *old_e_flags = h->e_flags;                                  \
    khkey_t *old_keys = h->keys;                                        \
    khval_t *old_vals = h->vals;                                        \
    khint_t old_n_buckets = h->n_buckets;                               \
    h->n_buckets = new_n_buckets;                                       \
    kh_alloc_##name(h);                                                 \
    /* relocate */                                                      \
    khint_t i;                                                          \
    for( i=0 ; i<old_n_buckets ; i++ ){                                 \
      if( !__ac_isempty(old_e_flags, old_d_flags, i) ){                 \
        khint_t k = kh_put_##name(h, old_keys[i]);                      \
        kh_value(h,k) = old_vals[i];                                    \
      }                                                                 \
    }                                                                   \
  }                                                                     \
  static inline khint_t kh_put_##name(kh_##name##_t *h, khkey_t key)    \
  {                                                                     \
    khint_t k;                                                          \
    if( h->n_occupied >= h->upper_bound ){                              \
      kh_resize_##name(h, h->n_buckets*2);                              \
    }                                                                   \
    k = __hash_func(h->mrb,key) & (h->mask);                            \
    while( !__ac_iseither(h->e_flags, h->d_flags, k) ){                 \
      if( __hash_equal(h->mrb,h->keys[k], key) ) break;                 \
      k = (k+h->inc) & (h->mask);                                       \
    }                                                                   \
    if( __ac_isempty(h->e_flags, h->d_flags, k) ) {                     \
      /* put at empty */                                                \
      h->keys[k] = key;                                                 \
      h->e_flags[k/8] &= ~__m[k%8];                                     \
      h->size++;                                                        \
      h->n_occupied++;                                                  \
    } else if( __ac_isdel(h->e_flags, h->d_flags, k) ) {                \
      /* put at del */                                                  \
      h->keys[k] = key;                                                 \
      h->d_flags[k/8] &= ~__m[k%8];                                     \
      h->size++;                                                        \
    }                                                                   \
    return k;                                                           \
  }                                                                     \
  static inline void kh_del_##name(kh_##name##_t *h, khint_t x)         \
  {                                                                     \
    h->d_flags[x/8] |= __m[x%8];                                        \
    h->size--;                                                          \
  }                                                                     \
  static inline void kh_debug_##name(kh_##name##_t *h)                  \
  {                                                                     \
    khint_t i;                                                          \
    printf("idx:e_flag:d_flag\n");                                      \
    for( i=0 ; i<h->n_buckets/8 ; i++ ){                                \
      printf("%4d:%02X:%02X\n", i, h->e_flags[i], h->d_flags[i]);       \
    }                                                                   \
  }                                                                     \

#define khash_t(name) kh_##name##_t

#define kh_init(name,mrb) kh_init_##name(mrb)
#define kh_destroy(name, h) kh_destroy_##name(h)
#define kh_clear(name, h) kh_clear_##name(h)
#define kh_resize(name, h, s) kh_resize_##name(h, s)
#define kh_put(name, h, k) kh_put_##name(h, k)
#define kh_get(name, h, k) kh_get_##name(h, k)
#define kh_del(name, h, k) kh_del_##name(h, k)
#define kh_debug(name, h) kh_debug_##name(h)

#define kh_exist(h, x) (!__ac_iseither((h)->e_flags, (h)->d_flags, (x)))
#define kh_key(h, x) ((h)->keys[x])
#define kh_val(h, x) ((h)->vals[x])
#define kh_value(h, x) ((h)->vals[x])
#define kh_begin(h) (khint_t)(0)
#define kh_end(h) ((h)->n_buckets)
#define kh_size(h) ((h)->size)
#define kh_n_buckets(h) ((h)->n_buckets)

//#define kh_int_hash_func(mrb,key) (uint32_t)(key)
#define kh_int_hash_func(mrb,key) (uint32_t)((key)^((key)<<2)^((key)>>2))
#define kh_int_hash_equal(mrb,a, b) (a == b)
#define kh_int64_hash_func(mrb,key) (uint32_t)((key)>>33^(key)^(key)<<11)
#define kh_int64_hash_equal(mrb,a, b) (a == b)
static inline khint_t __ac_X31_hash_string(const char *s)
{
    khint_t h = *s;
    if (h) for (++s ; *s; ++s) h = (h << 5) - h + *s;
    return h;
}
#define kh_str_hash_func(mrb,key) __ac_X31_hash_string(key)
#define kh_str_hash_equal(mrb,a, b) (strcmp(a, b) == 0)

#define KHASH_MAP_INIT_INT(name, khval_t)                               \
    KHASH_INIT(name, uint32_t, khval_t, 1, kh_int_hash_func, kh_int_hash_equal)
typedef const char *kh_cstr_t;
#define KHASH_MAP_INIT_STR(name, khval_t)                               \
    KHASH_INIT(name, kh_cstr_t, khval_t, 1, kh_str_hash_func, kh_str_hash_equal)

