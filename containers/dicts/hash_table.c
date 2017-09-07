// An implementation of a open addressing hash table data structure with linear probing
// Copyright (C) 2017 Marcus Pinnecke
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
// warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with this program.
// If not, see <http://www.gnu.org/licenses/>.

// ---------------------------------------------------------------------------------------------------------------------
// I N C L U D E S
// ---------------------------------------------------------------------------------------------------------------------

#include <msg.h>
#include <containers/dicts/hash_table.h>

// ---------------------------------------------------------------------------------------------------------------------
// M A C R O S
// ---------------------------------------------------------------------------------------------------------------------

#define seek_to_slot(base, id, key_size, elem_size)                                                                    \
    (base + id * (key_size + elem_size))

#define bytewise_equals(size, lhs, rhs)                                                                                \
    ({                                                                                                                 \
        bool equals = true;                                                                                            \
        for (size_t byte_idx = 0; byte_idx < size; byte_idx++) {                                                       \
            equals &= (*(char *)(lhs + byte_idx) == *(char *)(rhs + byte_idx));                                        \
        }                                                                                                              \
        equals;                                                                                                        \
    })

bool is_empty_key(const char *a) {
    return (a == NULL || strlen(a) == 0);
}

bool key_comp(const char *a, const char *b) {
    return (strcmp(a, b) == 0);
}

#define keys_match(self, extra, lhs, rhs)                                                                              \
    (extra->key_is_str ? key_comp(*(char **)lhs, *(char **)rhs) : bytewise_equals(self->key_size, lhs, rhs))

#define is_slot_empty(self, extra, slot_data, empty_indicator)                                                         \
    (extra->key_is_str ? is_empty_key(*(char **)slot_data) : bytewise_equals(self->key_size, slot_data, empty_indicator))

#define require_linear_hash_table_tag(dict)                                                                            \
    require((dict->tag == dict_type_linear_hash_table), BADTAG);

#define require_instanceof_this(dict)                                                                                  \
    { require_nonnull(dict); require_nonnull(dict->extra); require_linear_hash_table_tag(dict); }

#define calc_hash_code(self, extra, key)                                                                               \
    (extra->hash_function.hash_code(extra->hash_function.capture, extra->key_is_str ? strlen(*((char **) key)) : self->key_size,    \
                                                                  extra->key_is_str ? *((char **) key) : key))

#define convert_to_slot_id(hash_code, extra)                                                                           \
    (hash_code % extra->num_slots)

#define calc_round_trip(slot_id, extra)                                                                                \
    (slot_id == 0 ? (extra->num_slots - 1) : (convert_to_slot_id((slot_id - 1), extra)))

#define test_slot(self, extra, slot_id)                                                                                \
    ({                                                                                                                 \
        bool empty = slot_is_empty(self, extra, slot_id);                                                              \
        extra->counters.num_test_slot++;                                                                               \
        empty;                                                                                                         \
    })

#define get_key(self, extra, slot_id)                                                                                  \
    ({                                                                                                                 \
        void *key = slot_get_key(extra->slots, slot_id, self->key_size, self->elem_size);                              \
        extra->counters.num_slot_get_key++;                                                                            \
        key;                                                                                                           \
    })

#define get_value(self, extra, slot_id)                                                                                \
    ({                                                                                                                 \
        void *value = slot_get_value(extra->slots, slot_id, self->key_size, self->elem_size);                          \
        extra->counters.num_slot_get_value++;                                                                          \
        value;                                                                                                         \
    })

#define remove_key(self, extra, slot_id)                                                                               \
    ({                                                                                                                 \
        slot_remove(extra->slots, slot_id, self->key_size, self->elem_size, extra->empty_indicator);                   \
        extra->counters.num_remove_key++;                                                                              \
    })

