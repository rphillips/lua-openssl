/*=========================================================================*\
* pkey.c
* pkey module for lua-openssl binding
*
* Author:  george zhao <zhaozg(at)gmail.com>
\*=========================================================================*/

#include "openssl.h"
#include "private.h"

#define MYNAME    "pkey"
#define MYVERSION MYNAME " library for " LUA_VERSION " / Nov 2014 / "\
  "based on OpenSSL " SHLIB_VERSION_NUMBER

static int openssl_pkey_bits(lua_State *L)
{
  EVP_PKEY *pkey = CHECK_OBJECT(1, EVP_PKEY, "openssl.evp_pkey");
  lua_Integer ret = EVP_PKEY_bits(pkey);
  lua_pushinteger(L, ret);
  return  1;
};


static int openssl_is_private_key(EVP_PKEY* pkey)
{
  assert(pkey != NULL);

  switch (pkey->type)
  {
#ifndef OPENSSL_NO_RSA
  case EVP_PKEY_RSA:
  case EVP_PKEY_RSA2:
    assert(pkey->pkey.rsa != NULL);
    if (pkey->pkey.rsa != NULL && (NULL == pkey->pkey.rsa->p || NULL == pkey->pkey.rsa->q))
    {
      return 0;
    }
    break;
#endif
#ifndef OPENSSL_NO_DSA
  case EVP_PKEY_DSA:
  case EVP_PKEY_DSA1:
  case EVP_PKEY_DSA2:
  case EVP_PKEY_DSA3:
  case EVP_PKEY_DSA4:
    assert(pkey->pkey.dsa != NULL);

    if (NULL == pkey->pkey.dsa->p || NULL == pkey->pkey.dsa->q || NULL == pkey->pkey.dsa->priv_key)
    {
      return 0;
    }
    break;
#endif
#ifndef OPENSSL_NO_DH
  case EVP_PKEY_DH:
    assert(pkey->pkey.dh != NULL);

    if (NULL == pkey->pkey.dh->p || NULL == pkey->pkey.dh->priv_key)
    {
      return 0;
    }
    break;
#endif
#ifndef OPENSSL_NO_EC
  case EVP_PKEY_EC:
    assert(pkey->pkey.ec != NULL);
    if (NULL == EC_KEY_get0_private_key(pkey->pkey.ec))
    {
      return 0;
    }
    break;
#endif
  default:
    return -1;
    break;
  }
  return 1;
}

int pkey_read_pass_cb(char *buf, int size, int rwflag, void *u)
{
  int len = size;

  if (len <= 0) return 0;
  strncpy(buf, (const char*)u, size);
  len = strlen(buf);
  return len;
}

static int openssl_pkey_read(lua_State*L)
{
  EVP_PKEY * key = NULL;
  BIO* in = load_bio_object(L, 1);
  int priv = lua_isnoneornil(L, 2) ? 0 : auxiliar_checkboolean(L, 2);
  int fmt = luaL_checkoption(L, 3, "auto", format);

  if (!priv)
  {
    if (fmt == FORMAT_AUTO || fmt == FORMAT_PEM)
    {
      key = PEM_read_bio_PUBKEY(in, NULL, NULL, NULL);
      BIO_reset(in);
    }
    if ((fmt == FORMAT_AUTO && key == NULL) || fmt == FORMAT_DER)
    {
      key = d2i_PUBKEY_bio(in, NULL);
      BIO_reset(in);
    }
  }
  else
  {
    if (fmt == FORMAT_AUTO || fmt == FORMAT_PEM)
    {
      const char* passphrase = luaL_optstring(L, 4, NULL);
      key = PEM_read_bio_PrivateKey(in, NULL, passphrase ? pkey_read_pass_cb : NULL, (void*)passphrase);
      BIO_reset(in);
    }
    if ((fmt == FORMAT_AUTO && key == NULL) || fmt == FORMAT_DER)
    {
      d2i_PrivateKey_bio(in, &key);
      BIO_reset(in);
    }
  }
  BIO_free(in);
  if (key) {
    ERR_clear_error();
    PUSH_OBJECT(key, "openssl.evp_pkey");
  }
  else
    lua_pushnil(L);
  return 1;
}

static int EC_KEY_generate_key_part(EC_KEY *eckey)
{
  int ok = 0;
  BN_CTX  *ctx = NULL;
  BIGNUM  *priv_key = NULL, *order = NULL;
  EC_POINT *pub_key = NULL;
  const EC_GROUP *group;

  if (!eckey)
  {
    return 0;
  }
  group = EC_KEY_get0_group(eckey);

  if ((order = BN_new()) == NULL) goto err;
  if ((ctx = BN_CTX_new()) == NULL) goto err;
  priv_key = (BIGNUM*)EC_KEY_get0_private_key(eckey);

  if (priv_key == NULL)
  {
    goto err;
  }

  if (!EC_GROUP_get_order(group, order, ctx))
    goto err;

  if (BN_is_zero(priv_key))
    goto err;
  pub_key = (EC_POINT *)EC_KEY_get0_public_key(eckey);

  if (pub_key == NULL)
  {
    pub_key = EC_POINT_new(group);
    if (pub_key == NULL)
      goto err;
  }

  if (!EC_POINT_mul(group, pub_key, priv_key, NULL, NULL, ctx))
    goto err;
  {
    EC_POINT_make_affine(EC_KEY_get0_group(eckey),
                         (EC_POINT *)EC_KEY_get0_public_key(eckey),
                         NULL);
  }
  EC_KEY_set_private_key(eckey, priv_key);
  EC_KEY_set_public_key(eckey, pub_key);

  ok = 1;

err:
  if (order)
    BN_free(order);

  if (ctx != NULL)
    BN_CTX_free(ctx);
  return (ok);
}

