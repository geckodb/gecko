#include <storage/database.h>
#include <storage/files.h>
#include <storage/dirs.h>
#include <apr_strings.h>

// The file containing node records and the per-node version chain
#define DB_NODES_FILE                "nodes.records"

// The file mapping a unique node id to its first version in the node file
#define DB_NODES_INDEX_FILE          "nodes.heads"


// The file containing property records and the per-property version chain
#define DB_PROPS_FILE                "props.records"

// The file mapping a unique prop id to its first version in the property file
#define DB_PROPS_INDEX_FILE          "props.heads"

// A file containing lists of props used to attach a set of properties to nodes and edges
#define DB_PROPS_LIST_FILE           "proplists.records"

// A file mapping a unique prop list id to its offset in the props list file
#define DB_PROPS_LIST_INDEX_FILE     "proplists.heads"


// A file containing variable-length strings
#define DB_STRINGPOOL_FILE           "strings.records"

// A file mapping a unique string identifier to its offset and legnth in the strings record file
#define DB_STRINGPOOL_INDEX_FILE     "strings.lookup"


#define DB_EDGES_FILE           "edges.records"


typedef struct database_t
{
    apr_pool_t         *apr_pool;
    char               *dbpath;
    FILE               *node_records,
                       *node_heads,

                       *prop_records,
                       *prop_heads,
                       *proplist_records,
                       *proplist_heads,

                       *string_records,
                       *string_lookup,

                       *edge_records;


    const char         *nodes_records_path,
                       *nodes_heads_path,

                       *prop_records_path,
                       *prop_heads_path,
                       *proplist_records_path,
                       *proplist_heads_path,

                       *string_records_path,
                       *string_lookup_path,

                       *edges_db_path;
    gs_spinlock_t       spinlock;

} database_t;

typedef struct __attribute__((__packed__)) nodes_header_t
{
    node_id_t                next_node_id;
    node_slot_id_t           next_node_slot_id;
    node_slot_id_t           capacity;
} node_records_header_t;

typedef struct __attribute__((__packed__)) edges_header_t
{
    edge_slot_id_t           next_edge_id;
    edge_slot_id_t           capacity;
} edges_header_t;

typedef struct __attribute__((__packed__)) edges_t
{
    struct {
        short           in_use            : 1;
        short           is_final_version  : 1;
    }                   flags;
    node_slot_id_t             head_node,
                               tail_node;
    timestamp_t                start_time;
    prop_slot_id_t             next_prop;
} edges_t;

typedef struct __attribute__((__packed__)) node_index_header_t
{
    node_id_t           node_cursor;
} node_index_header_t;

typedef node_slot_id_t  node_index_entry_t;

typedef struct database_node_cursor_t
{
    database_t          *database;
    node_id_t            max_node_id,
                         current_node_id;
    bool                 open;
    bool                 first;
} database_node_cursor_t;

typedef struct database_node_version_cursor_t
{
    database_t          *database;
    database_node_t      node;
} database_node_version_cursor_t;

typedef struct __attribute__((__packed__)) props_header_t
{
    prop_id_t                next_props_id;
    prop_slot_id_t           next_props_slot_id;
    prop_slot_id_t           capacity;
} props_header_t;

static inline gs_status_t exists_db(database_t *database);

static inline gs_status_t unsafe_nodes_db_autoresize(database_t *db, node_records_header_t *header, size_t num_nodes);
static inline gs_status_t unsafe_read_node_records_header(node_records_header_t *header, database_t *database);
static inline gs_status_t unsafe_read_node_index_header(node_index_header_t *header, database_t *database);
static inline gs_status_t unsafe_update_node_records_header(database_t *database, const node_records_header_t *header);
static inline gs_status_t unsafe_update_node_index_header(database_t *database, const node_index_header_t *header);
static inline node_slot_id_t  unsafe_read_node(database_node_t *node, node_id_t node_id, database_t *database);
static inline node_slot_id_t  unsafe_node_last_version(database_node_t *last, const database_node_t *start,
                                                       node_slot_id_t start_slot, database_t *db);
