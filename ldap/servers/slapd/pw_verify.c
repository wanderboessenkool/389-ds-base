/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2016 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

/*
 * pw_verify.c
 *
 * This contains helpers that take a DN and a password credential from a simple
 * bind or SASL PLAIN/LOGIN. It steps through the raw credential and returns
 *
 * SLAPI_BIND_SUCCESS : The credentials are correct for the DN.
 * SLAPI_BIND_ANONYMOUS : The credentials are anonymous.
 * SLAPI_BIND_REFERRAL : The DN provided is going to be a referal, go away!
 * LDAP_INVALID_CREDENTIALS : The credentials are incorrect for this DN, or not
 *                            enough material was provided.
 * LDAP_OPERATIONS_ERROR : Something went wrong during verification.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "slap.h"
#include "fe.h"

int
pw_verify_root_dn(const char *dn, const Slapi_Value *cred)
{
    int result = LDAP_OPERATIONS_ERROR;
    char *root_pw = config_get_rootpw();
    if (root_pw != NULL && slapi_dn_isroot(dn)) {
        /* Now build a slapi value to give to slapi_pw_find_sv */
        Slapi_Value root_dn_pw_bval;
        slapi_value_init_string(&root_dn_pw_bval, root_pw);
        Slapi_Value *root_dn_pw_vals[] = {&root_dn_pw_bval, NULL};
        result = slapi_pw_find_sv(root_dn_pw_vals, cred);
        value_done(&root_dn_pw_bval);
    }
    slapi_ch_free_string(&root_pw);
    return result;
}

/*
 * This will work out which backend is needed, and then work from there.
 * You must set the SLAPI_BIND_TARGET_SDN, and SLAPI_BIND_CREDENTIALS to
 * the pblock for this to operate correctly.
 *
 * In the future, this will use the credentials and do mfa.
 *
 * All other results, it's already released.
 */
int
pw_verify_be_dn(Slapi_PBlock *pb, Slapi_Entry **referral)
{
    int rc = 0;
    Slapi_Backend *be = NULL;

    if (slapi_mapping_tree_select(pb, &be, referral, NULL, 0) != LDAP_SUCCESS) {
        return SLAPI_BIND_NO_BACKEND;
    }

    if (*referral) {
        slapi_be_Unlock(be);
        return SLAPI_BIND_REFERRAL;
    }

    slapi_pblock_set(pb, SLAPI_BACKEND, be);
    /* Put the credentials into the pb */
    if (be->be_bind == NULL) {
        /* Selected backend doesn't support binds! */
        slapi_be_Unlock(be);
        return LDAP_OPERATIONS_ERROR;
    }
    slapi_pblock_set(pb, SLAPI_PLUGIN, be->be_database);
    /* Make sure the result handlers are setup */
    set_db_default_result_handlers(pb);
    /* now take the dn, and check it */
    rc = (*be->be_bind)(pb);
    slapi_be_Unlock(be);

    return rc;
}

/*
 * Resolve the dn we have been requested to bind with and verify it's
 * valid, and has a backend.
 *
 * We are checking:
 * * is this anonymous?
 * * is this the rootdn?
 * * is this a real dn, which associates to a real backend.
 *
 * This is used in SASL autobinds, so we need to handle this validation.
 */

int
pw_validate_be_dn(Slapi_PBlock *pb, Slapi_Entry **referral)
{
    Slapi_Backend *be = NULL;
    Slapi_DN *pb_sdn;
    struct berval *cred;
    ber_tag_t method;


    slapi_pblock_get(pb, SLAPI_BIND_TARGET_SDN, &pb_sdn);
    slapi_pblock_get(pb, SLAPI_BIND_CREDENTIALS, &cred);
    slapi_pblock_get(pb, SLAPI_BIND_METHOD, &method);

    if (pb_sdn != NULL || cred != NULL) {
        return LDAP_OPERATIONS_ERROR;
    }

    if (*referral) {
        return SLAPI_BIND_REFERRAL;
    }

    /* We need a slapi_sdn_isanon? */
    if (method == LDAP_AUTH_SIMPLE && (cred == NULL || cred->bv_len == 0)) {
        return SLAPI_BIND_ANONYMOUS;
    }

    if (slapi_sdn_isroot(pb_sdn)) {
        /* This is a real identity */
        return SLAPI_BIND_SUCCESS;
    }

    if (slapi_mapping_tree_select(pb, &be, referral, NULL, 0) != LDAP_SUCCESS) {
        return SLAPI_BIND_NO_BACKEND;
    }
    slapi_be_Unlock(be);

    slapi_pblock_set(pb, SLAPI_BACKEND, be);
    slapi_pblock_set(pb, SLAPI_PLUGIN, be->be_database);
    /* Make sure the result handlers are setup */
    set_db_default_result_handlers(pb);

    /* The backend associated with this identity is real. */

    return SLAPI_BIND_SUCCESS;
}