static LUA_FUNCTION(openssl_pkey_new)
{
  EVP_PKEY *pkey = NULL;
  const char* alg = "rsa";

  if (lua_isnoneornil(L, 1) || lua_isstring(L, 1))
  {
    alg = luaL_optstring(L, 1, alg);

    if (strcasecmp(alg, "rsa") == 0)
    {
      int bits = luaL_optint(L, 2, 1024);
      int e = luaL_optint(L, 3, 65537);
      RSA* rsa = bits ? RSA_generate_key(bits, e, NULL, NULL) : RSA_new();
      if (bits == 0 || rsa->n == 0)
        rsa->n = BN_new();
      pkey = EVP_PKEY_new();
      EVP_PKEY_assign_RSA(pkey, rsa);
    }
    else if (strcasecmp(alg, "dsa") == 0)
    {
      int bits = luaL_optint(L, 2, 1024);
      size_t seed_len = 0;
      const char* seed = luaL_optlstring(L, 3, NULL, &seed_len);

      DSA *dsa = DSA_generate_parameters(bits, (byte*)seed, seed_len, NULL,  NULL, NULL, NULL);
      if ( !DSA_generate_key(dsa))
      {
        DSA_free(dsa);
        luaL_error(L, "DSA_generate_key failed");
      }
      pkey = EVP_PKEY_new();
      EVP_PKEY_assign_DSA(pkey, dsa);

    }
    else if (strcasecmp(alg, "dh") == 0)
    {
      int bits = luaL_optint(L, 2, 512);
      int generator = luaL_optint(L, 3, 2);

      DH* dh = DH_new();
      if (!DH_generate_parameters_ex(dh, bits, generator, NULL))
      {
        DH_free(dh);
        luaL_error(L, "DH_generate_parameters_ex failed");
      }
      DH_generate_key(dh);
      pkey = EVP_PKEY_new();
      EVP_PKEY_assign_DH(pkey, dh);
    }
#ifndef OPENSSL_NO_EC
    else if (strcasecmp(alg, "ec") == 0)
    {
      int ec_name = NID_undef;
      EC_KEY *ec = NULL;

      int flag = OPENSSL_EC_NAMED_CURVE;

      if (lua_isnumber(L, 2))
      {
        ec_name = luaL_checkint(L, 2);
      }
      else if (lua_isstring(L, 2))
      {
        const char* name = luaL_checkstring(L, 2);
        ec_name = OBJ_sn2nid(name);
      }
      else
        luaL_argerror(L, 2, "must be ec_name string or nid");

      flag = lua_isnoneornil(L, 3) ? flag : lua_toboolean(L, 3);
      ec = EC_KEY_new();
      if (ec_name != NID_undef)
      {
        EC_GROUP *group = EC_GROUP_new_by_curve_name(ec_name);
        if (!group)
        {
          luaL_error(L, "not support curve_name %d:%s!!!!", ec_name, OBJ_nid2sn(ec_name));
        }
        EC_KEY_set_group(ec, group);
        EC_GROUP_free(group);
        if (!EC_KEY_generate_key(ec))
        {
          EC_KEY_free(ec);
          luaL_error(L, "EC_KEY_generate_key failed");
        }
      }

      EC_KEY_set_asn1_flag(ec, flag);

      pkey = EVP_PKEY_new();
      EVP_PKEY_assign_EC_KEY(pkey, ec);
    }
#endif
    else
    {
      luaL_error(L, "not support %s!!!!", alg);
    }
  }
  else if (lua_istable(L, 1))
  {
    lua_getfield(L, 1, "alg");
    alg = luaL_optstring(L, -1, alg);
    lua_pop(L, 1);
    if (strcasecmp(alg, "rsa") == 0)
    {
      pkey = EVP_PKEY_new();
      if (pkey)
      {
        RSA *rsa = RSA_new();
        if (rsa)
        {
          OPENSSL_PKEY_SET_BN(1, rsa, n);
          OPENSSL_PKEY_SET_BN(1, rsa, e);
          OPENSSL_PKEY_SET_BN(1, rsa, d);
          OPENSSL_PKEY_SET_BN(1, rsa, p);
          OPENSSL_PKEY_SET_BN(1, rsa, q);
          OPENSSL_PKEY_SET_BN(1, rsa, dmp1);
          OPENSSL_PKEY_SET_BN(1, rsa, dmq1);
          OPENSSL_PKEY_SET_BN(1, rsa, iqmp);
          if (rsa->n)
          {
            if (!EVP_PKEY_assign_RSA(pkey, rsa))
            {
              EVP_PKEY_free(pkey);
              pkey = NULL;
            }
          }
        }
      }
    }
    else if (strcasecmp(alg, "dsa") == 0)
    {
      pkey = EVP_PKEY_new();
      if (pkey)
      {
        DSA *dsa = DSA_new();
        if (dsa)
        {
          OPENSSL_PKEY_SET_BN(-1, dsa, p);
          OPENSSL_PKEY_SET_BN(-1, dsa, q);
          OPENSSL_PKEY_SET_BN(-1, dsa, g);
          OPENSSL_PKEY_SET_BN(-1, dsa, priv_key);
          OPENSSL_PKEY_SET_BN(-1, dsa, pub_key);
          if (dsa->p && dsa->q && dsa->g)
          {
            if (!dsa->priv_key && !dsa->pub_key)
            {
              DSA_generate_key(dsa);
            }
            if (!EVP_PKEY_assign_DSA(pkey, dsa))
            {
              EVP_PKEY_free(pkey);
              pkey = NULL;
            }
          }
        }
      }
    }
    else if (strcasecmp(alg, "dh") == 0)
    {

      pkey = EVP_PKEY_new();
      if (pkey)
      {
        DH *dh = DH_new();
        if (dh)
        {
          OPENSSL_PKEY_SET_BN(-1, dh, p);
          OPENSSL_PKEY_SET_BN(-1, dh, g);
          OPENSSL_PKEY_SET_BN(-1, dh, priv_key);
          OPENSSL_PKEY_SET_BN(-1, dh, pub_key);
          if (dh->p && dh->g)
          {
            if (!dh->pub_key)
            {
              DH_generate_key(dh);
            }
            if (!EVP_PKEY_assign_DH(pkey, dh))
            {
              EVP_PKEY_free(pkey);
              pkey = NULL;
            }
          }
        }
      }
    }
    else if (strcasecmp(alg, "ec") == 0)
    {

      int ec_name = NID_undef;
      BIGNUM *d = NULL;
      BIGNUM *x = NULL;
      BIGNUM *y = NULL;
      BIGNUM *z = NULL;
      EC_GROUP *group = NULL;

      lua_getfield(L, -1, "ec_name");
      if (lua_isnumber(L, -1))
      {
        ec_name = luaL_checkint(L, -1);
      }
      else if (lua_isstring(L, -1))
      {
        const char* name = luaL_checkstring(L, -1);
        ec_name = OBJ_sn2nid(name);
      }
      else
      {
        luaL_error(L, "not support ec_name type:%s!!!!", lua_typename(L, lua_type(L, -1)));
      }
      lua_pop(L, 1);

      lua_getfield(L, -1, "D");
      if (!lua_isnil(L, -1))
      {
        d = BN_get(L, -1);
      }
      lua_pop(L, 1);

      lua_getfield(L, -1, "X");
      if (!lua_isnil(L, -1))
      {
        x = BN_get(L, -1);
      }
      lua_pop(L, 1);

      lua_getfield(L, -1, "Y");
      if (!lua_isnil(L, -1))
      {
        y = BN_get(L, -1);
      }
      lua_pop(L, 1);

      lua_getfield(L, -1, "Z");
      if (!lua_isnil(L, -1))
      {
        z = BN_get(L, -1);
      }
      lua_pop(L, 1);

      if (ec_name != NID_undef)
        group = EC_GROUP_new_by_curve_name(ec_name);

      if (!group)
      {
        luaL_error(L, "not support curve_name %d:%s!!!!", ec_name, OBJ_nid2sn(ec_name));
      }

      pkey = EVP_PKEY_new();
      if (pkey)
      {
        EC_KEY *ec = EC_KEY_new();
        if (ec)
        {
          EC_KEY_set_group(ec, group);
          if (d)
            EC_KEY_set_private_key(ec, d);
          if (x != NULL && y != NULL)
          {
            EC_POINT *pnt = EC_POINT_new(group);
            if (z == NULL)
              EC_POINT_set_affine_coordinates_GFp(group, pnt, x, y, NULL);
            else
              EC_POINT_set_Jprojective_coordinates_GFp(group, pnt, x, y, z, NULL);

            EC_KEY_set_public_key(ec, pnt);
          }

          if (!EVP_PKEY_assign_EC_KEY(pkey, ec))
          {
            EC_KEY_free(ec);
            EVP_PKEY_free(pkey);
            pkey = NULL;
          }
          if (d && !EC_KEY_check_key(ec))
          {
            EC_KEY_generate_key_part(ec);
          }
        }
      }
    }
  }

  if (pkey)
  {
    PUSH_OBJECT(pkey, "openssl.evp_pkey");
    return 1;
  }
  return 0;

}