static inline gs_status_t unsafe_node_new_version(node_slot_id_t *cpy_slot_id, database_node_t *cpy,
                                                  database_node_t *original, database_t *db,
                                                  size_t approx_num_new_nodes, node_slot_id_t original_slot);
static inline gs_status_t unsafe_node_adjust_lifetime(database_node_t *node, node_slot_id_t slot_id,
                                                      database_t *db, const timespan_t *lifetime);

static inline gs_status_t create_db(database_t *database);
static inline gs_status_t create_nodes_db(database_t *database);
static inline gs_status_t create_edges_db(database_t *database);
static inline gs_status_t create_stringpool_db(database_t *database);
static inline gs_status_t open_db(database_t *database);
static inline gs_status_t open_nodes_db(database_t *database);
static inline gs_status_t open_edges_db(database_t *database);
static inline gs_status_t open_stringpool_db(database_t *database);

gs_status_t database_open(database_t **db, const char *dbpath)
{
    gs_status_t status;
    database_t *database;

    apr_initialize();
    database = GS_REQUIRE_MALLOC(sizeof(database_t));
    apr_pool_create(&database->apr_pool, NULL);
    database->dbpath = apr_pstrdup(database->apr_pool, dbpath);
    gs_spinlock_create(&database->spinlock);

    if ((status = dirs_add_filename(&database->nodes_records_path, database->dbpath, DB_NODES_FILE,
                                    database->apr_pool)) != GS_SUCCESS)
        return status;

    if ((status = dirs_add_filename(&database->nodes_heads_path, database->dbpath, DB_NODES_INDEX_FILE,
                                    database->apr_pool)) != GS_SUCCESS)
        return status;

    if ((status = dirs_add_filename(&database->edges_db_path, database->dbpath, DB_EDGES_FILE,
                                    database->apr_pool)) != GS_SUCCESS)
        return status;

    if ((status = dirs_add_filename(&database->string_records_path, database->dbpath, DB_STRINGPOOL_FILE,
                                    database->apr_pool)) != GS_SUCCESS)
        return status;

    if ((status = dirs_add_filename(&database->string_lookup_path, database->dbpath, DB_STRINGPOOL_INDEX_FILE,
                                    database->apr_pool)) != GS_SUCCESS)
        return status;

    if (exists_db(database) == GS_FALSE) {
        if (create_db(database) == GS_SUCCESS && database_close(database) == GS_SUCCESS) {
            return database_open(db, dbpath);
        } else return GS_FAILED;
    }

    status = open_db(database);

    *db = database;

    return status;
}

// Seek to slot in node records file
#define NODE_RECORDS_FILE_SEEK(db, slot_id)                                                                            \
    fseek(db->node_records, sizeof(node_records_header_t) + slot_id  * sizeof(database_node_t), SEEK_SET);

// Seek to index slot for particular node id
#define NODE_INDEX_FILE_SEEK(db, node_id)                                                                              \
    fseek(db->node_heads, sizeof(node_index_header_t) + node_id * sizeof(node_index_entry_t), SEEK_SET);

