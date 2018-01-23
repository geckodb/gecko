// Copyright (C) 2017 Marcus Pinnecke
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either user_port 3 of the License, or
// (at your option) any later user_port.
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

#include <apr_hash.h>
#include <apr_strings.h>

#include <gecko-commons/gs_error.h>
#include <gecko-commons/gs_spinlock.h>
#include <gecko-commons/containers/gs_vec.h>
#include <gecko-commons/containers/gs_hash.h>

#include <gs_dispatcher.h>

// ---------------------------------------------------------------------------------------------------------------------
// D A T A T Y P E S
// ---------------------------------------------------------------------------------------------------------------------

typedef struct gs_dispatcher_t
{
    bool            accept_new;
    apr_queue_t    *queue;
    apr_pool_t     *mem_pool;
    gs_hash_t      *handler_map;
} gs_dispatcher_t;

// ---------------------------------------------------------------------------------------------------------------------
// H E L P E R   P R O T O T Y P E S
// ---------------------------------------------------------------------------------------------------------------------

 gs_vec_t *dispatcher_get_handler(gs_dispatcher_t *dispatcher, gs_signal_type_e signal);

static inline int singnal_comp(const void *lhs, const void *rhs);

// ---------------------------------------------------------------------------------------------------------------------
// I N T E R F A C E  I M P L E M E N T A T I O N
// ---------------------------------------------------------------------------------------------------------------------

GS_DECLARE(gs_status_t) gs_dispatcher_create(gs_dispatcher_t **dispatcher)
{
    gs_dispatcher_t *result = GS_REQUIRE_MALLOC(sizeof(gs_dispatcher_t));
    result->accept_new = true;
    apr_pool_create(&result->mem_pool, NULL);
    apr_queue_create(&result->queue, MESSAGE_QUEUE_SIZE_MAX, result->mem_pool);

    gs_hash_create(&result->handler_map, DISPATCHER_NUM_ROUTERS, singnal_comp);

    *dispatcher = result;
    return GS_SUCCESS;
}

GS_DECLARE(gs_status_t) gs_dispatcher_dispose(gs_dispatcher_t **dispatcher_ptr)
{
    GS_REQUIRE_NONNULL(dispatcher_ptr)
    GS_REQUIRE_NONNULL(*dispatcher_ptr)
    gs_dispatcher_t *dispatcher = *dispatcher_ptr;
    apr_pool_destroy(dispatcher->mem_pool);
    gs_hash_dispose(dispatcher->handler_map);
    GS_DEBUG("dispatcher %p disposed", dispatcher);
    free(dispatcher);
    *dispatcher_ptr = NULL;
    return GS_SUCCESS;
}

GS_DECLARE(gs_status_t) gs_dispatcher_start(gs_dispatcher_t *dispatcher)
{
    GS_REQUIRE_NONNULL(dispatcher)

    apr_status_t status;
    gs_signal_type_e signal;
    gs_event_t *event;
    gs_vec_t *handlers;

    GS_DEBUG("dispatcher %p is starting", dispatcher);

    while (true) {
        GS_DEBUG("dispatcher %p is waiting for events", dispatcher);
        if (!dispatcher->accept_new && (apr_queue_size(dispatcher->queue) == 0)) {
            break;
        }
        if ((status = apr_queue_pop(dispatcher->queue, (void **) &event)) != APR_EINTR) {
            gs_error_if((status == APR_EOF), err_dispatcher_terminated);
            assert (status == APR_SUCCESS && event != NULL);
            signal = gs_event_get_signal(event);
            handlers = dispatcher_get_handler(dispatcher, signal);
            WARN_IF(((signal != GS_SIG_HEARTBEAT) && (!handlers)), "no handler for signal '%d' registered", signal);
            GS_DEBUG("dispatcher %p received event for signal %d", dispatcher, signal);
            for (gs_event_handler_t *it = gs_vec_begin(handlers);
                (it && (it != gs_vec_end(handlers))); it++) {
                (*it)(event); // TODO: handler can return void
            }
            gs_event_free(event);
        }
    }
    GS_DEBUG("dispatcher %p left main loop", dispatcher);
    return GS_SUCCESS;
}