static LUA_FUNCTION(openssl_pkey_export)
{
  EVP_PKEY * key;
  int exppriv = 0;
  int exraw = 0;
  int expem = 1;
  size_t passphrase_len = 0;
  BIO * bio_out = NULL;
  int ret = 0;
  const EVP_CIPHER * cipher;
  const char * passphrase = NULL;
  int is_priv;

  key = CHECK_OBJECT(1, EVP_PKEY, "openssl.evp_pkey");
  if (!lua_isnoneornil(L, 2))
    exppriv = lua_toboolean(L, 2);
  if (!lua_isnoneornil(L, 3))
    exraw = lua_toboolean(L, 3);
  if (!lua_isnoneornil(L, 4))
    expem = lua_toboolean(L, 4);
  passphrase = luaL_optlstring(L, 5, NULL, &passphrase_len);

  is_priv = openssl_is_private_key(key);
  bio_out = BIO_new(BIO_s_mem());
  if (!is_priv)
    exppriv = 0;

  if (passphrase)
  {
    cipher = (EVP_CIPHER *) EVP_des_ede3_cbc();
  }
  else
  {
    cipher = NULL;
  }

  if (!exraw)
  {
    /* export with EVP format */
    if (!exppriv)
    {
      if (expem)
        ret = PEM_write_bio_PUBKEY(bio_out, key);
      else
      {
#if OPENSSL_VERSION_NUMBER > 0x10000000L
        ret = i2b_PublicKey_bio(bio_out, key);
#else
        
        int l;
        l = i2d_PublicKey(key, NULL);
        if (l > 0)
        {
          unsigned char* p = malloc(l);
          l = i2d_PublicKey(key, &p);
          if (l > 0)
          {
            BIO_write(bio_out, p, l);
            ret = 1;
          }
          else
            ret = 0;
          free(p);
        }
        else
          ret = 0;
#endif
      }
    }
    else
    {
      if (expem)
        ret = PEM_write_bio_PrivateKey(bio_out, key, cipher, (unsigned char *)passphrase, passphrase_len, NULL, NULL);
      else
      {
        if (passphrase == NULL)
        {
#if OPENSSL_VERSION_NUMBER > 0x10000000L
          ret = i2b_PrivateKey_bio(bio_out, key);
#else
          int l = i2d_PrivateKey(key, NULL);
          if (l > 0)
          {
            unsigned char* p = malloc(l);
            l = i2d_PrivateKey(key, &p);
            if (l > 0)
            {
              BIO_write(bio_out, p, l);
              ret = 1;
            }
            else
              ret = 0;
            free(p);
          }
          else
            ret = 0;
#endif
        }
        else
        {
          ret = i2d_PKCS8PrivateKey_bio(bio_out, key, cipher, (char *)passphrase, passphrase_len, NULL, NULL);
        }
      }
    }
  }
  else
  {
    /* export raw key format */
    switch (EVP_PKEY_type(key->type))
    {
    case EVP_PKEY_RSA:
    case EVP_PKEY_RSA2:
      if (expem)
      {
        ret = exppriv ? PEM_write_bio_RSAPrivateKey(bio_out, key->pkey.rsa, cipher, (unsigned char *)passphrase, passphrase_len, NULL, NULL)
              : PEM_write_bio_RSAPublicKey(bio_out, key->pkey.rsa);
      }
      else
      {
        ret = exppriv ? i2d_RSAPrivateKey_bio(bio_out, key->pkey.rsa)
              : i2d_RSA_PUBKEY_bio(bio_out, key->pkey.rsa);
      }
      break;
    case EVP_PKEY_DSA:
    case EVP_PKEY_DSA2:
    case EVP_PKEY_DSA3:
    case EVP_PKEY_DSA4:
      if (expem)
      {
        ret = exppriv ? PEM_write_bio_DSAPrivateKey(bio_out, key->pkey.dsa, cipher, (unsigned char *)passphrase, passphrase_len, NULL, NULL)
              : PEM_write_bio_DSA_PUBKEY(bio_out, key->pkey.dsa);
      }
      else
      {
        ret = exppriv ? i2d_DSAPrivateKey_bio(bio_out, key->pkey.dsa)
              : i2d_DSA_PUBKEY_bio(bio_out, key->pkey.dsa);
      }
      break;
    case EVP_PKEY_DH:
      if (expem)
        ret = PEM_write_bio_DHparams(bio_out, key->pkey.dh);
      else
        ret = i2d_DHparams_bio(bio_out, key->pkey.dh);
      break;
#ifndef OPENSSL_NO_EC
    case EVP_PKEY_EC:
      if (expem)
        ret = exppriv ? PEM_write_bio_ECPrivateKey(bio_out, key->pkey.ec, cipher, (unsigned char *)passphrase, passphrase_len, NULL, NULL)
              : PEM_write_bio_EC_PUBKEY(bio_out, key->pkey.ec);
      else
        ret = exppriv ? i2d_ECPrivateKey_bio(bio_out, key->pkey.ec)
              : i2d_EC_PUBKEY_bio(bio_out, key->pkey.ec);

      break;
#endif
    default:
      ret = 0;
      break;
    }
  }
  if (ret)
  {
    char * bio_mem_ptr;
    long bio_mem_len;

    bio_mem_len = BIO_get_mem_data(bio_out, &bio_mem_ptr);

    lua_pushlstring(L, bio_mem_ptr, bio_mem_len);
    ret  = 1;
  }

  if (bio_out)
  {
    BIO_free(bio_out);
  }
  return ret;
}

