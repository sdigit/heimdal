/*-
 * Copyright (c) 2005 Doug Rabson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD: src/lib/libgssapi/gss_krb5.c,v 1.1 2005/12/29 14:40:20 dfr Exp $
 */

#include "mech_locl.h"
RCSID("$Id$");

#include <krb5.h>
#include <roken.h>


OM_uint32
gss_krb5_copy_ccache(OM_uint32 *minor_status,
		     gss_cred_id_t cred,
		     krb5_ccache out)
{
    gss_buffer_set_t data_set = GSS_C_NO_BUFFER_SET;
    krb5_context context;
    krb5_error_code kret;
    krb5_ccache id;
    OM_uint32 ret;
    char *str;

    ret = gss_inquire_cred_by_oid(minor_status,
				  cred,
				  GSS_KRB5_COPY_CCACHE_X,
				  &data_set);
    if (ret)
	return ret;

    if (data_set == GSS_C_NO_BUFFER_SET || data_set->count != 1) {
	gss_release_buffer_set(minor_status, &data_set);
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }

    kret = krb5_init_context(&context);
    if (kret) {
	*minor_status = kret;
	gss_release_buffer_set(minor_status, &data_set);
	return GSS_S_FAILURE;
    }

    kret = asprintf(&str, "%.*s", (int)data_set->elements[0].length,
		    (char *)data_set->elements[0].value);
    gss_release_buffer_set(minor_status, &data_set);
    if (kret == -1) {
	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }

    kret = krb5_cc_resolve(context, str, &id);
    free(str);
    if (kret) {
	*minor_status = kret;
	return GSS_S_FAILURE;
    }

    kret = krb5_cc_copy_cache(context, id, out);
    krb5_cc_close(context, id);
    krb5_free_context(context);
    if (kret) {
	*minor_status = kret;
	return GSS_S_FAILURE;
    }

    return ret;
}

OM_uint32
gss_krb5_import_cred(OM_uint32 *minor_status,
		     krb5_ccache id,
		     krb5_principal keytab_principal,
		     krb5_keytab keytab,
		     gss_cred_id_t *cred)
{
    gss_buffer_desc buffer;
    OM_uint32 major_status;
    krb5_context context;
    krb5_error_code ret;
    krb5_storage *sp;
    krb5_data data;
    char *str;

    *cred = GSS_C_NO_CREDENTIAL;

    ret = krb5_init_context(&context);
    if (ret) {
	*minor_status = ret;
	return GSS_S_FAILURE;
    }

    sp = krb5_storage_emem();
    if (sp == NULL) {
	*minor_status = ENOMEM;
	major_status = GSS_S_FAILURE;
	goto out;
    }

    if (id) {
	ret = krb5_cc_get_full_name(context, id, &str);
	if (ret == 0) {
	    ret = krb5_store_string(sp, str);
	    free(str);
	}
    } else
	ret = krb5_store_string(sp, "");
    if (ret) {
	*minor_status = ret;
	major_status = GSS_S_FAILURE;
	goto out;
    }

    if (keytab_principal) {
	ret = krb5_unparse_name(context, keytab_principal, &str);
	if (ret == 0) {
	    ret = krb5_store_string(sp, str);
	    free(str);
	}
    } else
	krb5_store_string(sp, "");
    if (ret) {
	*minor_status = ret;
	major_status = GSS_S_FAILURE;
	goto out;
    }


    if (keytab) {
	ret = krb5_kt_get_full_name(context, keytab, &str);
	if (ret == 0) {
	    ret = krb5_store_string(sp, str);
	    free(str);
	}
    } else
	krb5_store_string(sp, "");
    if (ret) {
	*minor_status = ret;
	major_status = GSS_S_FAILURE;
	goto out;
    }

    krb5_storage_to_data(sp, &data);

    buffer.value = data.data;
    buffer.length = data.length;
    
    major_status = gss_set_cred_option(minor_status,
				       cred,
				       GSS_KRB5_IMPORT_CRED_X,
				       &buffer);
    krb5_data_free(&data);
out:
    if (sp)
	krb5_storage_free(sp);
    krb5_free_context(context);
    return major_status;
}

OM_uint32
gsskrb5_register_acceptor_identity(const char *identity)
{
        struct _gss_mech_switch	*m;
	gss_buffer_desc buffer;
	OM_uint32 junk;

	_gss_load_mech();

	buffer.value = rk_UNCONST(identity);
	buffer.length = strlen(identity);

	SLIST_FOREACH(m, &_gss_mechs, gm_link) {
		if (m->gm_mech.gm_set_sec_context_option == NULL)
			continue;
		m->gm_mech.gm_set_sec_context_option(&junk, NULL,
		    GSS_KRB5_REGISTER_ACCEPTOR_IDENTITY_X, &buffer);
	}

	return (GSS_S_COMPLETE);
}

