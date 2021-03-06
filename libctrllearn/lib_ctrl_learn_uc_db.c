/* Copyright (c) 2014  Mellanox Technologies, Ltd. All rights reserved.
 *
 * This software is available to you under BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */ 

#define  __UC_FDB_DB_C__

#include <complib/sx_log.h>
#include <complib/cl_mem.h>
#include <complib/cl_qmap.h>
#include <complib/cl_pool.h>
#include "lib_ctrl_learn_defs.h"
#include "lib_ctrl_learn_uc_db.h"
#include "lib_ctrl_learn_filters.h"
#include "errno.h"

#undef  __MODULE__
#define __MODULE__ CTRL_LEARN_UC_DB


/************************************************
 *  Local definitions
 ***********************************************/
/************************************************
 *  Global variables
 ***********************************************/
static sx_verbosity_level_t LOG_VAR_NAME(__MODULE__) =
    SX_VERBOSITY_LEVEL_NOTICE;

/************************************************
 *  Local variables
 ***********************************************/
static cl_pool_t main_db_pool;        /* pool for allocations of list head items */
static cl_qmap_t fdb_map;             /* qmap used for store filters list head   */
static ctrl_learn_log_cb ctrl_learn_logging_cb;  /* log callback */
static int fdb_initiated;             /* static flag risen when database initialized */

/************************************************
 *  Local function declarations
 ***********************************************/

static int fdb_uc_db_destroy(void);

static int fdb_uc_db_create_record(cl_qmap_t *fdb_map, uint64_t mac_key,
                                   fdb_uc_mac_entry_t **mac_item_pp);

static inline int set_record(fdb_uc_mac_entry_t *cur_mac_entry_p,
                             fdb_uc_mac_entry_t *new_mac_entry_p );
/************************************************
 *  Function implementations
 ***********************************************/

/**
 * This function initializes SW DB
 *
 * @return 0  operation completes successfully
 * @return -EPERM   general error
 */
int
fdb_uc_db_init(ctrl_learn_log_cb log_cb)
{
    int err = 0;

    if (fdb_initiated) {
        goto bail;
    }

    cl_qmap_init(&fdb_map);
    cl_pool_construct(&main_db_pool);

    err = cl_pool_init(&main_db_pool, MIN_FDB_ENTRIES, MAX_FDB_ENTRIES,
                       0, sizeof(fdb_uc_mac_entry_t), NULL, NULL,
                       NULL);
    if (err) {
        err = -EPERM;
        CL_LOG(CL_LOG_ERR, " err = %d\n", err);
        goto bail;
    }
    ctrl_learn_logging_cb = log_cb;
    err = fdb_uc_db_filter_init(log_cb);
    if (err) {
        err = -EPERM;
        CL_LOG(CL_LOG_ERR, " err = %d\n", err);
        goto bail;
    }
    fdb_initiated = 1;
    CL_LOG(CL_LOG_NOTICE, "Inited uc db, err = %d\n", err);

bail:
    return err;
}

/**
 *  This function set the module verbosity level
 *
 *  @param[in] verbosity_level - module verbosity level
 *
 *  @return 0 when successful
 *  @return -EPERM general error
 */
int
fdb_uc_db_log_verbosity_set(int verbosity_level)
{
    int err = 0;

    LOG_VAR_NAME(__MODULE__) = verbosity_level;

    return err;
}

/**
 * This function cleans the DB
 * @return 0  operation completes successfully
 */
int
fdb_uc_db_destroy(void)
{
    fdb_uc_mac_entry_t *mac_entry_item_p = NULL;
    int err = 0;

    CHECK_FDB_INIT_DONE;

    err = fdb_uc_db_get_first_record(&mac_entry_item_p);
    if (err) {
        CL_LOG(CL_LOG_ERR, " err = %d\n", err);
        goto bail;
    }
    while (mac_entry_item_p) {
        cl_qmap_remove_item(&fdb_map, &mac_entry_item_p->map_item);
        cl_pool_put(&main_db_pool, mac_entry_item_p);
        err = fdb_uc_db_get_first_record(&mac_entry_item_p);
        if (err) {
            CL_LOG(CL_LOG_ERR, " err = %d\n", err);
            goto bail;
        }
    }
bail:
    return err;
}