static LUA_FUNCTION(openssl_pkey_free)
{
  EVP_PKEY *pkey = CHECK_OBJECT(1, EVP_PKEY, "openssl.evp_pkey");
  EVP_PKEY_free(pkey);
  return 0;
}

static LUA_FUNCTION(openssl_pkey_parse)
{
  EVP_PKEY *pkey = CHECK_OBJECT(1, EVP_PKEY, "openssl.evp_pkey");
  if (pkey->pkey.ptr)
  {
    lua_newtable(L);

    AUXILIAR_SET(L, -1, "bits", EVP_PKEY_bits(pkey), integer);
    AUXILIAR_SET(L, -1, "size", EVP_PKEY_size(pkey), integer);

    switch (EVP_PKEY_type(pkey->type))
    {
    case EVP_PKEY_RSA:
    case EVP_PKEY_RSA2:
    {
      RSA* rsa = EVP_PKEY_get1_RSA(pkey);
      PUSH_OBJECT(rsa, "openssl.rsa");
      lua_setfield(L, -2, "rsa");

      AUXILIAR_SET(L, -1, "type", "rsa", string);
    }

    break;
    case EVP_PKEY_DSA:
    case EVP_PKEY_DSA2:
    case EVP_PKEY_DSA3:
    case EVP_PKEY_DSA4:
    {
      DSA* dsa = EVP_PKEY_get1_DSA(pkey);
      PUSH_OBJECT(dsa, "openssl.dsa");
      lua_setfield(L, -2, "dsa");

      AUXILIAR_SET(L, -1, "type", "dsa", string);
    }
    break;
    case EVP_PKEY_DH:
    {
      DH* dh = EVP_PKEY_get1_DH(pkey);
      PUSH_OBJECT(dh, "openssl.dh");
      lua_rawseti(L, -2, 0);

      AUXILIAR_SET(L, -1, "type", "dh", string);
    }

    break;
#ifndef OPENSSL_NO_EC
    case EVP_PKEY_EC:
    {
      const EC_KEY* ec = EVP_PKEY_get1_EC_KEY(pkey);
      PUSH_OBJECT(ec, "openssl.ec_key");
      lua_setfield(L, -2, "ec");

      AUXILIAR_SET(L, -1, "type", "ec", string);
    }

    break;
#endif
    default:
      break;
    };
    return 1;
  }
  else
    luaL_argerror(L, 1, "not assign any keypair");
  return 0;
};
/* }}} */