node_id_t *database_nodes_create(gs_status_t *result, database_t *db, size_t num_nodes, const timespan_t *lifetime)
{
    node_records_header_t records_header;
    node_index_header_t   index_header;

    // For rollback operations
    node_id_t             node_record_next_id_backup;
    node_slot_id_t        node_record_next_slot_id_backup;
    node_id_t             node_index_cursor_backup;

    node_id_t *retval = GS_REQUIRE_MALLOC(num_nodes * sizeof(node_id_t));

    database_lock(db);

    unsafe_read_node_records_header(&records_header, db);
    unsafe_read_node_index_header(&index_header, db);

    // Store old information from the header in case writes to the storage or index fails
    node_record_next_id_backup = records_header.next_node_id;
    node_record_next_slot_id_backup = records_header.next_node_slot_id;
    node_index_cursor_backup = index_header.node_cursor;

    assert(records_header.next_node_id == index_header.node_cursor);

    if (unsafe_nodes_db_autoresize(db, &records_header, num_nodes) != GS_SUCCESS) {
        // Something went wrong during expansion. No changes are written to the database, since the
        // records_header is not updated (although the database file now is grown beyond the size indicated
        // be the capacity_in_byte value).
        database_unlock(db);
        return NULL;
    }

    // Seek to next free slot in node records file
    NODE_RECORDS_FILE_SEEK(db, records_header.next_node_slot_id);

    // Seek to next index slot in the node index file
    NODE_INDEX_FILE_SEEK(db, index_header.node_cursor);

    // New note to be inserted. The node is setup with client and system time but blank content, i.e., no properties and
    // no references to other version. However, the node begins its existing at client/system time and has currently
    // and infinite lifetime.
    database_node_t node = {
        .lifetime               = *lifetime,
        .flags.in_use           = TRUE,
        .flags.has_version_prev = FALSE,
        .flags.has_version_next = FALSE,
        .flags.has_prop         = FALSE,
        .flags.has_edge_in      = FALSE,
        .flags.has_edge_out     = FALSE
    };

    // Store new notes to the nodes record file
    for (int i = 0; i < num_nodes; i++) {
        // Set unique node id and system creation time
        node.unique_id = records_header.next_node_id;
        node.creation_time = GS_SYSTEM_TIME_MS();

        // Get the current write first_slot which will be used in the index to enable an efficient mapping from
        // unique node id to its first version
        node_index_entry_t first_slot = records_header.next_node_slot_id++;

        // Write node to store
        if (fwrite(&node, sizeof(database_node_t), 1, db->node_records) != 1) {
            // Something went wront during write. Reset these changes by not updating the records_header (i.e., changes writen
            // so far are overwritten next time), release lock and notify about failure
            database_unlock(db);
            return NULL;
        };

        // Register first_slot of this node object in the index file such that the unique node id (i.e., the i-th
        // element) in the index file maps to the first_slot of the first node in the storage file.
        if (fwrite(&first_slot, sizeof(node_index_entry_t), 1, db->node_heads) != 1) {
            // In case this write fails, the index to the new first_slot of some added nodes (these before this write fails)
            // were written. However, since the index header is not updated, these changes will be overwritten the next
            // time. Thus, no rollback is needed.
            database_unlock(db);
            return NULL;
        };
        retval[i] = records_header.next_node_id++;
        index_header.node_cursor = records_header.next_node_id;
    }

    // Write down everything which is in the output buffer
    fflush(db->node_records);
    fflush(db->node_heads);

    // Update both the records header and the index header to register the changes finally.
    unsigned max_retry;
    gs_status_t status_store_update, status_index_update;

    // Update records header in order to store that new node records were written. Give this 100 tries or fails.
    max_retry = 100;
    do {
        status_store_update = unsafe_update_node_records_header(db, &records_header);
    } while (status_store_update != GS_SUCCESS && max_retry--);

    // Update index header in order to store that new nod _records were written. Give this 100 tries or fails.
    max_retry = 100;
    do {
        status_index_update = unsafe_update_node_index_header(db, &index_header);
    } while (status_index_update != GS_SUCCESS && max_retry--);

    if (status_store_update != GS_SUCCESS) {
        // Updating the store header fails for some reasons. Depending on the outcome of the index write this
        // may lead to an serious issue
        if (status_index_update != GS_SUCCESS) {
            // Both update request fail which means that data might be written to the store and the index but
            // their registration (in the header) is missing. Therefore, these changes will be overwritten the next
            // time. To sum up, this case is an database insert failure without side effects.

            // Nothing to do but to release the lock and notify about failure.
            database_unlock(db);
            GS_OPTIONAL(result != NULL, *result = GS_WRITE_FAILED)
        } else {
            // The index is updated and now points to nodes that are not stored in the store. Try to rollback the
            // index update by resetting the index header.
            index_header.node_cursor = node_index_cursor_backup;

            // Update index header in order to store that new nod _records were written. Give this 100 tries or fails.
            max_retry = 100;
            do {
                status_index_update = unsafe_update_node_index_header(db, &index_header);
            } while (status_index_update != GS_SUCCESS && max_retry--);

            if (status_index_update == GS_SUCCESS) {
                // Rollback successful, flush buffers
                fflush(db->node_heads);
                database_unlock(db);
                GS_OPTIONAL(result != NULL, *result = GS_WRITE_FAILED)
            } else {
                // Rollback failed which means that the index is damaged and not repairable by only resetting the
                // index header.
                // TODO: Rebuild the index file and try to swap the damaged index file with the new one
                database_unlock(db);
                panic("Node index file '%s' was corrupted and cannot be repaired.", db->nodes_heads_path);
                GS_OPTIONAL(result != NULL, *result = GS_CORRUPTED)
            }
        }
        free (retval);
        return NULL;
    } else {
        // Updating the store header was successful. Depending on the index write outcome, there is maybe another issue
        if (status_index_update != GS_SUCCESS) {
            // The storage now contains registered nodes which are not receivable from the index. Even worse, since
            // the index is not updates, subsequent writes now will lead to an index mapping not to the new nodes
            // in that call but to the nodes that were written with this call. Try to rollback changes in the
            // storage file by resetting the header.

            records_header.next_node_id = node_record_next_id_backup;
            records_header.next_node_slot_id = node_record_next_slot_id_backup;

            max_retry = 100;
            do {
                status_store_update = unsafe_update_node_records_header(db, &records_header);
            } while (status_store_update != GS_SUCCESS && max_retry--);

            if (status_store_update == GS_SUCCESS) {
                // Rollback was successful
                fflush(db->node_records);
                database_unlock(db);
                GS_OPTIONAL(result != NULL, *result = GS_WRITE_FAILED)
            } else {
                // Rollback failed which means that the storage is damaged and not repairable by only resetting the
                // storage header.
                // TODO: Handling this failure case to avoid corruption of database
                database_unlock(db);
                panic("Node storage file '%s' was corrupted and cannot be repaired.", db->nodes_records_path);
                GS_OPTIONAL(result != NULL, *result = GS_CORRUPTED)
            }
            free (retval);
            return NULL;
        } else {
            // Both updates were performed successfully. Release the lock and notify about this success.
            database_unlock(db);
            GS_OPTIONAL(result != NULL, *result = GS_SUCCESS)
            return retval;
        }
    }
}

