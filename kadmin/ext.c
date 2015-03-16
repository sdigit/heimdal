/*
 * Copyright (c) 1997 - 2006 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "kadmin_locl.h"
#include "kadmin-commands.h"

struct ext_keytab_data {
    krb5_keytab keytab;
    int random_key_flag;
};

static int
do_ext_keytab(krb5_principal principal, void *data)
{
    krb5_error_code ret;
    kadm5_principal_ent_rec princ;
    struct ext_keytab_data *e = data;
    krb5_keytab_entry *keys = NULL;
    krb5_keyblock *k = NULL;
    size_t i;
    int n_k = 0;
    uint32_t mask;
    char *unparsed = NULL;

    mask = KADM5_PRINCIPAL;
    if (!e->random_key_flag)
        mask |= KADM5_KVNO | KADM5_KEY_DATA;

    ret = kadm5_get_principal(kadm_handle, principal, &princ, mask);
    if (ret)
	return ret;

    ret = krb5_unparse_name(context, principal, &unparsed);
    if (ret)
	goto out;

    if (!e->random_key_flag) {
        if (princ.n_key_data == 0) {
            krb5_warnx(context, "principal has no keys, or user lacks "
                       "get-keys privilege for %s", unparsed);
            goto out;
        }
	keys = calloc(sizeof(*keys), princ.n_key_data);
	if (keys == NULL) {
	    ret = krb5_enomem(context);
	    goto out;
	}
	for (i = 0; i < princ.n_key_data; i++) {
	    krb5_key_data *kd = &princ.key_data[i];
	    int warned = 0;

            /*
             * If the kadm5 client princ lacks get-keys then it may get
             * bogus keys.  This should only happen with kadmind servers
             * running master code from somewhere between 1.5 and 1.6.
             */
            if (kadm5_some_keys_are_bogus(1, kd)) {
		if (!warned) {
		    krb5_warnx(context, "user lacks get-keys privilege for %s",
			       unparsed);
		    warned = 1;
		}
                continue;
	    }

	    keys[i].principal = princ.principal;
	    keys[i].vno = kd->key_data_kvno;
	    keys[i].keyblock.keytype = kd->key_data_type[0];
	    keys[i].keyblock.keyvalue.length = kd->key_data_length[0];
	    keys[i].keyblock.keyvalue.data = kd->key_data_contents[0];
	    keys[i].timestamp = time(NULL);
            n_k++;
	}
    } else if (e->random_key_flag) {
        /* Probably lack get-keys privilege, but we may be able to set keys */
	ret = kadm5_randkey_principal(kadm_handle, principal, &k, &n_k);
	if (ret)
	    goto out;

	keys = calloc(sizeof(*keys), n_k);
	if (keys == NULL) {
	    ret = krb5_enomem(context);
	    goto out;
	}
	for (i = 0; i < n_k; i++) {
	    keys[i].principal = principal;
	    keys[i].vno = princ.kvno + 1; /* XXX get entry again */
	    keys[i].keyblock = k[i];
	    keys[i].timestamp = time(NULL);
	}
    }

    if (n_k == 0)
        krb5_warn(context, ret, "no keys written to keytab for %s", unparsed);

    for (i = 0; i < n_k; i++) {
	ret = krb5_kt_add_entry(context, e->keytab, &keys[i]);
	if (ret)
	    krb5_warn(context, ret, "krb5_kt_add_entry(%lu)", (unsigned long)i);
    }

  out:
    kadm5_free_principal_ent(kadm_handle, &princ);
    if (k) {
        for (i = 0; i < n_k; i++)
            memset(k[i].keyvalue.data, 0, k[i].keyvalue.length);
	free(k);
    }
    free(unparsed);
    free(keys);
    return 0;
}

int
ext_keytab(struct ext_keytab_options *opt, int argc, char **argv)
{
    krb5_error_code ret;
    int i;
    struct ext_keytab_data data;

    if (opt->keytab_string == NULL)
	ret = krb5_kt_default(context, &data.keytab);
    else
	ret = krb5_kt_resolve(context, opt->keytab_string, &data.keytab);

    if(ret){
	krb5_warn(context, ret, "krb5_kt_resolve");
	return 1;
    }

    data.random_key_flag = opt->random_key_flag;

    for(i = 0; i < argc; i++) {
	ret = foreach_principal(argv[i], do_ext_keytab, "ext", &data);
	if (ret)
	    break;
    }

    krb5_kt_close(context, data.keytab);

    return ret != 0;
}