#define shallow_cpy(other)                                                                                             \
    ({(hash_table_extra_t) {                                                                                           \
        .mutex = (other)->mutex,                                                                                       \
        .num_inuse = (other)->num_inuse,                                                                               \
        .empty_indicator = (other)->empty_indicator,                                                                   \
        .grow_factor = (other)->grow_factor,                                                                           \
        .hash_function = (other)->hash_function,                                                                       \
        .num_slots = (other)->num_slots,                                                                               \
        .slots = (other)->slots,                                                                                       \
        .max_load_factor = (other)->max_load_factor,                                                                   \
        .counters = (other)->counters,                                                                                 \
        .equals = (other)->equals,                                                                                     \
        .cleanup = (other)->cleanup,                                                                                   \
        .key_is_str = (other)->key_is_str,                                                                             \
        .keyset = (other)->keyset                                                                                      \
    };})

// ---------------------------------------------------------------------------------------------------------------------
// T Y P E S
// ---------------------------------------------------------------------------------------------------------------------

typedef struct {
    hash_function_t hash_function;
    void *slots;
    size_t num_slots, num_inuse;
    float grow_factor;
    float max_load_factor;
    void *empty_indicator;
    pthread_mutex_t mutex;
    counters_t counters;
    bool (*equals)(const void *key_lhs, const void *key_rhs);
    void (*cleanup)(void *key, void *value);
    bool key_is_str;
    struct vector_t *keyset;
} hash_table_extra_t;

// ---------------------------------------------------------------------------------------------------------------------
// H E L P E R   P R O T O T Y P E S
// ---------------------------------------------------------------------------------------------------------------------

void this_clear(struct dict_t *self);
bool this_empty(const struct dict_t *self);
bool this_contains_key(const struct dict_t *self, const void *key);
const struct vector_t *this_keyset(const struct dict_t *self);
const void *this_get(const struct dict_t *self, const void *key);
struct vector_t *this_gets(const struct dict_t *self, size_t num_keys, const void *keys);
bool this_remove(struct dict_t *self, size_t num_keys, const void *keys);
void this_put(struct dict_t *self, const void *key, const void *value);
void this_puts(struct dict_t *self, size_t num_elements, const void *keys, const void *values);
size_t this_num_elements(struct dict_t *self);
void this_for_each(struct dict_t *self, void *capture, void (*consumer)(void *capture, const void *key, const void *value));

static inline void init_slots(void *slots, size_t num_slots, size_t key_size, size_t elem_size,
                              void *empty_indicator);
static inline bool slot_is_empty(const dict_t *self, hash_table_extra_t *extra, size_t slot_id);
static inline void slot_put(void *slots, size_t slot_id, size_t key_size, size_t elem_size, const void *key, const void *data);
static inline void *slot_get_key(void *slots, size_t slot_id, size_t key_size, size_t elem_size);
static inline void *slot_get_value(void *slots, size_t slot_id, size_t key_size, size_t elem_size);
static inline void slot_remove(void *slots, size_t slot_id, size_t key_size, size_t elem_size, void *empty_indicator);


static inline void rebuild(const dict_t *self, hash_table_extra_t *extra);
static inline void swap(hash_table_extra_t *a, hash_table_extra_t *b);

// ---------------------------------------------------------------------------------------------------------------------
// I N T E R F A C E  I M P L E M E N T A T I O N
// ---------------------------------------------------------------------------------------------------------------------

dict_t *hash_table_create(const hash_function_t *hash_function, size_t key_size, size_t elem_size,
                          size_t num_slots, float grow_factor, float max_load_factor)
{
    return hash_table_create_ex(hash_function, key_size, elem_size, num_slots, num_slots,
                                grow_factor, max_load_factor, NULL, NULL, false);
}

dict_t *hash_table_create_jenkins(size_t key_size, size_t elem_size, size_t num_slots, float grow_factor,
                                  float max_load_factor)
{
    return hash_table_create(&(hash_function_t) {.capture = NULL, .hash_code = hash_code_jen}, key_size, elem_size,
                             num_slots, grow_factor, max_load_factor);
}