gs_status_t database_nodes_alter_lifetime(database_t *db, node_id_t *nodes, size_t num_nodes, const timespan_t *lifetime)
{
    database_node_t   node_first, node_last, new_node;
    node_slot_id_t    first_slot, last_slot, cpy_slot;
    node_id_t        *node_id;

    database_lock(db);

    while (num_nodes--) {
        node_id = nodes++;
        first_slot = unsafe_read_node(&node_first, *node_id, db);
        last_slot  = unsafe_node_last_version(&node_last, &node_first, first_slot, db);
        if (unsafe_node_new_version(&cpy_slot, &new_node, &node_last, db, num_nodes, last_slot) != GS_SUCCESS) {
            // Database file could not be resized although that is required. Thus, this operations fails
            // but eventual changes are overwritten by the next call since the header is not updated.
            // Unlock the database and notify about the failure.
            database_unlock(db);
            return GS_FAILED;
        }
        if (unsafe_node_adjust_lifetime(&new_node, cpy_slot, db, lifetime) != GS_SUCCESS) {
            database_unlock(db);
            return GS_FAILED;
        }
    }
    fflush(db->node_records);

    database_unlock(db);
    return GS_SUCCESS;
}

void database_lock(database_t *db)
{
    // Acquire exclusive access to the database file
    gs_spinlock_lock(&db->spinlock);
}

void database_unlock(database_t *db)
{
    // Release exclusive access to the database file
    gs_spinlock_unlock(&db->spinlock);
}

node_id_t database_nodes_last_id(database_t *db)
{
    node_records_header_t header;

    database_lock(db);
    unsafe_read_node_records_header(&header, db);
    database_unlock(db);

    return header.next_node_id;
}

gs_status_t database_nodes_fullscan(database_node_cursor_t **cursor, database_t *db)
{
    database_node_cursor_t *result = GS_REQUIRE_MALLOC(sizeof(database_node_cursor_t));
    result->current_node_id = 0;
    result->open            = FALSE;
    result->first           = TRUE;
    result->database        = db;
    result->max_node_id     = database_nodes_last_id(db);
    *cursor = result;
    return GS_SUCCESS;
}

