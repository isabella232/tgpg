/* cryptglue.c - Crypto glue layer using libgcrypt.
   Copyright (C) 2007,2015 g10 Code GmbH

   This file is part of TGPG.

   TGPG is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   TPGP is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.  */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include <gcrypt.h>

#include "tgpgdefs.h"
#include "cryptglue.h"

/* The size of the buffer we allocate for hash contexts. */
#define HASH_BUFFERSIZE 1024



/* Map gpg-error codes to tgpg error codes.  */
static int
maperr (gpg_error_t err)
{
  return err? TGPG_CRYPT_ERR : 0;
}




/*

     P U B K E Y   F u n c t i o n s

*/

/* Return the number of parameters required to encrypt using the
   public key algorithm with ALGO.  ALGO is of course an OpenPGP id.
   The function returns 0 for not supported algorithms.  */
unsigned int
_tgpg_pk_get_nenc (int algo)
{
  switch (algo)
    {
    case PK_ALGO_RSA: return 1;
    case PK_ALGO_ELG: return 2;
    default: return 0;
    }
}

/* Return the number of parameters required to sign using the
   public key algorithm with ALGO.  ALGO is of course an OpenPGP id.
   The function returns 0 for not supported algorithms.  */
unsigned int
_tgpg_pk_get_nsig (int algo)
{
  switch (algo)
    {
    case PK_ALGO_RSA: return 1;
    case PK_ALGO_DSA: return 2;
    default: return 0;
    }
}



/* Run a decrypt operation on the data in ENCDAT using the provided key
   SECKEY and algorithm ALGO.  On success the result is stored as a
   new allocated buffer at the address R_PLAN and its length at
   R_PLAINLEN.  On error PLAIN and R_PLAINLEN are set to NULL/0.*/
int
_tgpg_pk_decrypt (int algo, tgpg_mpi_t seckey, tgpg_mpi_t encdat,
                  char **r_plain, size_t *r_plainlen)
{
  int rc;
  gcry_sexp_t s_plain, s_data, s_key;
  const char *result;
  size_t resultlen;

  *r_plain = NULL;
  *r_plainlen = 0;

  if (algo == PK_ALGO_RSA)
    {
      rc = gcry_sexp_build (&s_data, NULL, "(enc-val(rsa(a%b)))",
                            (int)encdat[0].valuelen, encdat[0].value);
      if (rc)
        return TGPG_INV_DATA;

      rc = gcry_sexp_build (&s_key, NULL,
			    "(private-key(rsa(n%b)(e%b)(d%b)(p%b)(q%b)(u%b)))",
                            (int)seckey[0].valuelen, seckey[0].value,
                            (int)seckey[1].valuelen, seckey[1].value,
                            (int)seckey[2].valuelen, seckey[2].value,
                            (int)seckey[3].valuelen, seckey[3].value,
                            (int)seckey[4].valuelen, seckey[4].value,
                            (int)seckey[5].valuelen, seckey[5].value);
      if (rc)
        {
          gcry_sexp_release (s_data);
          return TGPG_INV_DATA;
        }
    }
  else
    return TGPG_INV_ALGO;

  rc = gcry_pk_decrypt (&s_plain, s_data, s_key);
  gcry_sexp_release (s_data);
  gcry_sexp_release (s_key);
  if (rc)
    return TGPG_CRYPT_ERR;

  result = gcry_sexp_nth_data (s_plain, 0, &resultlen);
  if (!result || !resultlen)
    {
      gcry_sexp_release (s_plain);
      return TGPG_CRYPT_ERR;
    }

  *r_plain = xtrymalloc (resultlen);
  if (!*r_plain)
    {
      gcry_sexp_release (s_plain);
      return TGPG_SYSERROR;
    }

  memcpy (*r_plain, result, resultlen);
  *r_plainlen = resultlen;
  gcry_sexp_release (s_plain);
  return rc;
}


/* Run an encrypt operation on PLAIN of length PLAINLEN using the
   algorithm ALGO and key PUBKEY.  On success, the result is stored in
   an allocated array R_ENCDAT with R_ENCLEN elements.  */
