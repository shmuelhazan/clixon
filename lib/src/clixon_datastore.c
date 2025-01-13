 /*
 *
  ***** BEGIN LICENSE BLOCK *****
 
# Copyright (C) 2009-2016 Olof Hagsand and Benny Holmgren
# Copyright (C) 2017-2019 Olof Hagsand
# Copyright (C) 2020-2022 Olof Hagsand and Rubicon Communications, LLC(Netgate)

  This file is part of CLIXON.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  Alternatively, the contents of this file may be used under the terms of
  the GNU General Public License Version 3 or later (the "GPL"),
  in which case the provisions of the GPL are applicable instead
  of those above. If you wish to allow use of your version of this file only
  under the terms of the GPL, and not to allow others to
  use your version of this file under the terms of Apache License version 2, 
  indicate your decision by deleting the provisions above and replace them with
  the  notice and other provisions required by the GPL. If you do not delete
  the provisions above, a recipient may use your version of this file under
  the terms of any one of the Apache License version 2 or the GPL.

  ***** END LICENSE BLOCK *****

 * Clixon Datastore (XMLDB)
 * Saves Clixon data as clear-text XML (or JSON)
 */

#ifdef HAVE_CONFIG_H
#include "clixon_config.h" /* generated by config & autoconf */
#endif

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <libgen.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/param.h>

/* cligen */
#include <cligen/cligen.h>

/* clixon */
#include "clixon_queue.h"
#include "clixon_hash.h"
#include "clixon_handle.h"
#include "clixon_yang.h"
#include "clixon_xml.h"
#include "clixon_err.h"
#include "clixon_log.h"
#include "clixon_debug.h"
#include "clixon_string.h"
#include "clixon_file.h"
#include "clixon_yang_module.h"
#include "clixon_plugin.h"
#include "clixon_options.h"
#include "clixon_data.h"
#include "clixon_netconf_lib.h"
#include "clixon_xml_bind.h"
#include "clixon_xml_default.h"
#include "clixon_xml_io.h"
#include "clixon_json.h"
#include "clixon_datastore.h"
#include "clixon_datastore_write.h"
#include "clixon_datastore_read.h"

/*! Get xml database element including id, xml cache, empty on startup and dirty bit
 *
 * @param[in]  h    Clixon handle
 * @param[in]  db   Name of database
 * @retval     de   Database element
 * @retval     NULL None found
 */
db_elmnt *
clicon_db_elmnt_get(clixon_handle h,
                    const char   *db)
{
    clicon_hash_t *cdat = clicon_db_elmnt(h);
    void          *p;

    if ((p = clicon_hash_value(cdat, db, NULL)) != NULL)
        return (db_elmnt *)p;
    return NULL;
}

/*! Set xml database element including id, xml cache, empty on startup and dirty bit
 *
 * @param[in] h   Clixon handle
 * @param[in] db  Name of database
 * @param[in] de  Database element
 * @retval    0   OK
 * @retval   -1   Error
 * @see xmldb_disconnect
*/
int
clicon_db_elmnt_set(clixon_handle h,
                    const char   *db,
                    db_elmnt     *de)
{
    clicon_hash_t  *cdat = clicon_db_elmnt(h);

    if (clicon_hash_add(cdat, db, de, sizeof(*de))==NULL)
        return -1;
    return 0;
}

/*! Translate from symbolic database name to actual filename in file-system
 *
 * Internal function for explicit XMLDB_MULTI use or not
 * @param[in]   h        Clixon handle
 * @param[in]   db       Symbolic database name, eg "candidate", "running"
 * @param[in]   multi    Use multi/split datastores, see CLICON_XMLDB_MULTI
 * @param[out]  filename Filename. Unallocate after use with free()
 * @retval      0        OK
 * @retval     -1        Error
 */