gs_status_t database_node_cursor_open(database_node_cursor_t *cursor)
{
    if (cursor && !cursor->open) {
        database_lock(cursor->database);
        cursor->open = TRUE;
    }
    return cursor ? GS_SUCCESS : GS_ILLEGALARG;
}

gs_status_t database_node_cursor_next(database_node_cursor_t *cursor)
{
    cursor->current_node_id = cursor->current_node_id + (cursor->first ? 0 : 1);
    cursor->first = FALSE;
    return (cursor->current_node_id < cursor->max_node_id) ? GS_TRUE : GS_FALSE;
}

gs_status_t database_node_cursor_read(database_node_t *node, const database_node_cursor_t *cursor)
{
    bool valid = (node && cursor && cursor->open);
    if (valid) {
        unsafe_read_node(node, cursor->current_node_id, cursor->database);
    }
    return (valid ? GS_SUCCESS : GS_FAILED);
}

gs_status_t database_node_cursor_close(database_node_cursor_t *cursor)
{
    if (cursor && cursor->open) {
        database_unlock(cursor->database);
        free (cursor);
    }
    return cursor ? GS_SUCCESS : GS_ILLEGALARG;
}

gs_status_t database_node_version_cursor_open(database_node_version_cursor_t **result, database_node_t *node,
                                              database_t *db)
{
    database_node_version_cursor_t *cursor = GS_REQUIRE_MALLOC(sizeof(database_node_version_cursor_t));
    cursor->database = db;
    cursor->node = *node;
    *result = cursor;
    return GS_SUCCESS;
}

gs_status_t database_node_version_cursor_next(database_node_version_cursor_t *cursor)
{
    bool result = (cursor->node.flags.has_version_next ? GS_TRUE : GS_FALSE );
    NODE_RECORDS_FILE_SEEK(cursor->database, cursor->node.version_next);
    fread(&cursor->node, sizeof(database_node_t), 1, cursor->database->node_records);
    return result;
}

gs_status_t database_node_version_cursor_read(database_node_t *node, database_node_version_cursor_t *cursor)
{
    *node = cursor->node;
    return GS_SUCCESS;
}

gs_status_t database_node_version_cursor_close(database_node_version_cursor_t *cursor)
{
    free (cursor);
    return GS_SUCCESS;
}

string_id_t *database_string_create(size_t *num_created_strings, database_t *db, const char **strings,
                                    size_t num_strings, string_create_mode_t mode)
{
    string_id_t *result = GS_REQUIRE_MALLOC(num_strings);
    size_t result_size = 0;

    database_lock(db);

    while (num_strings--) {
        //const char *string = strings++;


    }

    database_unlock(db);

    *num_created_strings = result_size;
    return result;
}

prop_id_t *database_create_prop(database_t *db, const target_t *target, const char *key, const value_t *value,
                                const timespan_t *lifetime)
{
    return GS_FAILED;
}

gs_status_t database_close(database_t *db)
{
    fclose(db->node_records);
    fclose(db->node_heads);
    fclose(db->edge_records);
    fclose(db->string_lookup);
    fclose(db->string_records);
    apr_pool_destroy(db->apr_pool);
    free (db);
    return GS_SUCCESS;
}

static inline gs_status_t exists_db(database_t *database)
{
    return (files_exists(database->edges_db_path) && files_exists(database->nodes_records_path) &&
            files_exists(database->nodes_heads_path) && files_exists(database->string_lookup_path) &&
            files_exists(database->string_records_path)) ?
           GS_TRUE : GS_FALSE;
}

static inline gs_status_t unsafe_nodes_db_autoresize(database_t *db, node_records_header_t *header, size_t num_nodes)
{
    if (header->next_node_slot_id + num_nodes > header->capacity) {
        // The capacity_in_byte of the records file is too less. Thus, expand file.

        size_t expand_size = 1.7f * (1 + num_nodes);
        fseek(db->node_records, 0, SEEK_END);

        database_node_t empty = {
                .flags.in_use = FALSE
        };

        for (int i = 0; i < expand_size; i++) {
            if (fwrite(&empty, sizeof (database_node_t), 1, db->node_records) != 1) {
                return GS_FAILED;
            }
        }
        header->capacity += expand_size;
    }
    return GS_SUCCESS;
}

