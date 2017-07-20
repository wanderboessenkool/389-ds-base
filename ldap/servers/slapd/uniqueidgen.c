/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* uniqueidgen.c  - implementation for uniqueID generator */

#include <string.h>
#include <sys/types.h>
#include <sys/time.h>

/* What platforms actually need this? */
#ifdef HAVE_SYS_SYSINFO_H
#include <sys/sysinfo.h>
#endif

#include <sys/utsname.h>
#include "nspr.h"
#include "slap.h"
#include "uuid.h"

#define MODULE "uniqueid generator"

/* converts from guid -> UniqueID */
/* static void uuid2UniqueID (const guid_t *uuid, Slapi_UniqueID *uId); */
/* converts from UniqueID -> guid */
/* static void uniqueID2uuid (const Slapi_UniqueID *uId, guid_t *uuid); */
/* validates directory */
static int validDir(const char *configDir);

/* Function:    uniqueIDGenInit
   Description: this function initializes the generator
   Parameters:  configDir - directory in which generators state is stored
                configDN - DIT entry with state information
                mtGen - indicates whether multiple threads will use generator
   Return:      UID_SUCCESS if function succeeds
                UID_BADDATA if invalif directory is passed
                UID_SYSTEM_ERROR if any other failure occurs
*/
int
uniqueIDGenInit(const char *configDir, const Slapi_DN *configDN, PRBool mtGen)
{
    int rt;
    if ((configDN == NULL && (configDir == NULL || !validDir(configDir))) ||
        (configDN && configDir)) {
        slapi_log_err(SLAPI_LOG_ERR, MODULE, "uniqueIDGenInit: invalid arguments\n");

        return UID_BADDATA;
    }

    rt = uuid_init(configDir, configDN, mtGen);

    if (rt == UUID_SUCCESS)
        return UID_SUCCESS;
    else {
        slapi_log_err(SLAPI_LOG_ERR, MODULE, "uniqueIDGenInit: "
                                             "generator initialization failed\n");
        return UID_SYSTEM_ERROR;
    }
}

/* Function:    uniqueIDGenCleanup
   Description: cleanup
   Parameters:  none
   Return:      none
*/
void
uniqueIDGenCleanup()
{
    uuid_cleanup();
}

/* Function:    slapi_uniqueIDGenerate
   Description: this function generates UniqueID; exposed to the plugins.
   Parameters:  uId - structure in which new id will be return
   Return:      UID_SUCCESS, if operation is successful
                UID_BADDATA, if null pointer is passed to the function
                UID_SYSTEM_ERROR, if update to persistent storage failed
*/

int
slapi_uniqueIDGenerate(Slapi_UniqueID *uId)
{
    int rt;

    if (uId == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, MODULE, "uniqueIDGenerate: "
                                             "NULL parameter is passed to the function.\n");
        return UID_BADDATA;
    }

    rt = uuid_create(uId);
    if (rt != UUID_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, MODULE, "uniqueIDGenerate: "
                                             "id generation failed.\n");
        return UID_SYSTEM_ERROR;
    }
    return UID_SUCCESS;
}

/* Function:    slapi_uniqueIDGenerateString
   Description: this function generates uniqueid an returns it as a string
                This function returns the data in the format generated by
                slapi_uniqueIDFormat.
   Parameters:  uId - buffer to receive the ID.    Caller is responsible for
                freeing uId buffer.
   Return:      UID_SUCCESS if function succeeds;
                UID_BADDATA if invalid pointer passed to the function;
                UID_MEMORY_ERROR if malloc fails;
                UID_SYSTEM_ERROR update to persistent storage failed.
*/

int
slapi_uniqueIDGenerateString(char **uId)
{
    Slapi_UniqueID uIdTemp;
    int rc;

    rc = slapi_uniqueIDGenerate(&uIdTemp);

    if (rc != UID_SUCCESS)
        return rc;

    rc = slapi_uniqueIDFormat(&uIdTemp, uId);

    return rc;
}

/* Function:    slapi_uniqueIDGenerateFromName
   Description:    this function generates an id from name. See uuid
                draft for more details. This function is thread safe.
   Parameters:    uId        - generated id
                uIDBase - uid used for generation to distinguish different
                name - buffer containing name from which to generate the id
                namelen - length of the name buffer
                name spaces
   Return:        UID_SUCCESS if function succeeds
                UID_BADDATA if invalid argument is passed to the
                function.
*/

int
slapi_uniqueIDGenerateFromName(Slapi_UniqueID *uId, const Slapi_UniqueID *uIdBase, const void *name, int namelen)
{
    if (uId == NULL || uIdBase == NULL || name == NULL || namelen <= 0) {
        slapi_log_err(SLAPI_LOG_ERR, MODULE, "uniqueIDGenerateMT: "
                                             "invalid parameter is passed to the function.\n");
        return UID_BADDATA;
    }

    uuid_create_from_name(uId, *uIdBase, name, namelen);

    return UID_SUCCESS;
}

/* Function:    slapi_uniqueIDGenerateFromName
   Description:    this function generates an id from a name and returns
                it in the string format. See uuid draft for more
                details. This function can be used in both a
                singlethreaded and a multithreaded environments.
   Parameters:    uId        - generated id in string form
                uIDBase - uid used for generation to distinguish among
                different name spaces in string form; NULL means to use
                empty id as the base.
                name - buffer containing name from which to generate the id
                namelen - length of the name buffer
   Return:        UID_SUCCESS if function succeeds
                UID_BADDATA if invalid argument is passed to the
                function.
*/

int
slapi_uniqueIDGenerateFromNameString(char **uId,
                                     const char *uIdBase,
                                     const void *name,
                                     int namelen)
{
    int rc;
    Slapi_UniqueID idBase = {0};
    Slapi_UniqueID idGen = {0};

    /* just use Id of all 0 as base id */
    if (uIdBase != NULL) {
        rc = slapi_uniqueIDScan(&idBase, uIdBase);
        if (rc != UID_SUCCESS) {
            return rc;
        }
    }

    rc = slapi_uniqueIDGenerateFromName(&idGen, &idBase, name, namelen);
    if (rc != UID_SUCCESS) {
        return rc;
    }

    rc = slapi_uniqueIDFormat(&idGen, uId);

    return rc;
}

/* helper fumctions */

static int
validDir(const char *configDir)
{
    PRDir *dir;

    /* empty string means this directory */
    if (strlen(configDir) == 0)
        return 1;
    dir = PR_OpenDir(configDir);
    if (dir) {
        PR_CloseDir(dir);
        return 1;
    }

    return 0;
}