dict_t *hash_table_create_ex(const hash_function_t *hash_function, size_t key_size, size_t elem_size,
                          size_t num_slots, size_t approx_num_keys, float grow_factor, float max_load_factor,
                          bool (*equals)(const void *key_lhs, const void *key_rhs),
                          void (*cleanup)(void *key, void *value), bool key_is_str)
{
    require_nonnull(hash_function);
    require_nonnull(hash_function->hash_code);
    require_not_zero(num_slots);

    dict_t *result = require_good_malloc(sizeof(dict_t));
    *result = (dict_t) {
        .tag = dict_type_linear_hash_table,
        .key_size = key_size,
        .elem_size = elem_size,
        .free_dict = hash_table_free,
        .clear = this_clear,
        .empty = this_empty,
        .contains_key = this_contains_key,
        .keyset = this_keyset,
        .get = this_get,
        .gets = this_gets,
        .remove = this_remove,
        .put = this_put,
        .puts = this_puts,
        .num_elements = this_num_elements,
        .for_each = this_for_each,

        .extra = require_good_malloc(sizeof(hash_table_extra_t))
    };

    panic_if ((key_is_str && key_size != sizeof(char *)), BADARG, "key must be pointer to string");

    hash_table_extra_t *extra = (hash_table_extra_t*) result->extra;
    *extra = (hash_table_extra_t) {
        .hash_function = *hash_function,
        .num_slots = num_slots,
        .num_inuse = 0,
        .grow_factor = grow_factor,
        .max_load_factor = max_load_factor,
        .slots = require_good_malloc(num_slots * (key_size + elem_size)),
        .empty_indicator = key_is_str ? require_good_malloc(sizeof(char **)) : require_good_malloc(key_size),
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .equals = equals,
        .cleanup = cleanup,
        .key_is_str = key_is_str,
        .keyset = (struct vector_t*) vector_create(key_size, approx_num_keys)
    };

    if (key_is_str) {
        char *empty_str = malloc(1);
        *empty_str = '\0';
        extra->empty_indicator = &empty_str;
    } else {
        memset(extra->empty_indicator, UCHAR_MAX, key_size);
    }

    init_slots(extra->slots, num_slots, key_size, elem_size, extra->empty_indicator);
    hash_reset_counters(result);

    return result;
}

void hash_table_lock(dict_t *dict)
{
    require_instanceof_this(dict);
    hash_table_extra_t* extra = (hash_table_extra_t*) dict->extra;
    pthread_mutex_lock(&(extra->mutex));
    extra->counters.num_locks++;
}

void hash_table_unlock(dict_t *dict)
{
    require_instanceof_this(dict);
    pthread_mutex_unlock(&(((hash_table_extra_t*) dict->extra)->mutex));
}

bool hash_table_free(dict_t *dict)
{
    if (dict != NULL) {
        require_instanceof_this(dict);
        hash_table_extra_t *extra = (hash_table_extra_t *) dict->extra;
        require_nonnull(extra->slots);
        require_nonnull(extra->empty_indicator);

        if (extra->cleanup != NULL) {
            for (size_t slot_id = 0; slot_id < extra->num_slots; slot_id++) {
                if (!slot_is_empty(dict, extra, slot_id)) {
                    void *key = slot_get_key(extra->slots, slot_id, dict->key_size, dict->elem_size);
                    void *value = slot_get_value(extra->slots, slot_id, dict->key_size, dict->elem_size);

                    assert (key != NULL);
                    assert (value != NULL);

                    extra->cleanup(key, value);
                }
            }
        }

        if (extra->keyset != NULL) {
            vector_free(extra->keyset);
        }

        free(extra->slots);
        if (extra->key_is_str) {
            free(*(char **) extra->empty_indicator);
        }

        free(extra->empty_indicator);
        free(extra);
        free(dict);
        return true;
    } else return false;
}

void hash_reset_counters(dict_t *dict)
{
    require_instanceof_this(dict);
    hash_table_extra_t *extra = (hash_table_extra_t *) dict->extra;
    extra->counters = (counters_t) {
        .num_collisions               = 0,
        .num_locks                    = 0,
        .num_put_calls                = 0,
        .num_rebuilds                 = 0,
        .num_put_slotsearch           = 0,
        .num_updates                  = 0,
        .num_get_foundkey             = 0,
        .num_get_slotdisplaced        = 0,
        .num_get_nosuchkey_fullsearch = 0,
        .num_get_nosuchkey            = 0,
        .num_test_slot                = 0,
        .num_slot_get_key             = 0,
        .num_slot_get_value           = 0,
        .num_remove_key               = 0,
        .num_remove_slotdisplaced     = 0,
        .num_remove_nosuchkey_fullsearch = 0,
        .num_remove_nosuchkey         = 0
    };
}

