/*
 * Copyright (c) 1997, 1998, 1999 Kungliga Tekniska H�gskolan
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
 * 3. All advertising materials mentioning features or use of this software 
 *    must display the following acknowledgement: 
 *      This product includes software developed by Kungliga Tekniska 
 *      H�gskolan and its contributors. 
 *
 * 4. Neither the name of the Institute nor the names of its contributors 
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

#include "krb5_locl.h"

RCSID("$Id$");

krb5_error_code
krb5_init_etype (krb5_context context,
		 unsigned *len,
		 int **val,
		 const krb5_enctype *etypes)
{
    int i;
    krb5_error_code ret;
    krb5_enctype *tmp;

    ret = 0;
    if (etypes)
	tmp = (krb5_enctype*)etypes;
    else {
	ret = krb5_get_default_in_tkt_etypes(context,
					     &tmp);
	if (ret)
	    return ret;
    }

    for (i = 0; tmp[i]; ++i)
	;
    *len = i;
    *val = malloc(i * sizeof(int));
    if (*val == NULL) {
	ret = ENOMEM;
	goto cleanup;
    }
    memmove (*val,
	     tmp,
	     i * sizeof(*tmp));
cleanup:
    if (etypes == NULL)
	free (tmp);
    return ret;
}


static krb5_error_code
decrypt_tkt (krb5_context context,
	     krb5_keyblock *key,
	     unsigned usage,
	     krb5_const_pointer decrypt_arg,
	     krb5_kdc_rep *dec_rep)
{
    krb5_error_code ret;
    krb5_data data;
    size_t size;
    krb5_crypto crypto;

    krb5_crypto_init(context, key, 0, &crypto);

    ret = krb5_decrypt_EncryptedData (context,
				      crypto,
				      usage,
				      &dec_rep->kdc_rep.enc_part,
				      &data);
    krb5_crypto_destroy(context, crypto);

    if (ret)
	return ret;

    ret = krb5_decode_EncASRepPart(context,
				   data.data,
				   data.length,
				   &dec_rep->enc_part, 
				   &size);
    if (ret)
	ret = krb5_decode_EncTGSRepPart(context,
					data.data,
					data.length,
					&dec_rep->enc_part, 
					&size);
    krb5_data_free (&data);
    if (ret)
	return ret;
    return 0;
}

int
_krb5_extract_ticket(krb5_context context, 
		     krb5_kdc_rep *rep, 
		     krb5_creds *creds,		
		     krb5_keyblock *key,
		     krb5_const_pointer keyseed,
		     krb5_key_usage key_usage,
		     krb5_addresses *addrs,
		     unsigned nonce,
		     krb5_boolean allow_server_mismatch,
		     krb5_decrypt_proc decrypt_proc,
		     krb5_const_pointer decryptarg)
{
    krb5_error_code ret;
    krb5_principal tmp_principal;
    int tmp;
    time_t tmp_time;
    int32_t sec_now;

    /* compare client */

    ret = principalname2krb5_principal (&tmp_principal,
					rep->kdc_rep.cname,
					rep->kdc_rep.crealm);
    if (ret)
	goto out;
    tmp = krb5_principal_compare (context, tmp_principal, creds->client);
    krb5_free_principal (context, tmp_principal);
    if (!tmp) {
	ret = KRB5KRB_AP_ERR_MODIFIED;
	goto out;
    }
    
    /* extract ticket */
    {
	unsigned char *buf;
	size_t len;
	len = length_Ticket(&rep->kdc_rep.ticket);
	buf = malloc(len);
	if(buf == NULL) {
	    ret = ENOMEM;
	    goto out;
	}
	encode_Ticket(buf + len - 1, len, &rep->kdc_rep.ticket, &len);
	creds->ticket.data = buf;
	creds->ticket.length = len;
	creds->second_ticket.length = 0;
	creds->second_ticket.data   = NULL;
    }

    /* compare server */

    ret = principalname2krb5_principal (&tmp_principal,
					rep->kdc_rep.ticket.sname,
					rep->kdc_rep.ticket.realm);
    if (ret)
	goto out;
    if(allow_server_mismatch){
	krb5_free_principal(context, creds->server);
	creds->server = tmp_principal;
	tmp_principal = NULL;
    }else{
	tmp = krb5_principal_compare (context, tmp_principal, creds->server);
	krb5_free_principal (context, tmp_principal);
	if (!tmp) {
	    ret = KRB5KRB_AP_ERR_MODIFIED;
	    goto out;
	}
    }
    
    /* decrypt */

    if (decrypt_proc == NULL)
	decrypt_proc = decrypt_tkt;
    
    ret = (*decrypt_proc)(context, key, key_usage, decryptarg, rep);
    if (ret)
	goto out;