static inline gs_status_t unsafe_read_node_records_header(node_records_header_t *header, database_t *database)
{
    fseek(database->node_records, 0, SEEK_SET);
    return (fread(header, sizeof(node_records_header_t), 1, database->node_records) == 1 ? GS_SUCCESS : GS_FAILED);
}

static inline gs_status_t unsafe_read_node_index_header(node_index_header_t *header, database_t *database)
{
    fseek(database->node_heads, 0, SEEK_SET);
    return (fread(header, sizeof(node_index_header_t), 1, database->node_heads) == 1 ? GS_SUCCESS : GS_FAILED);
}

static inline gs_status_t unsafe_update_node_records_header(database_t *database, const node_records_header_t *header)
{
    fseek(database->node_records, 0, SEEK_SET);
    bool result = fwrite(header, sizeof(node_records_header_t), 1, database->node_records) == 1 ? GS_SUCCESS : GS_FAILED;
    fflush(database->node_records);
    return result;
}

static inline gs_status_t unsafe_update_node_index_header(database_t *database, const node_index_header_t *header)
{
    fseek(database->node_heads, 0, SEEK_SET);
    bool result = fwrite(header, sizeof(node_index_header_t), 1, database->node_heads) == 1 ? GS_SUCCESS : GS_FAILED;
    fflush(database->node_heads);
    return result;
}

static inline node_slot_id_t unsafe_read_node(database_node_t *node, node_id_t node_id, database_t *db)
{
    node_index_entry_t node_1st_version_slot;
    // Seek to next index slot in the node index file
    NODE_INDEX_FILE_SEEK(db, node_id);
    fread(&node_1st_version_slot, sizeof(node_index_entry_t), 1, db->node_heads);

    NODE_RECORDS_FILE_SEEK(db, node_1st_version_slot);
    fread(node, sizeof(database_node_t), 1, db->node_records);

    return node_1st_version_slot;
}

static inline node_slot_id_t unsafe_node_last_version(database_node_t *last, const database_node_t *start,
                                                      node_slot_id_t start_slot, database_t *db)
{
    database_node_t cursor = *start;
    offset_t last_node_slot = start_slot;
    while (cursor.flags.has_version_next) {
        NODE_RECORDS_FILE_SEEK(db, cursor.version_next);
        last_node_slot = cursor.version_next;
        fread(&cursor, sizeof(database_node_t), 1, db->node_records);
    }
    *last = cursor;
    return last_node_slot;
}