void hash_table_info(dict_t *dict, linear_hash_table_info_t *info)
{
    require_instanceof_this(dict);
    hash_table_extra_t *extra = (hash_table_extra_t *) dict->extra;

    *info = (linear_hash_table_info_t) {
        .counters        = extra->counters,
        .num_slots_inuse = extra->num_inuse,
        .num_slots_free  = (extra->num_slots - extra->num_inuse),
        .load_factor     = extra->num_inuse /(double) extra->num_slots,
        .user_data_size  = ((dict->key_size + dict->elem_size) * extra->num_inuse),
        .overhead_size   = (sizeof(dict) + sizeof(hash_table_extra_t) +
                            dict->key_size /* empty indicator */ +
                            ((dict->key_size + dict->elem_size) * (extra->num_slots - extra->num_inuse)) /* reserved */)
    };
}

bool
str_equals(
        const void *lhs,
        const void *rhs)
{
    return (strcmp(*(char **)lhs, *(char **)rhs) == 0);
}

void
clean_up(
        void *key,
        void *value)
{
    char **key_string = (char **) key;
    char **val_string = (char **) value;
    free (*key_string);
    free (*val_string);
}

// ---------------------------------------------------------------------------------------------------------------------
// H E L P E R   I M P L E M E N T A T I O N
// ---------------------------------------------------------------------------------------------------------------------

void this_clear(struct dict_t *self)
{
    require_instanceof_this(self);
    hash_table_extra_t *extra = (hash_table_extra_t *) self->extra;
    extra->num_inuse = 0;
    init_slots(extra->slots, extra->num_slots, self->key_size, self->elem_size, extra->empty_indicator);
}

bool this_empty(const struct dict_t *self)
{
    require_instanceof_this(self);
    return (((hash_table_extra_t *) self->extra)->num_inuse == 0);
}

bool this_contains_key(const struct dict_t *self, const void *key)
{
    require_instanceof_this(self);
    return (this_get(self, key) != NULL);
}

const struct vector_t *this_keyset(const struct dict_t *self)
{
    require_instanceof_this(self);
    hash_table_extra_t *extra = (hash_table_extra_t *) self->extra;
    return extra->keyset;
}

const void *this_get(const struct dict_t *self, const void *key)
{
    vector_t *value_ptrs = this_gets(self, 1, key);
    assert (value_ptrs->num_elements <= 1);
    void *result = (value_ptrs->num_elements == 0 ? NULL : *(void **) vector_at(value_ptrs, 0));
    vector_free(value_ptrs);
    return result;
}

struct vector_t *this_gets(const struct dict_t *self, size_t num_keys, const void *keys)
{
    require_instanceof_this(self);
    require_nonnull(keys);
    require_not_zero(num_keys);

    vector_t *result = vector_create(sizeof(void *), num_keys);
    hash_table_extra_t *extra = (hash_table_extra_t *) self->extra;

    while (num_keys--) {
        const void *key = keys + (num_keys * self->key_size);

        size_t hash_code = calc_hash_code(self, extra, key);
        size_t slot_id = convert_to_slot_id(hash_code, extra);
        size_t round_trip_slot_id = calc_round_trip(slot_id, extra);

        bool empty_slot_found = test_slot(self, extra, slot_id);

        if (!empty_slot_found) {
            while ((round_trip_slot_id != slot_id) && !empty_slot_found) {
                const void *old_key = get_key(self, extra, slot_id);
                if (keys_match(self, extra, key, old_key)) {
                    extra->counters.num_get_foundkey++;
                    void *value_ptr = get_value(self, extra, slot_id);
                    vector_add(result, 1, &value_ptr);
                    goto next_key;
                } else {
                    extra->counters.num_get_slotdisplaced++;
                    slot_id = convert_to_slot_id((slot_id + 1), extra);
                }
                empty_slot_found = test_slot(self, extra, slot_id);
            }
        }
next_key:
        extra->counters.num_get_nosuchkey_fullsearch += (round_trip_slot_id == slot_id);
        extra->counters.num_get_nosuchkey += empty_slot_found;
    }