#if 0
    /* XXX should this decode be here, or in the decrypt_proc? */
    ret = krb5_decode_keyblock(context, &rep->enc_part.key, 1);
    if(ret)
	goto out;
#endif

    /* compare nonces */

    if (nonce != rep->enc_part.nonce) {
	ret = KRB5KRB_AP_ERR_MODIFIED;
	goto out;
    }

    /* set kdc-offset */

    krb5_timeofday (context, &sec_now);
    if (context->kdc_sec_offset == 0
	&& krb5_config_get_bool (context, NULL,
				 "libdefaults",
				 "kdc_timesync",
				 NULL)) {
	context->kdc_sec_offset = rep->enc_part.authtime - sec_now;
	krb5_timeofday (context, &sec_now);
    }

    /* check all times */

    if (rep->enc_part.starttime) {
	tmp_time = *rep->enc_part.starttime;
    } else
	tmp_time = rep->enc_part.authtime;

    if (creds->times.starttime == 0
	&& abs(tmp_time - sec_now) > context->max_skew) {
	ret = KRB5KRB_AP_ERR_SKEW;
	goto out;
    }

    if (creds->times.starttime != 0
	&& tmp_time != creds->times.starttime) {
	ret = KRB5KRB_AP_ERR_MODIFIED;
	goto out;
    }

    creds->times.starttime = tmp_time;

    if (rep->enc_part.renew_till) {
	tmp_time = *rep->enc_part.renew_till;
    } else
	tmp_time = 0;

    if (creds->times.renew_till != 0
	&& tmp_time > creds->times.renew_till) {
	ret = KRB5KRB_AP_ERR_MODIFIED;
	goto out;
    }

    creds->times.renew_till = tmp_time;

    creds->times.authtime = rep->enc_part.authtime;

    if (creds->times.endtime != 0
	&& rep->enc_part.endtime > creds->times.endtime) {
	ret = KRB5KRB_AP_ERR_MODIFIED;
	goto out;
    }

    creds->times.endtime  = rep->enc_part.endtime;

    if(rep->enc_part.caddr)
	krb5_copy_addresses (context, rep->enc_part.caddr, &creds->addresses);
    else if(addrs)
	krb5_copy_addresses (context, addrs, &creds->addresses);
    else {
	creds->addresses.len = 0;
	creds->addresses.val = NULL;
    }
    creds->flags.b = rep->enc_part.flags;
	  
    creds->authdata.len = 0;
    creds->authdata.val = NULL;
    creds->session.keyvalue.length = 0;
    creds->session.keyvalue.data   = NULL;
    creds->session.keytype = rep->enc_part.key.keytype;
    ret = krb5_data_copy (&creds->session.keyvalue,
			  rep->enc_part.key.keyvalue.data,
			  rep->enc_part.key.keyvalue.length);

out:
    memset (rep->enc_part.key.keyvalue.data, 0,
	    rep->enc_part.key.keyvalue.length);
    return ret;
}