static int
xmldb_db2file1(clixon_handle h,
               const char   *db,
               int           multi,
              char         **filename)
{
    int   retval = -1;
    cbuf *cb = NULL;
    char *dir;

    if ((cb = cbuf_new()) == NULL){
        clixon_err(OE_XML, errno, "cbuf_new");
        goto done;
    }
    if ((dir = clicon_xmldb_dir(h)) == NULL){
        clixon_err(OE_XML, errno, "CLICON_XMLDB_DIR not set");
        goto done;
    }
    /* Multi: write (root) to: <db>.d/0.xml
     * Classic: write to: <db>_db
     */
    if (multi)
        cprintf(cb, "%s/%s.d/0.xml", dir, db); /* Hardcoded to XML, XXX: JSON? */
    else
        cprintf(cb, "%s/%s_db", dir, db);
    if ((*filename = strdup4(cbuf_get(cb))) == NULL){
        clixon_err(OE_UNIX, errno, "strdup");
        goto done;
    }
    retval = 0;
 done:
    if (cb)
        cbuf_free(cb);
    return retval;
}

/*! Translate from symbolic database name to actual filename in file-system
 *
 * @param[in]   h        Clixon handle
 * @param[in]   db       Symbolic database name, eg "candidate", "running"
 * @param[out]  filename Filename. Unallocate after use with free()
 * @retval      0        OK
 * @retval     -1        Error
 * @note Could need a way to extend which databases exists, eg to register new.
 * The currently allowed databases are:
 *   candidate, tmp, running, result
 * The filename reside in CLICON_XMLDB_DIR option
 */
int
xmldb_db2file(clixon_handle  h,
              const char    *db,
              char         **filename)
{
    return xmldb_db2file1(h, db, clicon_option_bool(h, "CLICON_XMLDB_MULTI"), filename);
}

/*! Translate from symbolic database name to sub-directory of configure sub-files, no checks
 *
 * @param[in]   h       Clixon handle
 * @param[in]   db      Symbolic database name, eg "candidate", "running"
 * @param[out]  subdirp Sub-directory name. Unallocate after use with free()
 * @retval      0       OK
 * @retval     -1       Error
 * The dir is subdir to CLICON_XMLDB_DIR option
 * @see xmldb_db2file  For top-level config file
 */
int
xmldb_db2subdir(clixon_handle h,
                const char   *db,
                char        **subdirp)
{
    int   retval = -1;
    cbuf *cb = NULL;
    char *dir;
    char *subdir = NULL;

    if ((cb = cbuf_new()) == NULL){
        clixon_err(OE_XML, errno, "cbuf_new");
        goto done;
    }
    if ((dir = clicon_xmldb_dir(h)) == NULL){
        clixon_err(OE_XML, errno, "CLICON_XMLDB_DIR not set");
        goto done;
    }
    cprintf(cb, "%s/%s.d", dir, db);
    if ((subdir = strdup4(cbuf_get(cb))) == NULL){
        clixon_err(OE_UNIX, errno, "strdup");
        goto done;
    }
    *subdirp = subdir;
    subdir = NULL;
    retval = 0;
 done:
    if (subdir)
        free(subdir);
    if (cb)
        cbuf_free(cb);
    return retval;
}

/*! Connect to a datastore plugin, allocate resources to be used in API calls
 *
 * @param[in]  h    Clixon handle
 * @retval     0    OK
 * @retval    -1    Error
 */
int
xmldb_connect(clixon_handle h)
{
    return 0;
}

/*! Disconnect from a datastore plugin and deallocate resources
 *
 * @param[in]  handle  Disconect and deallocate from this handle
 * @retval     0       OK
 * @retval    -1    Error
 */
int
xmldb_disconnect(clixon_handle h)
{
    int       retval = -1;
    char    **keys = NULL;
    size_t    klen;
    int       i;
    db_elmnt *de;
    
    if (clicon_hash_keys(clicon_db_elmnt(h), &keys, &klen) < 0)
        goto done;
    for(i = 0; i < klen; i++) 
        if ((de = clicon_hash_value(clicon_db_elmnt(h), keys[i], NULL)) != NULL){
            if (de->de_xml){
                xml_free(de->de_xml);
                de->de_xml = NULL;
            }
        }
    retval = 0;
 done:
    if (keys)
        free(keys);
    return retval;
}