int
_tgpg_pk_encrypt (int algo, tgpg_mpi_t pubkey,
                  char *plain, size_t plainlen,
                  tgpg_mpi_t *r_encdat, size_t *r_enclen)
{
  int rc;
  int i;
  gcry_sexp_t s_plain, s_key, s_cipher, s_list;

  switch (algo)
    {
    case PK_ALGO_RSA:
      rc = gcry_sexp_build (&s_plain, NULL,
                            "(data(flags)(value%b))",
                            (int) plainlen, plain);
      if (rc)
        return TGPG_INV_DATA;

      rc = gcry_sexp_build (&s_key, NULL,
			    "(public-key(rsa(n%b)(e%b)))",
                            (int) pubkey[0].valuelen, pubkey[0].value,
                            (int) pubkey[1].valuelen, pubkey[1].value);
      if (rc)
        {
          gcry_sexp_release (s_plain);
          return TGPG_INV_DATA;
        }
      break;

    default:
      return TGPG_INV_ALGO;
    }

  rc = gcry_pk_encrypt (&s_cipher, s_plain, s_key);
  gcry_sexp_release (s_plain);
  gcry_sexp_release (s_key);
  if (rc)
    {
      rc = TGPG_CRYPT_ERR;
      goto leave;
    }

  *r_enclen = _tgpg_pk_get_nenc (algo);
  *r_encdat = xtrymalloc (*r_enclen * sizeof **r_encdat);
  if (r_encdat == NULL)
    {
      rc = TGPG_SYSERROR;
      goto leave;
    }

  for (i = 0; i < *r_enclen; i++)
    {
      const char *keys = "ab";
      tgpg_mpi_t r = &(*r_encdat)[i];

      s_list = gcry_sexp_find_token (s_cipher, &keys[i], 1);

      errno = 0;
      r->value =
        gcry_sexp_nth_buffer (s_list, 1, &r->valuelen);
      if (r->value == NULL)
        {
          rc = errno != 0 ? TGPG_SYSERROR : TGPG_BUG;
          goto leave;
        }

      r->nbits = r->valuelen << 3;
      gcry_sexp_release (s_list);
    }

 leave:
  gcry_sexp_release (s_cipher);
  return rc;
}


/*

     C I P H E R   F u n c t i o n s

*/

/* Return the block length in bytes of the cipher ALGO.  */
unsigned int
_tgpg_cipher_blocklen (int algo)
{
  return gcry_cipher_get_algo_blklen (algo);
}


/* Return the key length in bytes of the cipher ALGO.  */
unsigned int
_tgpg_cipher_keylen (int algo)
{
  return gcry_cipher_get_algo_keylen (algo);
}


/* Core of the en- and decrypt functions.  With DO_ENCRYPT true an
   encryption is done, otherwise it will decrypt.  */
static int
cipher_endecrypt (int do_encrypt,
                  int algo, enum cipher_modes mode,
                  const void *key, size_t keylen,
                  const void *iv, size_t ivlen,
                  char *prefix, size_t prefixlen,
                  void *outbuf, size_t outbufsize,
                  const void * inbuf, size_t inbuflen)
{
  gpg_error_t err;
  int flags = 0;
  int pgp_cipher_init = 0;
  gcry_cipher_hd_t hd;
  size_t bs = _tgpg_cipher_blocklen (algo);

  switch (mode)
    {
    case CIPHER_MODE_CBC: mode = GCRY_CIPHER_MODE_CBC; break;
    case CIPHER_MODE_CFB: mode = GCRY_CIPHER_MODE_CFB; break;
    case CIPHER_MODE_CFB_PGP:
      flags |= GCRY_CIPHER_ENABLE_SYNC;
      /* Fallthrough.  */
    case CIPHER_MODE_CFB_MDC:
      mode = GCRY_CIPHER_MODE_CFB;
      pgp_cipher_init = 1;
      if (prefixlen != bs + 2)
        return TGPG_BUG;
      break;
    default: return TGPG_BUG;
    }

  err = gcry_cipher_open (&hd, algo, mode, flags);
  if (err)
    goto leave;
  err = gcry_cipher_setkey (hd, key, keylen);
  if (err)
    goto leave;
  err = gcry_cipher_setiv (hd, iv, ivlen);
  if (err)
    goto leave;

  /* Handle cipher initialization and re-synchronization.  */
  if (pgp_cipher_init)
    {
      if (do_encrypt)
        {
          /* Check that the last two octets are repeated.  */
          if (prefix[bs-2] != prefix[bs] || prefix[bs-1] != prefix[bs+1])
            {
              err = TGPG_BUG;
              goto leave;
            }

          err = gcry_cipher_encrypt (hd, outbuf, outbufsize,
                                     prefix, prefixlen);
          outbuf += prefixlen, outbufsize -= prefixlen;
        }
      else
        {
          err = gcry_cipher_decrypt (hd, prefix, prefixlen,
                                     inbuf, prefixlen);
          if (err)
            goto leave;
          inbuf += prefixlen, inbuflen -= prefixlen;

          /* The last two octets are repeated.  */
          if (prefix[bs-2] != prefix[bs] || prefix[bs-1] != prefix[bs+1])
            err = TGPG_WRONG_KEY;
        }
      if (err)
        goto leave;

      err = gcry_cipher_sync (hd);
      if (err)
        goto leave;
    }

  err = \
    (do_encrypt ? gcry_cipher_encrypt : gcry_cipher_decrypt)    \
      (hd, outbuf, outbufsize, inbuf, inbuflen);

 leave:
  gcry_cipher_close (hd);
  return maperr (err);
}