static const char* sPadding[] =
{
  "pkcs1",
  "sslv23",
  "no",
  "oaep",
  "x931",
#if OPENSSL_VERSION_NUMBER > 0x10000000L
  "pss",
#endif
  NULL,
};

static int iPadding[] =
{
  RSA_PKCS1_PADDING,
  RSA_SSLV23_PADDING,
  RSA_NO_PADDING,
  RSA_PKCS1_OAEP_PADDING,
  RSA_X931_PADDING,
#if OPENSSL_VERSION_NUMBER > 0x10000000L
  RSA_PKCS1_PSS_PADDING
#endif
};

static LUA_FUNCTION(openssl_pkey_encrypt)
{
  size_t dlen = 0;
  EVP_PKEY *pkey = CHECK_OBJECT(1, EVP_PKEY, "openssl.evp_pkey");
  const char *data = luaL_checklstring(L, 2, &dlen);
  int padding = auxiliar_checkoption(L, 3, "pkcs1", sPadding, iPadding);
  size_t clen = EVP_PKEY_size(pkey);
  EVP_PKEY_CTX *ctx = NULL;
  int ret = 0;

  if (openssl_is_private_key(pkey)==0)
  {
    ctx = EVP_PKEY_CTX_new(pkey,pkey->engine);
    if(EVP_PKEY_encrypt_init(ctx)==1)
    {
      if(EVP_PKEY_CTX_set_rsa_padding(ctx, padding)==1)
      {
        byte* buf = malloc(clen);
        if(EVP_PKEY_encrypt(ctx,buf,&clen,data,dlen)==1)
        {
          lua_pushlstring(L, buf, clen);
          ret = 1;
        }else
          ret = openssl_pushresult(L, 0);
        free(buf);
      }else
        ret = openssl_pushresult(L, 0);
    }else
      ret = openssl_pushresult(L, 0);
    EVP_PKEY_CTX_free(ctx);
  }else {
    luaL_argerror(L, 2, "EVP_PKEY must be public key");
  }

  return ret;
}

static LUA_FUNCTION(openssl_pkey_decrypt)
{
  size_t dlen = 0;
  EVP_PKEY *pkey = CHECK_OBJECT(1, EVP_PKEY, "openssl.evp_pkey");
  const char *data = luaL_checklstring(L, 2, &dlen);
  int padding = auxiliar_checkoption(L, 3, "pkcs1", sPadding, iPadding);
  size_t clen = EVP_PKEY_size(pkey);
  EVP_PKEY_CTX *ctx = NULL;
  int ret = 0;

  if(openssl_is_private_key(pkey)){
    ctx = EVP_PKEY_CTX_new(pkey,pkey->engine);
    if(EVP_PKEY_decrypt_init(ctx)==1)
    {
      if(EVP_PKEY_CTX_set_rsa_padding(ctx, padding)==1)
      {
        byte* buf = malloc(clen);

        if(EVP_PKEY_decrypt(ctx,buf,&clen,data,dlen)==1)
        {
          lua_pushlstring(L, buf, clen);
          ret = 1;
        }else
          ret = openssl_pushresult(L, 0);
        free(buf);
      }else
        ret = openssl_pushresult(L, 0);
    }else
      ret = openssl_pushresult(L, 0);
    EVP_PKEY_CTX_free(ctx);
  }else {
    luaL_argerror(L, 2, "EVP_PKEY must be private key");
  }
  return ret;
}

static LUA_FUNCTION(openssl_pkey_is_private)
{
  EVP_PKEY *pkey = CHECK_OBJECT(1, EVP_PKEY, "openssl.evp_pkey");
  int private = openssl_is_private_key(pkey);
  if (private == 0)
    lua_pushboolean(L, 0);
  else if (private == 1)
    lua_pushboolean(L, 1);
  else
    luaL_error(L, "openssl.evp_pkey is not support");
  return 1;
}

static LUA_FUNCTION(openssl_pkey_get_public)
{
  EVP_PKEY *pkey = CHECK_OBJECT(1, EVP_PKEY, "openssl.evp_pkey");
  int private = openssl_is_private_key(pkey);
  int ret = 0;
  if (private == 0)
    luaL_argerror(L, 1, "alreay public key");
  else
  {
    BIO* bio = BIO_new(BIO_s_mem());
    if (i2d_PUBKEY_bio(bio, pkey))
    {
      EVP_PKEY *pub = d2i_PUBKEY_bio(bio, NULL);
      PUSH_OBJECT(pub, "openssl.evp_pkey");
      ret = 1;
    }
    BIO_free(bio);
  }
  return ret;
}

static LUA_FUNCTION(openssl_dh_compute_key)
{
  const char *pub_str;
  size_t pub_len;
  EVP_PKEY *pkey;
  BIGNUM *pub;
  char *data;
  int len;
  int ret = 0;

  pkey = CHECK_OBJECT(1, EVP_PKEY, "openssl.evp_pkey");
  pub_str = luaL_checklstring(L, 1, &pub_len);

  if (!pkey || EVP_PKEY_type(pkey->type) != EVP_PKEY_DH || !pkey->pkey.dh)
  {
    luaL_argerror(L, 1, "only support DH private key");
  }

  pub = BN_bin2bn((unsigned char*)pub_str, pub_len, NULL);

  data = malloc(DH_size(pkey->pkey.dh) + 1);
  len = DH_compute_key((unsigned char*)data, pub, pkey->pkey.dh);

  if (len >= 0)
  {
    data[len] = 0;
    lua_pushlstring(L, data, len);
    ret = 1;
  }
  else
  {
    free(data);
    ret = 0;
  }

  BN_free(pub);
  return ret;
}