/*! Copy datastore from db1 to db2, both cache and datastore
 *
 * May include copying datastore directory structure
 * @param[in]  h     Clixon handle
 * @param[in]  from  Source datastore
 * @param[in]  to    Destination datastore
 * @retval     0     OK
 * @retval    -1     Error
  */
int 
xmldb_copy(clixon_handle h,
           const char   *from,
           const char   *to)
{
    int         retval = -1;
    char       *fromfile = NULL;
    char       *tofile = NULL;
    db_elmnt   *de1 = NULL; /* from */
    db_elmnt   *de2 = NULL; /* to */
    db_elmnt    de0 = {0,};
    cxobj      *x1 = NULL;  /* from */
    cxobj      *x2 = NULL;  /* to */
    char       *fromdir = NULL;
    char       *todir = NULL;
    char       *subdir = NULL;
    struct stat st = {0,};

    clixon_debug(CLIXON_DBG_DATASTORE, "%s %s", from, to);
    /* XXX lock */
    /* Copy in-memory cache */
    /* 1. "to" xml tree in x1 */
    if ((de1 = clicon_db_elmnt_get(h, from)) != NULL)
        x1 = de1->de_xml;
    if ((de2 = clicon_db_elmnt_get(h, to)) != NULL)
        x2 = de2->de_xml;
    if (x1 == NULL && x2 == NULL){
        /* do nothing */
    }
    else if (x1 == NULL){  /* free x2 and set to NULL */
        xml_free(x2);
        x2 = NULL;
    }
    else  if (x2 == NULL){ /* create x2 and copy from x1 */
        if ((x2 = xml_new(xml_name(x1), NULL, CX_ELMNT)) == NULL)
            goto done;
        xml_flag_set(x2, XML_FLAG_TOP);
        if (xml_copy(x1, x2) < 0) 
            goto done;
    }
    else{ /* copy x1 to x2 */
        xml_free(x2);
        if ((x2 = xml_new(xml_name(x1), NULL, CX_ELMNT)) == NULL)
            goto done;
        xml_flag_set(x2, XML_FLAG_TOP);
        if (xml_copy(x1, x2) < 0) 
            goto done;
    }
    /* always set cache although not strictly necessary in case 1
     * above, but logic gets complicated due to differences with
     * de and de->de_xml */
    if (de2)
        de0 = *de2;
    de0.de_xml = x2; /* The new tree */
    if (clicon_option_bool(h, "CLICON_XMLDB_MULTI")){
        if (xmldb_db2subdir(h, to, &subdir) < 0)
            goto done;
        if (stat(subdir, &st) < 0){
            if (mkdir(subdir, S_IRWXU|S_IRGRP|S_IWGRP|S_IROTH|S_IXOTH) < 0){
                clixon_err(OE_UNIX, errno, "mkdir(%s)", subdir);
                goto done;
            }
        }
    }
    clicon_db_elmnt_set(h, to, &de0);
    /* Copy the files themselves (above only in-memory cache)
     * Alt, dump the cache to file
     */
    if (xmldb_db2file(h, from, &fromfile) < 0)
        goto done;
    if (xmldb_db2file(h, to, &tofile) < 0)
        goto done;
    if (clicon_file_copy(fromfile, tofile) < 0)
        goto done;
    if (clicon_option_bool(h, "CLICON_XMLDB_MULTI")) {
        if (xmldb_db2subdir(h, from, &fromdir) < 0)
            goto done;
        if (xmldb_db2subdir(h, to, &todir) < 0)
            goto done;
        if (clicon_dir_copy(fromdir, todir) < 0)
            goto done;
    }
    retval = 0;
 done:
    clixon_debug(CLIXON_DBG_DATASTORE, "retval:%d", retval);
    if (subdir)
        free(subdir);
    if (fromdir)
        free(fromdir);
    if (todir)
        free(todir);
    if (fromfile)
        free(fromfile);
    if (tofile)
        free(tofile);
    return retval;
}