GS_DECLARE(gs_status_t) gs_dispatcher_shutdown(gs_dispatcher_t *dispatcher, gs_system_t *system)
{
    GS_REQUIRE_NONNULL(dispatcher)
    GS_DEBUG("initialize shutdown for dispatcher %p", dispatcher);
    gs_dispatcher_publish(dispatcher, gs_event_heartbeat_new(system, dispatcher));
    dispatcher->accept_new = false;
    return GS_SUCCESS;
}

GS_DECLARE(gs_status_t) gs_dispatcher_connect(gs_dispatcher_t *dispatcher, gs_signal_type_e signal, gs_event_handler_t handler)
{
    GS_REQUIRE_NONNULL(dispatcher)
    GS_REQUIRE_NONNULL(handler)

    //sizeof(gs_signal_type_e), sizeof(gs_vec_t)

    if (gs_hash_get(dispatcher->handler_map, &signal, sizeof(gs_signal_type_e)) == NULL) {
        gs_vec_t *vec = gs_vec_new(sizeof(gs_event_handler_t), 10);
        gs_signal_type_e *signal_cpy = GS_REQUIRE_MALLOC(sizeof(gs_signal_type_e));
        *signal_cpy = signal;
        gs_hash_set(dispatcher->handler_map, signal_cpy, sizeof(gs_signal_type_e), vec);
    }
    gs_vec_t *handlers = (gs_vec_t *) gs_hash_get(dispatcher->handler_map, &signal, sizeof(gs_signal_type_e));
    assert (handlers);
    gs_vec_pushback(handlers, 1, &handler);
    return GS_SUCCESS;
}

GS_DECLARE(gs_status_t) gs_dispatcher_publish(gs_dispatcher_t *dispatcher, gs_event_t *event)
{
    GS_REQUIRE_NONNULL(dispatcher)
    GS_REQUIRE_NONNULL(event)
    if (dispatcher->accept_new) {
        apr_status_t status;
        while ((status = apr_queue_push(dispatcher->queue, event)) == APR_EINTR);
        gs_error_if((status != APR_SUCCESS), err_internal);
        return GS_SUCCESS;
    } else {
        GS_DEBUG("dispatcher %p is shutting down and skipped event %p", dispatcher, event);
        return GS_SKIPPED;
    }
}

GS_DECLARE(gs_status_t) gs_dispatcher_waitfor(gs_dispatcher_t *dispatcher, gs_event_t *event, gs_system_t *system)
{
    //gs_spinlock_t lock;
    //gs_spinlock_create(&lock);
    gs_dispatcher_publish(dispatcher, gs_event_new_blocking(system, NULL, event));
    GS_DEBUG2("thread is being locked...");
    //gs_spinlock_lock(lock);
    GS_DEBUG2("thread was unlocked...");
    //gs_spinlock_dispose(&lock);
    return GS_SUCCESS;
}

// ---------------------------------------------------------------------------------------------------------------------
// H E L P E R  I M P L E M E N T A T I O N
// ---------------------------------------------------------------------------------------------------------------------

 gs_vec_t *dispatcher_get_handler(gs_dispatcher_t *dispatcher, gs_signal_type_e signal)
{
    return (gs_vec_t *) gs_hash_get(dispatcher->handler_map, &signal, sizeof(gs_signal_type_e));
}

static inline int singnal_comp(const void *a, const void *b)
{
    gs_signal_type_e lhs = *(gs_signal_type_e *) a;
    gs_signal_type_e rhs = *(gs_signal_type_e *) b;
    return lhs < rhs ? -1 : (lhs > rhs ? +1 : 0);
}