static LUA_FUNCTION(openssl_sign)
{
  size_t data_len;
  EVP_PKEY *pkey = CHECK_OBJECT(1, EVP_PKEY, "openssl.evp_pkey");
  const char * data = luaL_checklstring(L, 2, &data_len);
  int top = lua_gettop(L);

  const EVP_MD *mdtype = NULL;
  if (top > 2)
  {
    if (lua_isstring(L, 3))
    {
      mdtype = EVP_get_digestbyname(lua_tostring(L, 3));
    }
    else if (lua_isuserdata(L, 3))
      mdtype = CHECK_OBJECT(3, EVP_MD, "openssl.evp_digest");
    else
      luaL_argerror(L, 3, "must be string for digest alg name, or openssl.evp_digest object,default use 'sha1'");
  }
  else
    mdtype = EVP_get_digestbyname("sha1");
  if (mdtype)
  {
    int ret = 0;
    EVP_MD_CTX md_ctx;
    unsigned int siglen = EVP_PKEY_size(pkey);
    unsigned char *sigbuf = malloc(siglen + 1);

    EVP_SignInit(&md_ctx, mdtype);
    EVP_SignUpdate(&md_ctx, data, data_len);
    if (EVP_SignFinal (&md_ctx, sigbuf, &siglen, pkey))
    {
      lua_pushlstring(L, (char *)sigbuf, siglen);
      ret = 1;
    }
    free(sigbuf);
    EVP_MD_CTX_cleanup(&md_ctx);
    return ret;
  }
  else
    luaL_argerror(L, 3, "Not support digest alg");

  return 0;
}

static LUA_FUNCTION(openssl_verify)
{
  size_t data_len, signature_len;
  EVP_PKEY *pkey = CHECK_OBJECT(1, EVP_PKEY, "openssl.evp_pkey");
  const char* data = luaL_checklstring(L, 2, &data_len);
  const char* signature = luaL_checklstring(L, 3, &signature_len);

  const EVP_MD *mdtype = NULL;
  int top = lua_gettop(L);
  if (top > 3)
  {
    if (lua_isstring(L, 4))
      mdtype = EVP_get_digestbyname(lua_tostring(L, 4));
    else if (lua_isuserdata(L, 4))
      mdtype = CHECK_OBJECT(4, EVP_MD, "openssl.evp_digest");
    else
      luaL_error(L, "#4 must be nil, string, or openssl.evp_digest object");
  }
  else
    mdtype = EVP_get_digestbyname("sha1");
  if (mdtype)
  {
    int result;
    EVP_MD_CTX     md_ctx;

    EVP_VerifyInit   (&md_ctx, mdtype);
    EVP_VerifyUpdate (&md_ctx, data, data_len);
    result = EVP_VerifyFinal (&md_ctx, (unsigned char *)signature, signature_len, pkey);
    EVP_MD_CTX_cleanup(&md_ctx);
    lua_pushboolean(L, result);

    return 1;
  }
  else
    luaL_argerror(L, 4, "Not support digest alg");

  return 0;
}

static LUA_FUNCTION(openssl_seal)
{
  size_t data_len;
  const char *data = NULL;
  int nkeys = 0;
  const EVP_CIPHER *cipher = NULL;
  int top = lua_gettop(L);

  if (lua_istable(L, 1))
  {
    nkeys = lua_rawlen(L, 1);
    if (!nkeys)
    {
      luaL_argerror(L, 1, "empty array");
    }
  }
  else if (auxiliar_isclass(L, "openssl.evp_pkey", 1))
  {
    nkeys = 1;
  }
  else
    luaL_argerror(L, 1, "must be openssl.evp_pkey or unemtpy table");

  data = luaL_checklstring(L, 2, &data_len);

  cipher = get_cipher(L, 3, "rc4");

  if (cipher)
  {
    EVP_CIPHER_CTX ctx;
    int ret = 0;
    EVP_PKEY **pkeys;
    unsigned char **eks;
    int *eksl;
    int i;
    int len1, len2;
    unsigned char *buf;
    char iv[EVP_MAX_MD_SIZE] = {0};
    int ivlen = 0;

    pkeys = malloc(nkeys * sizeof(EVP_PKEY *));
    eksl = malloc(nkeys * sizeof(int));
    eks = malloc(nkeys * sizeof(char*));

    memset(eks, 0, sizeof(char*) * nkeys);

    /* get the public keys we are using to seal this data */
    if (lua_istable(L, 1))
    {
      for (i = 0; i < nkeys; i++)
      {
        lua_rawgeti(L, 1, i + 1);

        pkeys[i] =  CHECK_OBJECT(-1, EVP_PKEY, "openssl.evp_pkey");
        if (pkeys[i] == NULL)
        {
          luaL_argerror(L, 1, "table with gap");
        }
        eksl[i] = EVP_PKEY_size(pkeys[i]);
        eks[i] = malloc(eksl[i]);

        lua_pop(L, 1);
      }
    }
    else
    {
      pkeys[0] = CHECK_OBJECT(1, EVP_PKEY, "openssl.evp_pkey");
      eksl[0] = EVP_PKEY_size(pkeys[0]);
      eks[0] = malloc(eksl[0]);
    }
    EVP_CIPHER_CTX_init(&ctx);

    /* allocate one byte extra to make room for \0 */
    len1 = data_len + EVP_CIPHER_block_size(cipher) + 1;
    buf = malloc(len1);


    if (!EVP_SealInit(&ctx, cipher, eks, eksl, iv, pkeys, nkeys) || !EVP_SealUpdate(&ctx, buf, &len1, (unsigned char *)data, data_len))
    {
      luaL_error(L, "EVP_SealInit failed");
    }

    EVP_SealFinal(&ctx, buf + len1, &len2);

    if (len1 + len2 > 0)
    {
      lua_pushlstring(L, (const char*)buf, len1 + len2);
      if (lua_istable(L, 1))
      {
        lua_newtable(L);
        for (i = 0; i < nkeys; i++)
        {
          lua_pushlstring(L, (const char*)eks[i], eksl[i]);
          free(eks[i]);
          lua_rawseti(L, -2, i + 1);
        }
      }
      else
      {
        lua_pushlstring(L, (const char*)eks[0], eksl[0]);
        free(eks[0]);
      }
      lua_pushlstring(L, iv, EVP_CIPHER_CTX_iv_length(&ctx));
      
      ret = 3;
    }

    free(buf);
    free(eks);
    free(eksl);
    free(pkeys);
    EVP_CIPHER_CTX_cleanup(&ctx);
    return ret;
  }
  else
    luaL_argerror(L, 3, "Not support cipher alg");
  return 0;
}