    return result;
}

bool this_remove(struct dict_t *self, size_t num_keys, const void *keys)
{
    panic("The function '%s' is not properly implemented.", "this_remove"); // TODO: Mark removed elements as "deleted". Actually removing them causes issues with chained entries for linear probing

    require_instanceof_this(self);
    require_nonnull(keys);
    require_not_zero(num_keys);

    bool removed_one_entry = false;
    hash_table_extra_t *extra = (hash_table_extra_t *) self->extra;

    while (num_keys--) {
        const void *key = keys + (num_keys * self->key_size);

        size_t hash_code = calc_hash_code(self, extra, key);
        size_t slot_id = convert_to_slot_id(hash_code, extra);
        size_t round_trip_slot_id = calc_round_trip(slot_id, extra);

        bool empty_slot_found = test_slot(self, extra, slot_id);

        if (!empty_slot_found) {
            while ((round_trip_slot_id != slot_id) && !empty_slot_found) {
                const void *old_key = get_key(self, extra, slot_id);
                if (keys_match(self, extra, key, old_key)) {
                    extra->counters.num_remove_key++;
                    remove_key(self, extra, slot_id);
                    extra->num_inuse--;
                    assert (extra->num_inuse < extra->num_slots);
                    removed_one_entry = true;
                    goto next_key;
                } else {
                    extra->counters.num_remove_slotdisplaced++;
                    slot_id = convert_to_slot_id((slot_id + 1), extra);
                }
                empty_slot_found = test_slot(self, extra, slot_id);
            }
        }
        next_key:
        extra->counters.num_remove_nosuchkey_fullsearch += (round_trip_slot_id == slot_id);
        extra->counters.num_remove_nosuchkey += empty_slot_found;
    }
    return removed_one_entry;
}

void this_put(struct dict_t *self, const void *key, const void *value)
{
    require_instanceof_this(self);
    require_nonnull(key && value);
    this_puts(self, 1, key, value);
}

void this_puts(struct dict_t *self, size_t num_elements, const void *keys, const void *values)
{
    require_instanceof_this(self);
    require_nonnull(keys && values);
    require_not_zero(num_elements);

    hash_table_extra_t *extra = (hash_table_extra_t *) self->extra;

    while (num_elements--) {
        float load_factor = extra->num_inuse/(float) extra->num_slots;
        if (load_factor > extra->max_load_factor) {
            rebuild(self, extra);
        }

        const void *key   = keys + (num_elements * self->key_size);
        const void *value = values + (num_elements * self->elem_size);

        assert (key != NULL);
        assert (value != NULL);

        size_t hash_code = calc_hash_code(self, extra, key);
        size_t slot_id = convert_to_slot_id(hash_code, extra);
        size_t round_trip_slot_id = calc_round_trip(slot_id, extra);

        bool slot_found = test_slot(self, extra, slot_id);
        bool new_key    = false;
        if (!slot_found) {
            while ((round_trip_slot_id != slot_id) && !slot_found) {
                const void *old_key = get_key(self, extra, slot_id);
                bool key_match = keys_match(self, extra, key, old_key);
                if (!key_match) {
                    extra->counters.num_put_slotsearch++;
                    slot_id = convert_to_slot_id((slot_id + 1), extra);
                    //break;
                } else {
                    extra->counters.num_updates++;
                    new_key = true;
                    break;
                }
                slot_found = test_slot(self, extra, slot_id);
            }
        } else {
            extra->counters.num_collisions++;
        }

        if (round_trip_slot_id == slot_id) {
            rebuild(self, extra);
            this_put(self, key, value);
        } else {
            slot_put(extra->slots, slot_id, self->key_size, self->elem_size, key, value);
            assert (extra->num_inuse < extra->num_slots);
            extra->num_inuse++;
            extra->counters.num_put_calls++;
            if (new_key) {
                vector_add(extra->keyset, 1, key);
            }
        }
    }
}