/**
 * This function de-inits the DB
 * @return 0 operation completes successfully
 *  @return -EPERM   general error
 */
int
fdb_uc_db_deinit()
{
    int err = 0;

    CHECK_FDB_INIT_DONE;

    err = fdb_uc_db_destroy();
    if (err) {
        err = -EPERM;
        CL_LOG(CL_LOG_ERR, " err = %d\n", err);
        goto bail;
    }
    cl_pool_destroy(&main_db_pool);
    CL_LOG(CL_LOG_NOTICE, "fdb uc db destroyed\n");

    err = fdb_uc_db_filter_deinit();
    if (err) {
        err = -EPERM;
        CL_LOG(CL_LOG_ERR, " err = %d\n", err);
        goto bail;
    }
    fdb_initiated = 0;
bail:
    return err;
}

/**
 * This function returns the pointer for first entry in DB
 *
 * @param[out] mac_item_pp - pointer to entry
 *
 * @return 0 operation completes successfully
 * @return   -EPERM  in case general error
 */
int
fdb_uc_db_get_first_record(fdb_uc_mac_entry_t **mac_pp)
{
    int err = 0;
    cl_map_item_t *map_item_p = NULL;
    *mac_pp = NULL;

    CHECK_FDB_INIT_DONE;

    if (CL_QMAP_HEAD(map_item_p, &fdb_map)) {
        /*LOG(LOG_DEBUG, "map_item_p key :0x%" PRIx64 "]\n", map_item_p->key);*/
        if (!CL_QMAP_END(map_item_p, &fdb_map)) {
            *mac_pp = CL_QMAP_PARENT_STRUCT(fdb_uc_mac_entry_t);
        }
    }
bail:
    return err;
}

/**
 * This function returns the pointer to next entry that comes
 * after the given one
 * @param[in] mac_item_p - input entry
 * @param[out] return_item_pp - pointer to entry
 *
 * @return 0  operation completes successfully
 * @return -EPERM   general error
 */
int
fdb_uc_db_next_record(fdb_uc_mac_entry_t *mac_entry_item_p,
                      fdb_uc_mac_entry_t **return_item_p)
{
    int err = 0;
    cl_map_item_t *map_item_p = NULL;

    CHECK_FDB_INIT_DONE;

    if (!mac_entry_item_p) {
        err = -EPERM;
        CL_LOG(CL_LOG_ERR, " pointer null err = %d\n", err);
        goto bail;
    }
    map_item_p = cl_qmap_next(&(mac_entry_item_p->map_item));
    if (CL_QMAP_END(map_item_p, &fdb_map)) {
        *return_item_p = NULL;
    }
    else {
        *return_item_p = CL_QMAP_PARENT_STRUCT(fdb_uc_mac_entry_t);
    }
    err = 0;

bail:
    return err;
}

/**
 * This function returns the pointer to the entry with given key
 *
 * @param[in] mac_key - mac DB key
 * @param[out] mac_item_pp - pointer to entry
 *
 * @return 0 operation completes successfully
 * @return   -ENOENT  in case general error
 */
int
fdb_uc_db_get_record_by_key(uint64_t mac_key,
                            fdb_uc_mac_entry_t **mac_entry_item_pp)
{
    int err = 0;
    cl_map_item_t *map_item_p = NULL;
    *mac_entry_item_pp = NULL;

    CHECK_FDB_INIT_DONE;

    if (CL_QMAP_KEY_EXISTS(&fdb_map, mac_key, map_item_p)) {
        *mac_entry_item_pp = CL_QMAP_PARENT_STRUCT(fdb_uc_mac_entry_t);
    }
    else {
        err = -ENOENT;
        /*CL_LOG(CL_LOG_WARN, "fdb uc get record by key [%" PRIx64 "] Entry not found\n",mac_key);*/
        goto bail;
    }
bail:
    return err;
}

/**
 * This function returns the pointer to next entry that comes
 * after the one with the given key
 *
 * @param[in] fdb_map - mac DB
 * @param[in] mac_key - mac DB key
 * @param[out] mac_item_pp - pointer to entry
 *
 * @return 0 operation completes successfully
 * @return   -ENOENT  in case  entry not exists
 */