static inline gs_status_t unsafe_node_new_version(node_slot_id_t *cpy_slot_id, database_node_t *cpy,
                                                  database_node_t *original, database_t *db,
                                                  size_t approx_num_new_nodes,
                                                  node_slot_id_t original_slot)
{
    node_records_header_t records_header;
    gs_status_t           status;
    unsigned              max_retry;
    database_node_t       local_cpy, local_original;

    unsafe_read_node_records_header(&records_header, db);

    if (unsafe_nodes_db_autoresize(db, &records_header, approx_num_new_nodes) != GS_SUCCESS) {
        return GS_FAILED;
    }

    // Seek to next free slot in node records file
    NODE_RECORDS_FILE_SEEK(db, records_header.next_node_slot_id);
    *cpy_slot_id = records_header.next_node_slot_id;

    assert (original->flags.has_version_next == FALSE);

    // Create shallow local_cpy of node, update the modification timestamp, and link to the old version
    // Make local copies of "cpy" and "original" argument to corruption on these objects in case
    // changes are not written to the file
    local_cpy = *original;
    local_cpy.creation_time = (timestamp_t) time(NULL);
    local_cpy.flags.has_version_prev = TRUE;
    local_cpy.version_prev = original_slot;

    // Modify version pointer of the old version to point to the new version
    local_original = *original;
    local_original.flags.has_version_next = TRUE;
    local_original.version_next = *cpy_slot_id;

    // Write new version of node to the node storage
    if (fwrite(&local_cpy, sizeof(database_node_t), 1, db->node_records) != 1) {
        // In case this write fails, stop here and return to the caller.
        // Since there are no changes to original, this case is just a reject of the request
        // to create a new node.
        return GS_FAILED;
    }

    // Modify the old version to point to the new version
    NODE_RECORDS_FILE_SEEK(db, original_slot);
    if (fwrite(&local_original, sizeof(database_node_t), 1, db->node_records) != 1) {
        // In this case, the new version is physically written to the database file but neither is that
        // change reflected in the node records file header nor in the original version. Hence, aborting
        // here is just a reject of the request to create a new node.
        return GS_FAILED;
    }



    // TODO: cpy all properties and all edges!

    // Prepare registration of the new node in the header
    records_header.next_node_slot_id++;

    // Update records header in order to store that new node records were written. Give this 100 tries or fails.
    max_retry = 100;
    do {
        status = unsafe_update_node_records_header(db, &records_header);
    } while (status != GS_SUCCESS && max_retry--);

    if (status != GS_SUCCESS) {
        // The header is not updated. Whenever another node is now added, 'original' will point to this new
        // node since its 'next' pointer is already updated. To solve this, try a rollback on the 'next' pointer
        // change

        // Write the old version of node to the node storage
        NODE_RECORDS_FILE_SEEK(db, original_slot);
        if (fwrite(original, sizeof(database_node_t), 1, db->node_records) != 1) {
            // Rollback failed
            // TODO: Handle the rollback-failed for "new version of copy"
            panic("Node store corruption: node '%lld' will point to undefined successor version (file '%s')",
                   original->unique_id, db->nodes_records_path);
            return GS_FAILED;
        } else {
            // Rollback was successful
            fflush(db->node_records);
            return GS_FAILED;
        }

    }

    // Changes are written to the database file. Thus, update the input parameters to reflect that change
    *cpy = local_cpy;
    *original = local_original;

    return GS_SUCCESS;
}

static inline gs_status_t unsafe_node_adjust_lifetime(database_node_t *node, node_slot_id_t slot_id,
                                                      database_t *db, const timespan_t *lifetime)
{
    node->lifetime = *lifetime;
    NODE_RECORDS_FILE_SEEK(db, slot_id);
    if (fwrite(node, sizeof(database_node_t), 1, db->node_records) != 1) {
        // Lifetime of node cannot be updated, reject request
        return GS_FAILED;
    } else {
        return GS_SUCCESS;
    }
}

static inline gs_status_t create_db(database_t *database)
{
    if (create_nodes_db(database) != GS_SUCCESS)
        return GS_FAILED;

    if (create_edges_db(database) != GS_SUCCESS)
        return GS_FAILED;

    if (create_stringpool_db(database) != GS_SUCCESS)
        return GS_FAILED;

    return GS_SUCCESS;
}

static inline gs_status_t create_nodes_db(database_t *database)
{
    node_records_header_t records_header = {
        .next_node_slot_id = 0,
        .next_node_id      = 0,
        .capacity     = DB_NODE_DEFAULT_CAPACITY
    };
    database_node_t node = {
        .flags.in_use = FALSE
    };

    node_index_header_t index_header = {
        .node_cursor      = 0
    };

    // Initialize node record file
    if ((database->node_records = fopen(database->nodes_records_path, "wb")) == NULL)
        return GS_FAILED;

    if (fwrite(&records_header, sizeof (node_records_header_t), 1, database->node_records) != 1)
        return GS_FAILED;

    for (int i = 0; i < records_header.capacity; i++) {
        if (fwrite(&node, sizeof (database_node_t), 1, database->node_records) != 1)
            return GS_FAILED;
    }

    // Initial node index file
    if ((database->node_heads = fopen(database->nodes_heads_path, "wb")) == NULL)
        return GS_FAILED;
    if (fwrite(&index_header, sizeof (node_index_header_t), 1, database->node_heads) != 1)
        return GS_FAILED;

    fflush(database->node_records);
    fflush(database->node_heads);

    return GS_SUCCESS;
}