OM_uint32
gsskrb5_set_dns_canonicalize(int flag)
{
        struct _gss_mech_switch	*m;
	gss_buffer_desc buffer;
	OM_uint32 junk;
	char b = (flag != 0);

	_gss_load_mech();

	buffer.value = &b;
	buffer.length = sizeof(b);

	SLIST_FOREACH(m, &_gss_mechs, gm_link) {
		if (m->gm_mech.gm_set_sec_context_option == NULL)
			continue;
		m->gm_mech.gm_set_sec_context_option(&junk, NULL,
		    GSS_KRB5_SET_DNS_CANONICALIZE_X, &buffer);
	}

	return (GSS_S_COMPLETE);
}



static krb5_error_code
set_key(krb5_keyblock *keyblock, gss_krb5_lucid_key_t *key)
{
    key->type = keyblock->keytype;
    key->length = keyblock->keyvalue.length;
    key->data = malloc(key->length);
    if (key->data == NULL && key->length != 0)
	return ENOMEM;
    memcpy(key->data, keyblock->keyvalue.data, key->length);
    return 0;
}

static void
free_key(gss_krb5_lucid_key_t *key)
{
    memset(key->data, 0, key->length);
    free(key->data);
    memset(key, 0, sizeof(*key));
}


OM_uint32
gss_krb5_export_lucid_sec_context(OM_uint32 *minor_status,
				  gss_ctx_id_t *context_handle,
				  OM_uint32 version,
				  void **rctx)
{
    krb5_context context = NULL;
    krb5_error_code ret;
    gss_buffer_set_t data_set = GSS_C_NO_BUFFER_SET;
    OM_uint32 major_status;
    gss_krb5_lucid_context_v1_t *ctx = NULL;
    krb5_storage *sp = NULL;
    uint32_t num;

    if (context_handle == NULL || *context_handle == GSS_C_NO_CONTEXT) {
	ret = EINVAL;
	return GSS_S_FAILURE;
    }
    
    major_status =
	gss_inquire_sec_context_by_oid (minor_status,
					*context_handle,
					GSS_KRB5_EXPORT_LUCID_CONTEXT_V1_X,
					&data_set);
    if (major_status)
	return major_status;
    
    if (data_set == GSS_C_NO_BUFFER_SET || data_set->count != 1) {
	gss_release_buffer_set(minor_status, &data_set);
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }

    ret = krb5_init_context(&context);
    if (ret)
	goto out;

    ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
	ret = ENOMEM;
	goto out;
    }

    sp = krb5_storage_from_mem(data_set->elements[0].value,
			       data_set->elements[0].length);
    if (sp == NULL) {
	ret = ENOMEM;
	goto out;
    }
    
    ret = krb5_ret_uint32(sp, &num);
    if (ret) goto out;
    if (num != 1) {
	ret = EINVAL;
	goto out;
    }
    ctx->version = 1;
    /* initiator */
    ret = krb5_ret_uint32(sp, &ctx->initiate);
    if (ret) goto out;
    /* endtime */
    ret = krb5_ret_uint32(sp, &ctx->endtime);
    if (ret) goto out;
    /* send_seq */
    ret = krb5_ret_uint32(sp, &num);
    if (ret) goto out;
    ctx->send_seq = ((uint64_t)num) << 32;
    ret = krb5_ret_uint32(sp, &num);
    if (ret) goto out;
    ctx->send_seq |= num;
    /* recv_seq */
    ret = krb5_ret_uint32(sp, &num);
    if (ret) goto out;
    ctx->recv_seq = ((uint64_t)num) << 32;
    ret = krb5_ret_uint32(sp, &num);
    if (ret) goto out;
    ctx->recv_seq |= num;
    /* protocol */
    ret = krb5_ret_uint32(sp, &ctx->protocol);
    if (ret) goto out;
    if (ctx->protocol == 0) {
	krb5_keyblock key;

	/* sign_alg */
	ret = krb5_ret_uint32(sp, &ctx->rfc1964_kd.sign_alg);
	if (ret) goto out;
	/* seal_alg */
	ret = krb5_ret_uint32(sp, &ctx->rfc1964_kd.seal_alg);
	if (ret) goto out;
	/* ctx_key */
	ret = krb5_ret_keyblock(sp, &key);
	if (ret) goto out;
	ret = set_key(&key, &ctx->rfc1964_kd.ctx_key);
	krb5_free_keyblock_contents(context, &key);
	if (ret) goto out;
    } else if (ctx->protocol == 1) {
	krb5_keyblock key;

	/* acceptor_subkey */
	ret = krb5_ret_uint32(sp, &ctx->cfx_kd.have_acceptor_subkey);
	if (ret) goto out;
	/* ctx_key */
	ret = krb5_ret_keyblock(sp, &key);
	if (ret) goto out;
	ret = set_key(&key, &ctx->cfx_kd.ctx_key);
	krb5_free_keyblock_contents(context, &key);
	if (ret) goto out;
	/* acceptor_subkey */
	if (ctx->cfx_kd.have_acceptor_subkey) {
	    ret = krb5_ret_keyblock(sp, &key);
	    if (ret) goto out;
	    ret = set_key(&key, &ctx->cfx_kd.acceptor_subkey);
	    krb5_free_keyblock_contents(context, &key);
	    if (ret) goto out;
	}
    } else {
	ret = EINVAL;
	goto out;
    }

    *rctx = ctx;