static krb5_error_code
make_pa_enc_timestamp(krb5_context context, PA_DATA *pa, 
		      krb5_enctype etype, krb5_keyblock *key)
{
    PA_ENC_TS_ENC p;
    u_char buf[1024];
    size_t len;
    EncryptedData encdata;
    krb5_error_code ret;
    int32_t sec, usec;
    unsigned usec2;
    krb5_crypto crypto;
    
    krb5_us_timeofday (context, &sec, &usec);
    p.patimestamp = sec;
    usec2 = usec;
    p.pausec      = &usec2;

    ret = encode_PA_ENC_TS_ENC(buf + sizeof(buf) - 1,
			       sizeof(buf),
			       &p,
			       &len);
    if (ret)
	return ret;

    krb5_crypto_init(context, key, 0, &crypto);
    ret = krb5_encrypt_EncryptedData(context, 
				     crypto,
				     KRB5_KU_PA_ENC_TIMESTAMP,
				     buf + sizeof(buf) - len,
				     len,
				     0,
				     &encdata);
    krb5_crypto_destroy(context, crypto);
    if (ret)
	return ret;
		    
    ret = encode_EncryptedData(buf + sizeof(buf) - 1,
			       sizeof(buf),
			       &encdata, 
			       &len);
    free_EncryptedData(&encdata);
    if (ret)
	return ret;
    pa->padata_type = pa_enc_timestamp;
    pa->padata_value.length = 0;
    krb5_data_copy(&pa->padata_value,
		   buf + sizeof(buf) - len,
		   len);
    return 0;
}

static krb5_error_code
add_padata(krb5_context context,
	   METHOD_DATA *md, 
	   krb5_principal client,
	   krb5_key_proc key_proc,
	   krb5_const_pointer keyseed,
	   krb5_enctype enctype, 
	   krb5_salt *salt)
{
    krb5_error_code ret;
    PA_DATA *pa2;
    krb5_keyblock *key;
    krb5_salt salt2;
    
    if(salt == NULL) {
	/* default to standard salt */
	ret = krb5_get_pw_salt (context, client, &salt2);
	salt = &salt2;
    }
    ret = (*key_proc)(context, enctype, *salt, keyseed, &key);
    if(salt == &salt2)
	krb5_free_salt(context, salt2);
    if (ret)
	return ret;
    pa2 = realloc(md->val, (md->len + 1) * sizeof(*md->val));
    if(pa2 == NULL)
	return ENOMEM;
    md->val = pa2;
    ret = make_pa_enc_timestamp(context, &md->val[md->len], enctype, key);
    krb5_free_keyblock (context, key);
    if(ret)
	return ret;
    md->len++;
    return 0;
}