/*! Lock database
 *
 * @param[in]  h    Clixon handle
 * @param[in]  db   Database
 * @param[in]  id   Session id
 * @retval     0    OK
 * @retval    -1    Error
 */
int 
xmldb_lock(clixon_handle h, 
           const char   *db, 
           uint32_t      id)
{
    db_elmnt  *de = NULL;
    db_elmnt   de0 = {0,};

    if ((de = clicon_db_elmnt_get(h, db)) != NULL)
        de0 = *de;
    de0.de_id = id;
    gettimeofday(&de0.de_tv, NULL);
    clicon_db_elmnt_set(h, db, &de0);
    clixon_debug(CLIXON_DBG_DATASTORE, "%s: locked by %u",  db, id);
    return 0;
}

/*! Unlock database
 *
 * @param[in]  h   Clixon handle
 * @param[in]  db  Database
 * @retval     0   OK
 * @retval    -1   Error
 * Assume all sanity checks have been made
 */
int
xmldb_unlock(clixon_handle h,
             const char   *db)
{
    db_elmnt  *de = NULL;

    if ((de = clicon_db_elmnt_get(h, db)) != NULL){
        de->de_id = 0;
        memset(&de->de_tv, 0, sizeof(struct timeval));
        clicon_db_elmnt_set(h, db, de);

    }
    return 0;
}

/*! Unlock all databases locked by session-id (eg process dies) 
 *
 * @param[in]  h   Clixon handle
 * @param[in]  id  Session id
 * @retval     0   OK
 * @retval    -1   Error
 */
int
xmldb_unlock_all(clixon_handle h,
                 uint32_t      id)
{
    int       retval = -1;
    char    **keys = NULL;
    size_t    klen;
    int       i;
    db_elmnt *de;

    /* get all db:s */
    if (clicon_hash_keys(clicon_db_elmnt(h), &keys, &klen) < 0)
        goto done;
    /* Identify the ones locked by client id */
    for (i = 0; i < klen; i++) {
        if ((de = clicon_db_elmnt_get(h, keys[i])) != NULL &&
            de->de_id == id){
            de->de_id = 0;
            memset(&de->de_tv, 0, sizeof(struct timeval));
            clicon_db_elmnt_set(h, keys[i], de);
        }
    }
    retval = 0;
 done:
    if (keys)
        free(keys);
    return retval;
}

/*! Check if database is locked
 *
 * @param[in] h   Clixon handle
 * @param[in] db  Database
 * @retval   >0   Session id of locker
 * @retval    0   Not locked
 * @retval   -1   Error
 */
uint32_t
xmldb_islocked(clixon_handle h,
               const char   *db)
{
    db_elmnt  *de;

    if ((de = clicon_db_elmnt_get(h, db)) == NULL)
        return 0;
    return de->de_id;
}

/*! Get timestamp of when database was locked
 *
 * @param[in]  h   Clixon handle
 * @param[in]  db  Database
 * @param[out] tv  Timestamp
 * @retval     0   OK
 * @retval    -1   No timestamp / not locked
 */
int
xmldb_lock_timestamp(clixon_handle   h,
                     const char     *db,
                     struct timeval *tv)
{
    db_elmnt  *de;

    if ((de = clicon_db_elmnt_get(h, db)) == NULL)
        return -1;
    memcpy(tv, &de->de_tv, sizeof(*tv));
    return 0;
}

/*! Check if db exists or is empty
 *
 * @param[in]  h   Clixon handle
 * @param[in]  db  Database
 * @retval     1   Yes it exists
 * @retval     0   No it does not exist
 * @retval    -1   Error
 * @note  An empty datastore is treated as not existent so that a backend after dropping priviliges can re-create it
 */
int
xmldb_exists(clixon_handle h,
             const char   *db)
{
    int                 retval = -1;
    char               *filename = NULL;
    struct stat         sb;

    clixon_debug(CLIXON_DBG_DATASTORE | CLIXON_DBG_DETAIL, "%s", db);
    if (xmldb_db2file(h, db, &filename) < 0)
        goto done;
    if (lstat(filename, &sb) < 0)
        retval = 0;
    else{
        if (sb.st_size == 0)
            retval = 0;
        else
            retval = 1;
    }
 done:
    clixon_debug(CLIXON_DBG_DATASTORE | CLIXON_DBG_DETAIL, "retval:%d", retval);
    if (filename)
        free(filename);
    return retval;
}