/* Decrypt the data at INBUF of length INBUFLEN and write them to the
   caller provided OUTBUF which has a length of OUTBUFSIZE.  ALGO is
   the algorithm to use, MODE, the encryption modus, KEY and KEYLEN
   describes the key and IV and IVLEN the IV.  Returns an error
   code.  */
int
_tgpg_cipher_decrypt (int algo, enum cipher_modes mode,
                      const void *key, size_t keylen,
                      const void *iv, size_t ivlen,
                      char *prefix, size_t prefixlen,
                      void *outbuf, size_t outbufsize,
                      const void * inbuf, size_t inbuflen)
{
  return cipher_endecrypt (0, algo, mode, key, keylen, iv, ivlen,
                           prefix, prefixlen,
                           outbuf, outbufsize, inbuf, inbuflen);
}

/* Encrypt the data at INBUF of length INBUFLEN and write them to the
   caller provided OUTBUF which has a length of OUTBUFSIZE.  ALGO is
   the algorithm to use, MODE, the encryption modus, KEY and KEYLEN
   describes the key and IV and IVLEN the IV.  Returns an error
   code.  */
int
_tgpg_cipher_encrypt (int algo, enum cipher_modes mode,
                      const void *key, size_t keylen,
                      const void *iv, size_t ivlen,
                      char *prefix, size_t prefixlen,
                      void *outbuf, size_t outbufsize,
                      const void *inbuf, size_t inbuflen)
{
  return cipher_endecrypt (1, algo, mode, key, keylen, iv, ivlen,
                           prefix, prefixlen,
                           outbuf, outbufsize, inbuf, inbuflen);
}




/*

     H A S H   F u n c t i o n s

*/

/* Hash LENGTH bytes of BUFFER using the hash algorithm ALGO and put
   the result into the caller provided buffer DIGEST.  DIGEST must be
   allocated large enough to hold the entire digest. DIGESTLEN is only
   used for cross-checking.  */
void
_tgpg_hash_buffer (int algo, unsigned char *digest, size_t digestlen,
                   const void *buffer, size_t length)
{
  gcry_md_hash_buffer (algo, digest, buffer, length);
}


/* Create a new context for hash operations using the hash algorithm
   ALGO.  On success a new handle is return at RCTX and the return
   value will be 0; on error NULL is stored at RCTX and an error code
   is returned.  The caller needs to release the context after use by
   calling _tgpg_hash_close.  */
int
_tgpg_hash_open (hash_t *rctx, int algo, unsigned int flags)
{
  gpg_error_t err;
  gcry_md_hd_t hd;
  hash_t ctx;

  *rctx = NULL;

  err = gcry_md_open (&hd, algo,
                      (flags & HASH_FLAG_SECURE)? GCRY_MD_FLAG_SECURE : 0);
  if (err)
    return maperr (err);
  ctx = xtrymalloc (sizeof *ctx - 1 + HASH_BUFFERSIZE);
  if (!ctx)
    {
      int tmperr = errno;
      gcry_md_close (hd);
      errno = tmperr;
      return TGPG_SYSERROR;
    }
  ctx->handle = hd;
  ctx->secure = !!(flags & HASH_FLAG_SECURE);
  ctx->digestlen = gcry_md_get_algo_dlen (algo);
  ctx->buffersize = HASH_BUFFERSIZE;
  ctx->bufferpos = 0;

  *rctx = ctx;
  return 0;
}


/* Close the hash context CTX.  Passing NULL is a nop.  */
void
_tgpg_hash_close (hash_t ctx)
{
  if (ctx)
    {
      gcry_md_hd_t hd = ctx->handle;

      gcry_md_close (hd);
      if (ctx->secure)
        wipememory (ctx->buffer, ctx->buffersize);
      xfree (ctx);
    }
}

/* Reset the hash context and discard any buffered stuff.  This may be
   used instead of a close, open sequence if retaining the same
   context is desired.  */
void
_tgpg_hash_reset (hash_t ctx)
{
  gcry_md_hd_t hd = ctx->handle;

  gcry_md_reset (hd);
  ctx->bufferpos = 0;

}

/* Hash LENGTH bytes from BUFFER into the hash context CTX.  */
void
_tgpg_hash_write (hash_t ctx, const void *buffer, size_t length)
{
  gcry_md_hd_t hd = ctx->handle;

  if (ctx->bufferpos)
    {
      gcry_md_write (hd, ctx->buffer, ctx->bufferpos);
      ctx->bufferpos = 0;
    }
  if (buffer && length)
    gcry_md_write (hd, buffer, length);
}

/* Finalize the hash digest and return it.  The returned value is
   valid as long as the context is valid and no hash_reset has been
   used.  The length of the hash may be determined by using the
   hash_digestlen macro. */
const void *
_tgpg_hash_read (hash_t ctx)
{
  gcry_md_hd_t hd = ctx->handle;

  return gcry_md_read (hd, 0);
}

void
_tgpg_randomize (unsigned char *buffer, size_t length)
{
  gcry_randomize (buffer, length, GCRY_STRONG_RANDOM);
}