static krb5_error_code
init_as_req (krb5_context context,
	     krb5_kdc_flags opts,
	     krb5_creds *creds,
	     const krb5_addresses *addrs,
	     const krb5_enctype *etypes,
	     const krb5_preauthtype *ptypes,
	     const krb5_preauthdata *preauth,
	     krb5_key_proc key_proc,
	     krb5_const_pointer keyseed,
	     unsigned nonce,
	     AS_REQ *a)
{
    krb5_error_code ret;
    krb5_salt salt;
    krb5_enctype etype;

    memset(a, 0, sizeof(*a));

    a->pvno = 5;
    a->msg_type = krb_as_req;
    a->req_body.kdc_options = opts.b;
    a->req_body.cname = malloc(sizeof(*a->req_body.cname));
    if (a->req_body.cname == NULL) {
	ret = ENOMEM;
	goto fail;
    }
    a->req_body.sname = malloc(sizeof(*a->req_body.sname));
    if (a->req_body.sname == NULL) {
	ret = ENOMEM;
	goto fail;
    }
    ret = krb5_principal2principalname (a->req_body.cname, creds->client);
    if (ret)
	goto fail;
    ret = krb5_principal2principalname (a->req_body.sname, creds->server);
    if (ret)
	goto fail;
    ret = copy_Realm(&creds->client->realm, &a->req_body.realm);
    if (ret)
	goto fail;

    if(creds->times.starttime) {
	a->req_body.from = malloc(sizeof(*a->req_body.from));
	if (a->req_body.from == NULL) {
	    ret = ENOMEM;
	    goto fail;
	}
	*a->req_body.from = creds->times.starttime;
    }
    if(creds->times.endtime){
	ALLOC(a->req_body.till, 1);
	*a->req_body.till = creds->times.endtime;
    }
    if(creds->times.renew_till){
	a->req_body.rtime = malloc(sizeof(*a->req_body.rtime));
	if (a->req_body.rtime == NULL) {
	    ret = ENOMEM;
	    goto fail;
	}
	*a->req_body.rtime = creds->times.renew_till;
    }
    a->req_body.nonce = nonce;
    ret = krb5_init_etype (context,
			   &a->req_body.etype.len,
			   &a->req_body.etype.val,
			   etypes);
    if (ret)
	goto fail;

    etype = a->req_body.etype.val[0]; /* XXX */

    a->req_body.addresses = malloc(sizeof(*a->req_body.addresses));
    if (a->req_body.addresses == NULL) {
	ret = ENOMEM;
	goto fail;
    }

    if (addrs)
	ret = krb5_copy_addresses(context, addrs, a->req_body.addresses);
    else
	ret = krb5_get_all_client_addrs (context, a->req_body.addresses);
    if (ret)
	return ret;

    a->req_body.enc_authorization_data = NULL;
    a->req_body.additional_tickets = NULL;

    if(preauth != NULL) {
	int i;
	ALLOC(a->padata, 1);
	if(a->padata == NULL) {
	    ret = ENOMEM;
	    goto fail;
	}
	for(i = 0; i < preauth->len; i++) {
	    if(preauth->val[i].type == KRB5_PADATA_ENC_TIMESTAMP){
		int j;
		PA_DATA *tmp = realloc(a->padata->val, 
				       (a->padata->len + 
					preauth->val[i].info.len) * 
				       sizeof(*a->padata->val));
		if(tmp == NULL) {
		    ret = ENOMEM;
		    goto fail;
		}
		a->padata->val = tmp;
		for(j = 0; j < preauth->val[i].info.len; j++) {
		    krb5_salt *sp = &salt;
		    if(preauth->val[i].info.val[j].salttype)
			salt.salttype = *preauth->val[i].info.val[j].salttype;
		    else
			salt.salttype = KRB5_PW_SALT;
		    if(preauth->val[i].info.val[j].salt)
			salt.saltvalue = *preauth->val[i].info.val[j].salt;
		    else
			if(salt.salttype == KRB5_PW_SALT)
			    sp = NULL;
			else
			    krb5_data_zero(&salt.saltvalue);
		    add_padata(context, a->padata, creds->client, 
			       key_proc, keyseed, 
			       preauth->val[i].info.val[j].etype,
			       sp);
		}
	    }
	}
    } else 
    /* not sure this is the way to use `ptypes' */
    if (ptypes == NULL || *ptypes == KRB5_PADATA_NONE)
	a->padata = NULL;
    else if (*ptypes ==  KRB5_PADATA_ENC_TIMESTAMP) {
	ALLOC(a->padata, 1);
	if (a->padata == NULL) {
	    ret = ENOMEM;
	    goto fail;
	}
	a->padata->len = 0;
	a->padata->val = NULL;

	/* make a v5 salted pa-data */
	add_padata(context, a->padata, creds->client, 
		   key_proc, keyseed, etype, NULL);
	
	/* make a v4 salted pa-data */
	salt.salttype = KRB5_PW_SALT;
	krb5_data_zero(&salt.saltvalue);
	add_padata(context, a->padata, creds->client, 
		   key_proc, keyseed, etype, &salt);
    } else {
	ret = KRB5_PREAUTH_BAD_TYPE;
	goto fail;
    }
    return 0;
fail:
    free_AS_REQ(a);
    return ret;
}