int
fdb_uc_db_get_next_record_by_key(cl_qmap_t *fdb_map, uint64_t mac_key,
                                 fdb_uc_mac_entry_t **mac_entry_item_pp)
{
    int err = 0;
    cl_map_item_t *map_item_p = NULL;
    *mac_entry_item_pp = NULL;

    CHECK_FDB_INIT_DONE;

    if (CL_QMAP_NEXT_KEY_EXISTS(fdb_map, mac_key, map_item_p)) {
        *mac_entry_item_pp = CL_QMAP_PARENT_STRUCT(fdb_uc_mac_entry_t);
    }
    else {
        err = -ENOENT;
        CL_LOG(CL_LOG_WARN, " err = %d\n", err);
        goto bail;
    }
bail:
    return err;
}

/**
 * This function removes the entry from DB
 * @param[in] mac_item_p - input entry
 *
 * @return 0  operation completes successfully
 * @return -EPERM   general error
 */
int
fdb_uc_db_delete_record(fdb_uc_mac_entry_t *mac_entry_item_p)
{
    int err = 0;

    CHECK_FDB_INIT_DONE;

    if (!mac_entry_item_p) {
        CL_LOG(CL_LOG_ERR, " entry is null \n" );
        return -EPERM;
    }

    err = fdb_uc_db_filter_delete_entry(mac_entry_item_p);
    if (err) {
        CL_LOG(CL_LOG_ERR, " err = %d\n", err);
        goto bail;
    }
    cl_qmap_remove_item(&fdb_map, &mac_entry_item_p->map_item);
    cl_pool_put(&main_db_pool, mac_entry_item_p);
bail:
    return err;
}

/* This function adds a mac entry to the fdb

 * @param[in] mac_entry_p - input entry
 * @return 0  operation completes successfully
 * @return -ENOMEM not enough memory
 * @return -ENOENT general error
 */
int
fdb_uc_db_add_record(fdb_uc_mac_entry_t *mac_entry_p)
{
    int err = 0;
    fdb_uc_mac_entry_t *db_mac_entry_p = NULL;
    fdb_uc_mac_entry_t old_mac_entry;
    uint64_t db_key;
    struct   oes_fdb_uc_mac_addr_params *mac_params = NULL;
    int new_entry = TRUE;

    CHECK_FDB_INIT_DONE;

    if (!mac_entry_p) {
        err = -EPERM;
        CL_LOG(CL_LOG_ERR, " memory alloc fail, err = %d\n", err);
        goto bail;
    }
    mac_params = &mac_entry_p->mac_params;
    db_key =
        FDB_UC_CONVERT_MAC_VLAN_TO_KEY(mac_params->mac_addr, mac_params->vid);

    err = fdb_uc_db_get_record_by_key(db_key, &db_mac_entry_p);
    if (err == -ENOENT) { /* new entry */
        err = fdb_uc_db_create_record(&fdb_map, db_key, &db_mac_entry_p);
        if (err != 0) {
            CL_LOG(CL_LOG_ERR, " err = %d\n", err);
            goto bail;
        }
    }
    else {   /* modified entry */
        if (err != 0) {
            CL_LOG(CL_LOG_ERR, " err = %d\n", err);
            goto bail;
        }
        new_entry = FALSE;
    }
    if (!new_entry) {
        MEM_CPY(old_mac_entry.mac_params, db_mac_entry_p->mac_params);
    }

    err = set_record(db_mac_entry_p, mac_entry_p );
    if (err != 0) {
        CL_LOG(CL_LOG_ERR, " err = %d\n", err);
        goto bail;
    }
    /* add/update filters */
    if (new_entry) {
        err = fdb_uc_db_filter_add_entry(db_mac_entry_p);
        if (err != 0) {
            CL_LOG(CL_LOG_ERR, " err = %d\n", err);
            goto bail;
        }
    }
    else {  /* modified entry*/
        err = fdb_uc_db_filter_modified_entry(&old_mac_entry,
                                              db_mac_entry_p);
        if (err != 0) {
            CL_LOG(CL_LOG_ERR, " err = %d\n", err);
            goto bail;
        }
    }
bail:
    return err;
}