/*! Clear database cache if any for mem/size optimization only, not file itself
 *
 * @param[in]  h   Clixon handle
 * @param[in]  db  Database
 * @retval     0   OK
 * @retval    -1   Error
 */
int
xmldb_clear(clixon_handle h,
            const char   *db)
{
    cxobj    *xt = NULL;
    db_elmnt *de = NULL;

    if ((de = clicon_db_elmnt_get(h, db)) != NULL){
        if ((xt = de->de_xml) != NULL){
            xml_free(xt);
            de->de_xml = NULL;
        }
        de->de_modified = 0;
        de->de_id = 0;
        memset(&de->de_tv, 0, sizeof(struct timeval));
    }
    return 0;
}

/*! Delete database, clear cache if any. Remove file and dir
 *
 * @param[in]  h   Clixon handle
 * @param[in]  db  Database
 * @retval     0   OK
 * @retval    -1   Error
 * @note Datastores / dirs are not actually deleted so that a backend after dropping priviliges
 *       can re-create them
 */
int
xmldb_delete(clixon_handle h,
             const char   *db)
{
    int            retval = -1;
    char          *filename = NULL;
    struct stat    st = {0,};
    cbuf          *cb = NULL;
    char          *subdir = NULL;
    struct dirent *dp = NULL;
    int            ndp;
    int            i;
    char          *regexp = NULL;

    clixon_debug(CLIXON_DBG_DATASTORE | CLIXON_DBG_DETAIL, "%s", db);
    if (xmldb_clear(h, db) < 0)
        goto done;
    if (xmldb_db2file(h, db, &filename) < 0)
        goto done;
    if (lstat(filename, &st) == 0)
        if (truncate(filename, 0) < 0){
            clixon_err(OE_DB, errno, "truncate %s", filename);
            goto done;
        }
    if (clicon_option_bool(h, "CLICON_XMLDB_MULTI")){
        if (xmldb_db2subdir(h, db, &subdir) < 0)
            goto done;
        if (stat(subdir, &st) == 0){
            if ((ndp = clicon_file_dirent(subdir, &dp, regexp, S_IFREG)) < 0)
                goto done;
            if ((cb = cbuf_new()) == NULL){
                clixon_err(OE_XML, errno, "cbuf_new");
                goto done;
            }
            for (i = 0; i < ndp; i++){
                cbuf_reset(cb);
                cprintf(cb, "%s/%s", subdir, dp[i].d_name);
                if (truncate(cbuf_get(cb), 0) < 0){
                    clixon_err(OE_DB, errno, "truncate %s", filename);
                    goto done;
                }
            }
        }
    }
    retval = 0;
 done:
    clixon_debug(CLIXON_DBG_DATASTORE | CLIXON_DBG_DETAIL, "retval:%d", retval);
    if (dp)
        free(dp);
    if (cb)
        cbuf_free(cb);
    if (subdir)
        free(subdir);
    if (filename)
        free(filename);
    return retval;
}

/*! Create a database. Open database for writing.
 *
 * @param[in]  h   Clixon handle
 * @param[in]  db  Database
 * @retval     0   OK
 * @retval    -1   Error
 */