static inline gs_status_t create_edges_db(database_t *database)
{
    edges_header_t header = {
        .next_edge_id = 0,
        .capacity     = DB_EDGE_DEFAULT_CAPACITY
    };
    edges_t edge = {
        .flags.in_use = FALSE
    };

    if ((database->edge_records = fopen(database->edges_db_path, "wb")) == NULL)
        return GS_FAILED;

    if (fwrite(&header, sizeof (edges_header_t), 1, database->edge_records) != 1)
        return GS_FAILED;

    for (int i = 0; i < header.capacity; i++) {
        if (fwrite(&edge, sizeof(edges_t), 1, database->edge_records) != 1)
            return GS_FAILED;
    }

    fflush(database->edge_records);

    return GS_SUCCESS;
}

typedef struct __attribute__((__packed__)) string_records_header_t
{
    string_id_t         next_id;
    offset_t            capacity_in_byte;
    offset_t            size_in_byte;
} string_records_header_t;

typedef struct __attribute__((__packed__)) string_lookup_header_t
{
    string_id_t         id_cursor;
    offset_t            capacity;
} string_lookup_header_t;

typedef struct __attribute__((__packed__)) string_lookup_entry_t
{
    offset_t            string_offset;
    unsigned            string_length;
    union {
        short           in_use : 1;
    } flags;
} string_lookup_entry_t;

static inline gs_status_t create_stringpool_db(database_t *database)
{
    char empty;

    string_records_header_t records_header = {
        .capacity_in_byte       = DB_STRING_POOL_CAPACITY_BYTE,
        .size_in_byte           = 0,
        .next_id                = 0
    };

    if ((database->string_records = fopen(database->string_records_path, "wb")) == NULL)
        return GS_FAILED;

    if ((database->string_lookup = fopen(database->string_lookup_path, "wb")) == NULL)
        return GS_FAILED;

    if (fwrite(&records_header, sizeof (string_records_header_t), 1, database->string_records) != 1)
        return GS_FAILED;

    fseek(database->string_records, sizeof(string_records_header_t) + records_header.capacity_in_byte, SEEK_SET);
    if (fwrite(&empty, sizeof(char), 1, database->string_records) != 1) {
            return GS_FAILED;
    }

    string_lookup_header_t lookup_header = {
        .id_cursor   = 0,
        .capacity    = DB_STRING_POOL_LOOKUP_SLOT_CAPACITY
    };

    string_lookup_entry_t entry = {
        .flags.in_use = FALSE
    };

    if (fwrite(&lookup_header, sizeof(string_lookup_header_t), 1, database->string_lookup) != 1) {
        return GS_FAILED;
    }

    for (int i = 0; i < lookup_header.capacity; i++) {
        if (fwrite(&entry, sizeof(string_lookup_entry_t), 1, database->string_lookup) != 1)
            return GS_FAILED;
    }

    fflush(database->string_records);
    fflush(database->string_lookup);

    return GS_SUCCESS;
}

static inline gs_status_t open_db(database_t *database)
{
    if (open_nodes_db(database) != GS_SUCCESS)
        return GS_FAILED;

    if (open_edges_db(database) != GS_SUCCESS)
        return GS_FAILED;

    if (open_stringpool_db(database) != GS_SUCCESS)
        return GS_FAILED;

    return GS_SUCCESS;
}

static inline gs_status_t open_nodes_db(database_t *database)
{
    if ((database->node_records = fopen(database->nodes_records_path, "r+b")) == NULL)
        return GS_FAILED;
    if ((database->node_heads = fopen(database->nodes_heads_path, "r+b")) == NULL)
        return GS_FAILED;
    return GS_SUCCESS;
}

static inline gs_status_t open_edges_db(database_t *database)
{
    if ((database->edge_records = fopen(database->edges_db_path, "r+b")) == NULL)
        return GS_FAILED;
    return GS_SUCCESS;
}

static inline gs_status_t open_stringpool_db(database_t *database)
{
    if ((database->string_lookup = fopen(database->string_lookup_path, "r+b")) == NULL)
        return GS_FAILED;
    if ((database->string_records = fopen(database->string_records_path, "r+b")) == NULL)
        return GS_FAILED;
    return GS_SUCCESS;
}