krb5_error_code
krb5_get_in_cred(krb5_context context,
		 krb5_flags options,
		 const krb5_addresses *addrs,
		 const krb5_enctype *etypes,
		 const krb5_preauthtype *ptypes,
		 const krb5_preauthdata *preauth,
		 krb5_key_proc key_proc,
		 krb5_const_pointer keyseed,
		 krb5_decrypt_proc decrypt_proc,
		 krb5_const_pointer decryptarg,
		 krb5_creds *creds,
		 krb5_kdc_rep *ret_as_reply)
{
    krb5_error_code ret;
    AS_REQ a;
    krb5_kdc_rep rep;
    krb5_data req, resp;
    char buf[BUFSIZ];
    krb5_salt salt;
    krb5_keyblock *key;
    size_t size;
    krb5_kdc_flags opts;
    PA_DATA *pa;
    krb5_enctype etype;
    unsigned nonce;

    opts.i = options;

    krb5_generate_random_block (&nonce, sizeof(nonce));
    nonce &= 0xffffffff;

    ret = init_as_req (context,
		       opts,
		       creds,
		       addrs,
		       etypes,
		       ptypes,
		       preauth,
		       key_proc,
		       keyseed,
		       nonce,
		       &a);
    if (ret)
	return ret;

    ret = encode_AS_REQ ((unsigned char*)buf + sizeof(buf) - 1,
			 sizeof(buf),
			 &a,
			 &req.length);
    free_AS_REQ(&a);
    if (ret)
	return ret;

    req.data = buf + sizeof(buf) - req.length;

    ret = krb5_sendto_kdc (context, &req, &creds->client->realm, &resp);
    if (ret)
	return ret;

    memset (&rep, 0, sizeof(rep));
    if((ret = decode_AS_REP(resp.data, resp.length, &rep.kdc_rep, &size))) {
	/* let's try to parse it as a KRB-ERROR */
	KRB_ERROR error;
	int ret2;

	ret2 = krb5_rd_error(context, &resp, &error);
	if(ret2 && resp.data && ((char*)resp.data)[0] == 4)
	    ret = KRB5KRB_AP_ERR_V4_REPLY;
	krb5_data_free(&resp);
	if (ret2 == 0) {
	    ret = error.error_code;
	    if(ret_as_reply)
		ret_as_reply->error = error;
	    else
		free_KRB_ERROR (&error);
	    return ret;
	}
	return ret;
    }
    krb5_data_free(&resp);
    
    pa = NULL;
    etype = rep.kdc_rep.enc_part.etype;
    if(rep.kdc_rep.padata){
	int index = 0;
	pa = krb5_find_padata(rep.kdc_rep.padata->val, rep.kdc_rep.padata->len, 
			      pa_pw_salt, &index);
	if(pa == NULL) {
	    index = 0;
	    pa = krb5_find_padata(rep.kdc_rep.padata->val, 
				  rep.kdc_rep.padata->len, 
				  pa_afs3_salt, &index);
	}
    }
    if(pa) {
	salt.salttype = pa->padata_type;
	salt.saltvalue = pa->padata_value;
	
	ret = (*key_proc)(context, etype, salt, keyseed, &key);
    } else {
	/* make a v5 salted pa-data */
	ret = krb5_get_pw_salt (context, creds->client, &salt);
	
	if (ret)
	    goto out;
	ret = (*key_proc)(context, etype, salt, keyseed, &key);
	krb5_free_salt(context, salt);
    }
    if (ret)
	goto out;
	
    ret = _krb5_extract_ticket(context, 
			       &rep, 
			       creds, 
			       key, 
			       keyseed, 
			       KRB5_KU_AS_REP_ENC_PART,
			       NULL, 
			       nonce, 
			       FALSE, 
			       decrypt_proc, 
			       decryptarg);
    memset (key->keyvalue.data, 0, key->keyvalue.length);
    krb5_free_keyblock_contents (context, key);
    free (key);

out:
    if (ret == 0 && ret_as_reply)
	*ret_as_reply = rep;
    else
	krb5_free_kdc_rep (context, &rep);
    return ret;
}

krb5_error_code
krb5_get_in_tkt(krb5_context context,
		krb5_flags options,
		const krb5_addresses *addrs,
		const krb5_enctype *etypes,
		const krb5_preauthtype *ptypes,
		krb5_key_proc key_proc,
		krb5_const_pointer keyseed,
		krb5_decrypt_proc decrypt_proc,
		krb5_const_pointer decryptarg,
		krb5_creds *creds,
		krb5_ccache ccache,
		krb5_kdc_rep *ret_as_reply)
{
    krb5_error_code ret;
    krb5_kdc_flags opts;
    opts.i = 0;
    opts.b = int2KDCOptions(options);
    
    ret = krb5_get_in_cred (context,
			    opts.i,
			    addrs,
			    etypes,
			    ptypes,
			    NULL,
			    key_proc,
			    keyseed,
			    decrypt_proc,
			    decryptarg,
			    creds,
			    ret_as_reply);
    if(ret) 
	return ret;
    ret = krb5_cc_store_cred (context, ccache, creds);
    krb5_free_creds_contents (context, creds);
    return ret;
}