int
xmldb_create(clixon_handle h,
             const char   *db)
{
    int         retval = -1;
    char       *filename = NULL;
    int         fd = -1;
    db_elmnt   *de = NULL;
    cxobj      *xt = NULL;
    char       *subdir = NULL;
    struct stat st = {0,};

    clixon_debug(CLIXON_DBG_DATASTORE | CLIXON_DBG_DETAIL, "%s", db);
    if ((de = clicon_db_elmnt_get(h, db)) != NULL){
        if ((xt = de->de_xml) != NULL){
            xml_free(xt);
            de->de_xml = NULL;
        }
    }
    if (clicon_option_bool(h, "CLICON_XMLDB_MULTI")){
        if (xmldb_db2subdir(h, db, &subdir) < 0)
            goto done;
        if (stat(subdir, &st) < 0){
            if (mkdir(subdir, S_IRWXU|S_IRGRP|S_IWGRP|S_IXGRP|S_IROTH|S_IXOTH) < 0){
                clixon_err(OE_UNIX, errno, "mkdir(%s)", subdir);
                goto done;
            }
        }
    }
    if (xmldb_db2file(h, db, &filename) < 0)
        goto done;
    if ((fd = open(filename, O_CREAT|O_WRONLY, S_IRWXU)) == -1) {
        clixon_err(OE_UNIX, errno, "open(%s)", filename);
        goto done;
    }
    retval = 0;
 done:
    clixon_debug(CLIXON_DBG_DATASTORE | CLIXON_DBG_DETAIL, "retval:%d", retval);
    if (subdir)
        free(subdir);
    if (filename)
        free(filename);
    if (fd != -1)
        close(fd);
    return retval;
}

/*! Create an XML database. If it exists already, delete it before creating
 *
 * Utility function.
 * @param[in]  h   Clixon handle
 * @param[in]  db  Symbolic database name, eg "candidate", "running"
 * @retval     0   OK
 * @retval    -1   Error
 */
int
xmldb_db_reset(clixon_handle h,
               const char   *db)
{
    if (xmldb_exists(h, db) == 1){
        if (xmldb_delete(h, db) != 0 && errno != ENOENT)
            return -1;
    }
    if (xmldb_create(h, db) < 0)
        return -1;
    return 0;
}

/*! Get datastore XML cache
 *
 * @param[in]  h    Clixon handle
 * @param[in]  db   Database name
 * @retval     xml  XML cached tree or NULL
 * @see xmldb_get_cache  Read from store if miss
 */
cxobj *
xmldb_cache_get(clixon_handle h,
                const char   *db)
{
    db_elmnt *de;

    if ((de = clicon_db_elmnt_get(h, db)) == NULL)
        return NULL;
    return de->de_xml;
}

/*! Get modified flag from datastore
 *
 * @param[in]  h     Clixon handle
 * @param[in]  db    Database name
 * @retval     1     Db is modified
 * @retval     0     Db is not modified
 * @retval    -1     Error (datastore does not exist)
 * @note This only makes sense for "candidate", see RFC 6241 Sec 7.5
 * @note This only works if db cache is used,...
 */
int
xmldb_modified_get(clixon_handle h,
                   const char   *db)
{
    db_elmnt *de;

    if ((de = clicon_db_elmnt_get(h, db)) == NULL){
        clixon_err(OE_CFG, EFAULT, "datastore %s does not exist", db);
        return -1;
    }
    return de->de_modified;
}

/*! Set modified flag from datastore
 *
 * @param[in]  h     Clixon handle
 * @param[in]  db    Database name
 * @param[in]  value 0 or 1
 * @retval     0     OK
 * @retval    -1     Error (datastore does not exist)
 * @note This only makes sense for "candidate", see RFC 6241 Sec 7.5
 */
int
xmldb_modified_set(clixon_handle h,
                   const char   *db,
                   int           value)
{
    db_elmnt *de;

    if ((de = clicon_db_elmnt_get(h, db)) == NULL){
        clixon_err(OE_CFG, EFAULT, "datastore %s does not exist", db);
        return -1;
    }
    de->de_modified = value;
    return 0;
}

/*! Get empty flag from datastore (the datastore was empty ON LOAD)
 *
 * @param[in]  h     Clixon handle
 * @param[in]  db    Database name
 * @retval     1     Db was empty on load
 * @retval     0     Db was not empty on load
 * @retval    -1     Error (datastore does not exist)
 */
int
xmldb_empty_get(clixon_handle h,
                const char   *db)
{
    db_elmnt *de;

    if ((de = clicon_db_elmnt_get(h, db)) == NULL){
        clixon_err(OE_CFG, EFAULT, "datastore %s does not exist", db);
        return -1;
    }
    return de->de_empty;
}