static LUA_FUNCTION(openssl_seal_init)
{
  int nkeys = 0;
  const EVP_CIPHER *cipher = NULL;

  int top = lua_gettop(L);

  if (lua_istable(L, 1))
  {
    nkeys = lua_rawlen(L, 1);
    if (!nkeys)
    {
      luaL_argerror(L, 1, "empty array");
    }
  }
  else if (auxiliar_isclass(L, "openssl.evp_pkey", 1))
  {
    nkeys = 1;
  }
  else
    luaL_argerror(L, 1, "must be openssl.evp_pkey or unemtpy table");

  cipher = get_cipher(L, 2, "rc4");

  if (cipher)
  {
    EVP_PKEY **pkeys;
    unsigned char **eks;
    int *eksl;

    EVP_CIPHER_CTX *ctx = NULL;
    int ret = 0;

    int i;

    char iv[EVP_MAX_MD_SIZE] = {0};
    int ivlen = 0;

    pkeys = malloc(nkeys * sizeof(*pkeys));
    eksl = malloc(nkeys * sizeof(*eksl));
    eks = malloc(nkeys * sizeof(*eks));


    memset(eks, 0, sizeof(*eks) * nkeys);

    /* get the public keys we are using to seal this data */
    if (lua_istable(L, 1))
    {
      for (i = 0; i < nkeys; i++)
      {
        lua_rawgeti(L, 1, i + 1);

        pkeys[i] =  CHECK_OBJECT(-1, EVP_PKEY, "openssl.evp_pkey");
        if (pkeys[i] == NULL)
        {
          luaL_argerror(L, 1, "table with gap");
        }
        eksl[i] = EVP_PKEY_size(pkeys[i]);
        eks[i] = malloc(eksl[i]);

        lua_pop(L, 1);
      }
    }
    else
    {
      pkeys[0] = CHECK_OBJECT(1, EVP_PKEY, "openssl.evp_pkey");
      eksl[0] = EVP_PKEY_size(pkeys[0]);
      eks[0] = malloc(eksl[0]);
    }
    ctx = EVP_CIPHER_CTX_new();
    if (!EVP_EncryptInit(ctx, cipher, NULL, NULL))
    {
      luaL_error(L, "EVP_EncryptInit failed");
    }
    if (!EVP_SealInit(ctx, cipher, eks, eksl, iv, pkeys, nkeys))
    {
      luaL_error(L, "EVP_SealInit failed");
    }
    PUSH_OBJECT(ctx,"openssl.evp_cipher_ctx");
    if (lua_istable(L, 1))
    {
      lua_newtable(L);
      for (i = 0; i < nkeys; i++)
      {
        lua_pushlstring(L, (const char*)eks[i], eksl[i]);
        free(eks[i]);
        lua_rawseti(L, -2, i + 1);
      }
    }
    else
    {
      lua_pushlstring(L, (const char*)eks[0], eksl[0]);
      free(eks[0]);
    }
    lua_pushlstring(L, iv, EVP_CIPHER_CTX_iv_length(ctx));
    EVP_CIPHER_CTX_cleanup(ctx);
    EVP_CIPHER_CTX_free(ctx);
    free(eks);
    free(eksl);
    free(pkeys);
    return 3;
  } else {
    luaL_argerror(L, 2, "Not support cipher alg");
  }
  return 0;
}

static LUA_FUNCTION(openssl_seal_update) {
  EVP_CIPHER_CTX* ctx = CHECK_OBJECT(1, EVP_CIPHER_CTX, "openssl.evp_cipher_ctx");
  size_t data_len;
  const char *data = luaL_checklstring(L, 2, &data_len);
  int len = data_len + EVP_CIPHER_CTX_block_size(ctx);
  unsigned char *buf =  malloc(len);

  if (!EVP_SealUpdate(ctx, buf, &len, (unsigned char *)data, data_len)) {
    free(buf);
    luaL_error(L, "EVP_SealUpdate fail");
  }

  lua_pushlstring(L, buf, len);
  free(buf);
  return 1;
}

static LUA_FUNCTION(openssl_seal_final) {
  EVP_CIPHER_CTX* ctx = CHECK_OBJECT(1, EVP_CIPHER_CTX, "openssl.evp_cipher_ctx");
  int len = EVP_CIPHER_CTX_block_size(ctx);
  unsigned char *buf = malloc(len);


  if(!EVP_SealFinal(ctx, buf, &len))
  {
    free(buf);
    luaL_error(L, "EVP_SealFinal fail");
  }

  lua_pushlstring(L, (const char*)buf, len);
  return 1;
}