out:
    gss_release_buffer_set(minor_status, &data_set);
    if (sp)
	krb5_storage_free(sp);
    if (context)
	krb5_free_context(context);

    if (ret) {
	if (ctx)
	    gss_krb5_free_lucid_sec_context(NULL, ctx);

	*minor_status = ret;
	return GSS_S_FAILURE;
    }
    *minor_status = 0;
    return GSS_S_COMPLETE;
}

OM_uint32
gss_krb5_free_lucid_sec_context(OM_uint32 *minor_status, void *c)
{
    gss_krb5_lucid_context_v1_t *ctx = c;

    if (ctx->version != 1) {
	if (minor_status)
	    *minor_status = 0;
	return GSS_S_FAILURE;
    }

    if (ctx->protocol == 0) {
	free_key(&ctx->rfc1964_kd.ctx_key);
    } else if (ctx->protocol == 1) {
	free_key(&ctx->cfx_kd.ctx_key);
	if (ctx->cfx_kd.have_acceptor_subkey)
	    free_key(&ctx->cfx_kd.acceptor_subkey);
    }
    free(ctx);
    if (minor_status)
	*minor_status = 0;
    return GSS_S_COMPLETE;
}

OM_uint32
gsskrb5_set_send_to_kdc(struct gsskrb5_send_to_kdc *c)
{
    struct _gss_mech_switch *m;
    gss_buffer_desc buffer;
    OM_uint32 junk;

    _gss_load_mech();

    if (c) {
	buffer.value = c;
	buffer.length = sizeof(*c);
    } else {
	buffer.value = NULL;
	buffer.length = 0;
    }

    SLIST_FOREACH(m, &_gss_mechs, gm_link) {
	if (m->gm_mech.gm_set_sec_context_option == NULL)
	    continue;
	m->gm_mech.gm_set_sec_context_option(&junk, NULL,
	    GSS_KRB5_SEND_TO_KDC_X, &buffer);
    }

    return (GSS_S_COMPLETE);
}

OM_uint32
gsskrb5_extract_authtime_from_sec_context(OM_uint32 *minor_status,
					  gss_ctx_id_t context_handle,
					  time_t *authtime)
{
    gss_buffer_set_t data_set = GSS_C_NO_BUFFER_SET;
    unsigned char buf[4];
    OM_uint32 maj_stat;

    if (context_handle == GSS_C_NO_CONTEXT) {
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }
    
    maj_stat =
	gss_inquire_sec_context_by_oid (minor_status,
					context_handle,
					GSS_KRB5_GET_AUTHTIME_X,
					&data_set);
    if (maj_stat)
	return maj_stat;
    
    if (data_set == GSS_C_NO_BUFFER_SET || data_set->count != 1) {
	gss_release_buffer_set(minor_status, &data_set);
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }

    if (data_set->elements[0].length != sizeof(buf)) {
	gss_release_buffer_set(minor_status, &data_set);
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }

    memcpy(buf, data_set->elements[0].value, sizeof(buf));
    gss_release_buffer_set(minor_status, &data_set);
    
    *authtime = (buf[0] <<24) | (buf[1] << 16) | (buf[2] << 8) | (buf[3] << 0);

    *minor_status = 0;
    return GSS_S_COMPLETE;
}