/*! Set empty flag from datastore (the datastore was empty ON LOAD)
 *
 * @param[in]  h     Clixon handle
 * @param[in]  db    Database name
 * @param[in]  value 0 or 1
 * @retval     0     OK
 * @retval    -1     Error (datastore does not exist)
 */
int
xmldb_empty_set(clixon_handle h,
                const char   *db,
                int           value)
{
    db_elmnt *de;

    if ((de = clicon_db_elmnt_get(h, db)) == NULL){
        clixon_err(OE_CFG, EFAULT, "datastore %s does not exist", db);
        return -1;
    }
    de->de_empty = value;
    return 0;
}

/*! Get volatile flag of datastore cache
 *
 * Whether to sync cache to disk on every update (ie xmldb_put)
 * @param[in]  h     Clixon handle
 * @param[in]  db    Database name
 * @retval     1     Db was empty on load
 * @retval     0     Db was not empty on load
 * @retval    -1     Error (datastore does not exist)
 */
int
xmldb_volatile_get(clixon_handle h,
                   const char   *db)
{
    db_elmnt *de;

    if ((de = clicon_db_elmnt_get(h, db)) == NULL){
        clixon_err(OE_CFG, EFAULT, "datastore %s does not exist", db);
        return -1;
    }
    return de->de_volatile;
}

/*! Set datastore status of datastore cache
 *
 * Whether to sync cache to disk on every update (ie xmldb_put)
 * @param[in]  h     Clixon handle
 * @param[in]  db    Database name
 * @param[in]  value 0 or 1
 * @retval     0     OK
 * @retval    -1     Error (datastore does not exist)
 */
int
xmldb_volatile_set(clixon_handle h,
                   const char   *db,
                   int           value)
{
    db_elmnt *de;

    if ((de = clicon_db_elmnt_get(h, db)) == NULL){
        clixon_err(OE_CFG, EFAULT, "datastore %s does not exist", db);
        return -1;
    }
    de->de_volatile = value;
    return 0;
}

/* Print the datastore meta-info to file
 */
int
xmldb_print(clixon_handle h,
            FILE         *f)
{
    int       retval = -1;
    db_elmnt *de = NULL;
    char    **keys = NULL;
    size_t    klen;
    int       i;

    if (clicon_hash_keys(clicon_db_elmnt(h), &keys, &klen) < 0)
        goto done;
    for (i = 0; i < klen; i++){
        /* XXX name */
        if ((de = clicon_db_elmnt_get(h, keys[i])) == NULL)
            continue;
        fprintf(f, "Datastore:  %s\n", keys[i]);
        fprintf(f, "  Session:  %u\n", de->de_id);
        fprintf(f, "  XML:      %p\n", de->de_xml);
        fprintf(f, "  Modified: %d\n", de->de_modified);
        fprintf(f, "  Empty:    %d\n", de->de_empty);
    }
    retval = 0;
 done:
    if (keys)
        free(keys);
    return retval;
}

/*! Rename an XML database
 *
 * @param[in]  h        Clixon handle
 * @param[in]  db       Database name
 * @param[in]  newdb    New Database name; if NULL, then same as old
 * @param[in]  suffix   Suffix to append to new database name
 * @retval     0        OK
 * @retval    -1        Error
 * @note if newdb and suffix are null, OK is returned as it is a no-op
 */
int
xmldb_rename(clixon_handle h,
             const char    *db,
             const char    *newdb,
             const char    *suffix)
{
    int    retval = -1;
    char  *old;
    char  *fname = NULL;
    cbuf  *cb = NULL;

    if ((xmldb_db2file(h, db, &old)) < 0)
        goto done;
    if (newdb == NULL && suffix == NULL)        // no-op
        goto done;
    if ((cb = cbuf_new()) == NULL){
        clixon_err(OE_XML, errno, "cbuf_new");
        goto done;
    }
    cprintf(cb, "%s", newdb == NULL ? old : newdb);
    if (suffix)
        cprintf(cb, "%s", suffix);
    fname = cbuf_get(cb);
    if ((rename(old, fname)) < 0) {
        clixon_err(OE_UNIX, errno, "rename: %s", strerror(errno));
        goto done;
    };
    retval = 0;
 done:
    if (cb)
        cbuf_free(cb);
    if (old)
        free(old);
    return retval;
}