static LUA_FUNCTION(openssl_open)
{
  EVP_PKEY *pkey =  CHECK_OBJECT(1, EVP_PKEY, "openssl.evp_pkey");
  size_t data_len, ekey_len,iv_len;
  const char *data = luaL_checklstring(L, 2, &data_len);
  const char *ekey = luaL_checklstring(L, 3, &ekey_len);
  const char *iv = luaL_checklstring(L, 4, &iv_len);
  int top = lua_gettop(L);
  int ret = 0;
  int len1, len2 = 0;
  unsigned char *buf;

  EVP_CIPHER_CTX ctx;
  const EVP_CIPHER *cipher = NULL;

  cipher = get_cipher(L, 5, "rc4");

  if (cipher)
  {
    len1 = data_len + 1;
    buf = malloc(len1);

    EVP_CIPHER_CTX_init(&ctx);
    if (EVP_OpenInit(&ctx, cipher, (unsigned char *)ekey, ekey_len, iv, pkey) && EVP_OpenUpdate(&ctx, buf, &len1, (unsigned char *)data, data_len))
    {
      len2 = data_len - len1;
      if (!EVP_OpenFinal(&ctx, buf + len1, &len2) || (len1 + len2 == 0))
      {
        luaL_error(L, "EVP_OpenFinal() failed.");
        ret = 0;
      }
    }
    else
    {
      luaL_error(L, "EVP_OpenInit() failed.");
      ret = 0;
    }
    EVP_CIPHER_CTX_cleanup(&ctx);
    lua_pushlstring(L, (const char*)buf, len1 + len2);
    free(buf);
    ret = 1;
  }
  else
    luaL_argerror(L, 5, "Not support cipher alg");

  return ret;
}


static LUA_FUNCTION(openssl_open_init)
{
  EVP_PKEY *pkey =  CHECK_OBJECT(1, EVP_PKEY, "openssl.evp_pkey");
  size_t ekey_len,iv_len;
  const char *ekey = luaL_checklstring(L, 2, &ekey_len);
  const char *iv = luaL_checklstring(L, 3, &iv_len);
  int top = lua_gettop(L);
  int ret = 0;
  int len2 = 0;

  const EVP_CIPHER *cipher = NULL;

  cipher = get_cipher(L, 4, "rc4");

  if (cipher)
  {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();

    EVP_CIPHER_CTX_init(ctx);
    if (EVP_OpenInit(ctx, cipher, (unsigned char *)ekey, ekey_len, iv, pkey))
    {
      PUSH_OBJECT(ctx,"openssl.evp_cipher_ctx");
      return 1;
    }else
    {
      luaL_error(L,"EVP_OpenInit fail");
    }
  }else
    luaL_argerror(L, 5, "Not support cipher alg");
  return 0;
};

static LUA_FUNCTION(openssl_open_update) {
  EVP_CIPHER_CTX* ctx = CHECK_OBJECT(1, EVP_CIPHER_CTX, "openssl.evp_cipher_ctx");
  size_t data_len;
  const char* data = luaL_checklstring(L, 2, &data_len);

  int len = EVP_CIPHER_CTX_block_size(ctx) + data_len;
  unsigned char *buf = malloc(len);

  if (EVP_OpenUpdate(ctx, buf, &len, (unsigned char *)data, data_len))
  {
    lua_pushlstring(L, (const char*)buf, len);
  }else
    luaL_error(L,"EVP_OpenUpdate fail");

  free(buf);
  return 1;
}

static LUA_FUNCTION(openssl_open_final) {
  EVP_CIPHER_CTX* ctx = CHECK_OBJECT(1, EVP_CIPHER_CTX, "openssl.evp_cipher_ctx");
  int len = EVP_CIPHER_CTX_block_size(ctx);
  unsigned char *buf = malloc(len);
  
  if (!EVP_OpenFinal(ctx, buf, &len))
  {
    free(buf);
    return openssl_pushresult(L, 0);
  }

  lua_pushlstring(L, (const char*)buf, len);
  free(buf);
  return 1;
}

static luaL_Reg pkey_funcs[] =
{
  {"is_private",    openssl_pkey_is_private},
  {"get_public",    openssl_pkey_get_public},

  {"export",      openssl_pkey_export},
  {"parse",     openssl_pkey_parse},
  {"bits",      openssl_pkey_bits},

  {"encrypt",     openssl_pkey_encrypt},
  {"decrypt",     openssl_pkey_decrypt},
  {"sign",      openssl_sign},
  {"verify",      openssl_verify},

  {"seal",    openssl_seal},
  {"open",    openssl_open},

  {"compute_key",   openssl_dh_compute_key},

  {"__gc",      openssl_pkey_free},
  {"__tostring",    auxiliar_tostring},

  {NULL,      NULL},
};

static const luaL_Reg R[] =
{
  {"read",          openssl_pkey_read},
  {"new",           openssl_pkey_new},

  {"seal",          openssl_seal},
  {"seal_init",     openssl_seal_init},
  {"seal_update",   openssl_seal_update},
  {"seal_final",    openssl_seal_final},
  {"open",          openssl_open},
  {"open_init",     openssl_open_init},
  {"open_update",   openssl_open_update},
  {"open_final",    openssl_open_final},

  {"get_public",    openssl_pkey_get_public},
  {"is_private",    openssl_pkey_is_private},
  {"export",        openssl_pkey_export},
  {"parse",         openssl_pkey_parse},
  {"bits",          openssl_pkey_bits},

  {"encrypt",     openssl_pkey_encrypt},
  {"decrypt",     openssl_pkey_decrypt},
  {"sign",        openssl_sign},
  {"verify",      openssl_verify},

  {"compute_key",   openssl_dh_compute_key},

  {NULL,  NULL}
};

int luaopen_pkey(lua_State *L)
{
  auxiliar_newclass(L, "openssl.evp_pkey", pkey_funcs);

  lua_newtable(L);
  luaL_setfuncs(L, R, 0);
  lua_pushliteral(L, "version");    /** version */
  lua_pushliteral(L, MYVERSION);
  lua_settable(L, -3);

  return 1;
}