/* This function modifies a mac entry to the fdb

 * @param[in] new_mac_entry_p - pointer to new DB entry
 * @param[out] cur_mac_entry_p - pointer to current DB entry
 * @return 0  operation completes successfully
 * @return -ENOENT general error
 */
inline int
set_record(fdb_uc_mac_entry_t *cur_mac_entry_p,
           fdb_uc_mac_entry_t *new_mac_entry_p )
{
    int err = 0;

    MEM_CPY(cur_mac_entry_p->mac_params, new_mac_entry_p->mac_params);
    cur_mac_entry_p->cookie = new_mac_entry_p->cookie;
    cur_mac_entry_p->type = new_mac_entry_p->type;

    return err;
}


/**
 * This function creates ampty entry in DB with given key
 * @param[in] mac_key - mac DB key
 * @param[out] mac_item_pp - newly created entry
 *
 * @return 0 operation completes successfully
 * @return -EPERM   general error
 */
int
fdb_uc_db_create_record(cl_qmap_t *map, uint64_t mac_key,
                        fdb_uc_mac_entry_t **mac_entry_item_p)
{
    int err = 0;

    CHECK_FDB_INIT_DONE;

    if (!map) {
        err = -EPERM;
        CL_LOG(CL_LOG_ERR, " err = %d\n", err);
        goto bail;
    }

    *mac_entry_item_p = (fdb_uc_mac_entry_t *) cl_pool_get(&main_db_pool);
    if (!(*mac_entry_item_p)) {
        err = -ENOMEM;
        CL_LOG(CL_LOG_ERR, " mem. alloc. err = %d\n", err);
        goto bail;
    }

    MEM_CLR_P((*mac_entry_item_p));
    cl_qmap_insert(map, mac_key, &((*mac_entry_item_p)->map_item));
bail:
    return err;
}

/**
 * This function returns the pointer for first entry in DB by specific filter
 * @param[in] filter        - pointer to filters
 * @param[out] list_cookie  - cookie of specific filter
 * @param[out] mac_item_pp  - pointer to first entry or NULL
 *
 * @return 0 operation completes successfully
 * @return   -ENOENT in case of null pointer
 *  return   -EINVAL in case invalid filter
 *  return   -EPERM  in case wrong value
 */
int
fdb_uc_db_get_first_record_by_filter(const struct fdb_uc_key_filter * filter,
                                     void ** list_cookie,
                                     fdb_uc_mac_entry_t **mac_item_pp)
{
    return fdb_uc_db_filter_get_first_entry(filter, list_cookie, mac_item_pp);
}

/**
 * This function returns the pointer for next entry in DB by filter
 *
 * @param[in]  filter    - pointer to filters
 * @param[in]  list_cookie  - cookie of specific filter
 * @param[in]  mac_item_p - pointer to current entry
 * @param[out] mac_item_pp - pointer to next entry or NULL if mac_item_p is the last entry
 *
 * @return 0 -operation completes successfully
 * @return   -ENOENT in case of null pointer
 *  return   -EINVAL in case invalid filter
 *  return   -EPERM  in case general error
 */
int
fdb_uc_db_get_next_record_by_filter(const struct fdb_uc_key_filter * filter,
                                    void * list_cookie,
                                    fdb_uc_mac_entry_t * mac_item_p,
                                    fdb_uc_mac_entry_t **mac_item_pp)
{
    return fdb_uc_db_filter_get_next_entry(filter, list_cookie,
                                           mac_item_p, mac_item_pp);
}

/**
 * This function returns the size of the DB
 * @param[out] db_size - DB size
 *
 * @return 0  operation completes successfully
 */
int
fdb_uc_db_get_size(uint32_t *db_size)
{
    int err = 0;

    CHECK_FDB_INIT_DONE;

    if (!db_size) {
        CL_LOG(CL_LOG_ERR, " null pointer, err = %d\n", err);
        goto bail;
    }


    *db_size = cl_qmap_count(&fdb_map);

bail:
    return err;
}

/**
 * This function returns number of free items in the DB pool
 * @param[out]  count - number of free pools
 */
int
fdb_uc_db_get_free_pool_count(uint32_t *count)
{
    int err = 0;
    CHECK_FDB_INIT_DONE

    *count = cl_pool_count(&main_db_pool);
bail:
    return err;
}