/*! Given a datastore, populate its cache with yang binding and default values
 *
 * @param[in]  h      Clixon handle
 * @param[in]  db     Name of database to search in (filename including dir path
 * @retval     1      OK
 * @retval     0      YANG assigment and default assignment not made
 * @retval    -1      General error, check specific clicon_errno, clicon_suberrno
 * @see xmldb_get_cache  Consider using this instead
 */
int
xmldb_populate(clixon_handle h,
               const char   *db)
{
    int        retval = -1;
    cxobj     *x;
    yang_stmt *yspec;
    int        ret;

    if ((x = xmldb_cache_get(h, db)) == NULL){
        clixon_err(OE_XML, 0, "XML cache not found");
        goto done;
    }
    yspec = clicon_dbspec_yang(h);
    if ((ret = xml_bind_yang(h, x, YB_MODULE, yspec, NULL)) < 0)
        goto done;
    if (ret == 1){
        /* Add default global values (to make xpath below include defaults) */
        if (xml_global_defaults(h, x, NULL, "/", yspec, 0) < 0)
            goto done;
        /* Add default recursive values */
        if (xml_default_recurse(x, 0, 0) < 0)
            goto done;
    }
    retval = ret;
 done:
    return retval;
}

/*! Upgrade datastore from original non-multi to multi/split mode
 *
 * This is for upgrading the datastores on startup using CLICON_XMLDB_MULTI
 * (1) If <db>.d/0.xml does not exist AND
 * (2) <db>_db does exist and is a regular file
 * (3) THEN copy file from <db>_db to <db>.d/0.xml
 * @param[in]  h   Clixon handle
 * @param[in]  db  Datastore
 */
int
xmldb_multi_upgrade(clixon_handle h,
                    const char   *db)
{
    int         retval = -1;
    char       *fromfile = NULL;
    char       *tofile = NULL;
    struct stat st = {0,};

    if (xmldb_db2file1(h, db, 1, &tofile) < 0)
        goto done;
    if (stat(tofile, &st) < 0 && errno == ENOENT) {
        /* db.d/0.xml does not exist */
        if (xmldb_create(h, db) < 0)
            goto done;
        if (xmldb_db2file1(h, db, 0, &fromfile) < 0)
            goto done;
        if (stat(fromfile, &st) == 0 && S_ISREG(st.st_mode)){
            if (clicon_file_copy(fromfile, tofile) < 0)
                goto done;
        }
    }
    retval = 0;
 done:
    if (fromfile)
        free(fromfile);
    if (tofile)
        free(tofile);
    return retval;
}

/*! Get system-only config data by calling user callback
 *
 * @param[in]     h       Clixon handle
 * @param[in]     xpath   XPath selection, may be used to filter early
 * @param[in]     nsc     XML Namespace context for xpath
 * @param[in,out] xret    Existing XML tree, merge x into this, or rpc-error
 * @retval        1       OK
 * @retval        0       Statedata callback failed (error in xret)
 * @retval       -1       Error (fatal)
 */
int
xmldb_system_only_config(clixon_handle h,
                         const char   *xpath,
                         cvec         *nsc,
                         cxobj       **xret)
{
    int        retval = -1;
    yang_stmt *yspec;
    int        ret;

    clixon_debug(CLIXON_DBG_BACKEND, "");
    if ((yspec = clicon_dbspec_yang(h)) == NULL){
        clixon_err(OE_YANG, ENOENT, "No yang spec");
        goto done;
    }
    if ((ret = clixon_plugin_system_only_all(h, yspec, nsc, (char*)xpath, xret)) < 0)
        goto done;
    if (ret == 0)
        goto fail;
    retval = 1; /* OK */
 done:
    return retval;
 fail:
    retval = 0;
    goto done;
}