size_t this_num_elements(struct dict_t *self)
{
    require_instanceof_this(self);
    return ((hash_table_extra_t *) self->extra)->num_inuse;
}

void this_for_each(struct dict_t *self, void *capture, void (*consumer)(void *capture, const void *key, const void *value))
{
    require_instanceof_this(self);
    hash_table_extra_t * extra = (hash_table_extra_t *) self->extra;
    size_t slot_idx = extra->num_slots;
    size_t slot_size = (self->key_size + self->elem_size);

    while (slot_idx--) {
        void *slot_data = (extra->slots + (slot_idx * slot_size));

        if (extra->key_is_str) {
            assert (((char **) slot_data) != NULL);
        }
        if (!is_slot_empty(self, extra, slot_data, extra->empty_indicator))
            consumer (capture, slot_data, (slot_data + self->key_size));
    }
}

static inline void init_slots(void *slots, size_t num_slots, size_t key_size, size_t elem_size,
                              void *empty_indicator)
{
    assert (slots && num_slots > 0 && key_size > 0);

    size_t slot_len = (key_size + elem_size);
    while (num_slots--) {
        memcpy(slots + (num_slots * slot_len), empty_indicator, key_size);
    }
}

static inline bool slot_is_empty(const dict_t *self, hash_table_extra_t *extra, size_t slot_id)
{
    assert (self && extra && slot_id < extra->num_slots);
    void *pos = seek_to_slot(extra->slots, slot_id, self->key_size, self->elem_size);
    bool empty = is_slot_empty(self, extra, pos, extra->empty_indicator);
    return empty;
}

static inline void slot_put(void *slots, size_t slot_id, size_t key_size, size_t elem_size, const void *key, const void *data)
{
    assert (slots && key && data && key_size > 0 && elem_size > 0);
    void *slot_data = seek_to_slot(slots, slot_id, key_size, elem_size);
    memcpy(slot_data, key, key_size);               // Test
    memcpy(slot_data + key_size, data, elem_size);
}

static inline void *slot_get_key(void *slots, size_t slot_id, size_t key_size, size_t elem_size)
{
    assert (slots && key_size > 0 && elem_size > 0);
    return seek_to_slot(slots, slot_id, key_size, elem_size);
}

static inline void *slot_get_value(void *slots, size_t slot_id, size_t key_size, size_t elem_size)
{
    assert (slots && key_size > 0 && elem_size > 0);
    return seek_to_slot(slots, slot_id, key_size, elem_size) + key_size;
}

static inline void slot_remove(void *slots, size_t slot_id, size_t key_size, size_t elem_size, void *empty_indicator)
{
    assert (slots && empty_indicator && elem_size > 0 && key_size > 0);
    size_t slot_len = (key_size + elem_size);
    memcpy(slots + (slot_id * slot_len), empty_indicator, key_size);
}

static inline void rebuild(const dict_t *self, hash_table_extra_t *extra)
{
    assert (self && extra && extra->grow_factor > 1);

    size_t new_num_slots = ceil(extra->num_slots * extra->grow_factor);
    dict_t *tmp = hash_table_create(&extra->hash_function, self->key_size, self->elem_size,
                                    new_num_slots, extra->grow_factor, extra->max_load_factor);

    size_t slot_idx = extra->num_slots;
    size_t slot_size = (self->key_size + self->elem_size);

    while (slot_idx--) {
        void *old_slot_data = (extra->slots + (slot_idx * slot_size));
        if (!is_slot_empty(self, extra, old_slot_data, extra->empty_indicator)) {
            tmp->put(tmp, old_slot_data, old_slot_data + self->key_size);
        }
    }

    counters_t counters = extra->counters;
    counters.num_rebuilds++;
    swap((hash_table_extra_t *) tmp->extra, extra);
    extra->counters = counters;
    hash_table_free(tmp);
}

static inline void swap(hash_table_extra_t *a, hash_table_extra_t *b)
{
    hash_table_extra_t tmp = shallow_cpy(a);
    *a = shallow_cpy(b);
    *b = shallow_cpy(&tmp);
}