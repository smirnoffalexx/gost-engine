/*
 * Copyright (C) 2018 vt@altlinux.org. All Rights Reserved.
 *
 * Contents licensed under the terms of the OpenSSL license
 * See https://www.openssl.org/source/license.html for details
 */

#include "gost_grasshopper_cipher.h"
#include "gost_grasshopper_defines.h"
#include "gost_grasshopper_math.h"
#include "gost_grasshopper_core.h"
#include "e_gost_err.h"
#include "gost_lcl.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/asn1.h>
#include <string.h>

#define T(e) if (!(e)) {\
	ERR_print_errors_fp(stderr);\
	OpenSSLDie(__FILE__, __LINE__, #e);\
    }

#define cRED	"\033[1;31m"
#define cDRED	"\033[0;31m"
#define cGREEN	"\033[1;32m"
#define cDGREEN	"\033[0;32m"
#define cBLUE	"\033[1;34m"
#define cDBLUE	"\033[0;34m"
#define cNORM	"\033[m"
#define TEST_ASSERT(e) {if ((test = (e))) \
		 printf(cRED "Test FAILED\n" cNORM); \
	     else \
		 printf(cGREEN "Test passed\n" cNORM);}

/* Test key from both GOST R 34.12-2015 and GOST R 34.13-2015. */
static const unsigned char K[32] = {
    0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
    0xfe,0xdc,0xba,0x98,0x76,0x54,0x32,0x10,0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,
};

/* Plaintext from GOST R 34.13-2015 A.1.
 * First 16 bytes is vector (a) from GOST R 34.12-2015 A.1. */
static const unsigned char P[] = {
    0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x00,0xff,0xee,0xdd,0xcc,0xbb,0xaa,0x99,0x88,
    0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xee,0xff,0x0a,
    0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xee,0xff,0x0a,0x00,
    0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xee,0xff,0x0a,0x00,0x11,
};
/* Extended plaintext from tc26 acpkm Kuznyechik test vector */
static const unsigned char P_acpkm[] = {
    0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x00,0xFF,0xEE,0xDD,0xCC,0xBB,0xAA,0x99,0x88,
    0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xEE,0xFF,0x0A,
    0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xEE,0xFF,0x0A,0x00,
    0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xEE,0xFF,0x0A,0x00,0x11,
    0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xEE,0xFF,0x0A,0x00,0x11,0x22,
    0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xEE,0xFF,0x0A,0x00,0x11,0x22,0x33,
    0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xEE,0xFF,0x0A,0x00,0x11,0x22,0x33,0x44,
};
/* OMAC-ACPKM test vector from R 1323565.1.017-2018 A.4.1 */
static const unsigned char P_omac_acpkm1[] = {
    0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x00,0xFF,0xEE,0xDD,0xCC,0xBB,0xAA,0x99,0x88,
    0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
};
/* OMAC-ACPKM test vector from R 1323565.1.017-2018 A.4.2 */
static const unsigned char P_omac_acpkm2[] = {
    0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x00,0xFF,0xEE,0xDD,0xCC,0xBB,0xAA,0x99,0x88,
    0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xEE,0xFF,0x0A,
    0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xEE,0xFF,0x0A,0x00,
    0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xEE,0xFF,0x0A,0x00,0x11,
    0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xEE,0xFF,0x0A,0x00,0x11,0x22,
};
static const unsigned char E_ecb[] = {
    /* ECB test vectors from GOST R 34.13-2015  A.1.1 */
    /* first 16 bytes is vector (b) from GOST R 34.12-2015 A.1 */
    0x7f,0x67,0x9d,0x90,0xbe,0xbc,0x24,0x30,0x5a,0x46,0x8d,0x42,0xb9,0xd4,0xed,0xcd,
    0xb4,0x29,0x91,0x2c,0x6e,0x00,0x32,0xf9,0x28,0x54,0x52,0xd7,0x67,0x18,0xd0,0x8b,
    0xf0,0xca,0x33,0x54,0x9d,0x24,0x7c,0xee,0xf3,0xf5,0xa5,0x31,0x3b,0xd4,0xb1,0x57,
    0xd0,0xb0,0x9c,0xcd,0xe8,0x30,0xb9,0xeb,0x3a,0x02,0xc4,0xc5,0xaa,0x8a,0xda,0x98,
};
static const unsigned char E_ctr[] = {
    /* CTR test vectors from GOST R 34.13-2015  A.1.2 */
    0xf1,0x95,0xd8,0xbe,0xc1,0x0e,0xd1,0xdb,0xd5,0x7b,0x5f,0xa2,0x40,0xbd,0xa1,0xb8,
    0x85,0xee,0xe7,0x33,0xf6,0xa1,0x3e,0x5d,0xf3,0x3c,0xe4,0xb3,0x3c,0x45,0xde,0xe4,
    0xa5,0xea,0xe8,0x8b,0xe6,0x35,0x6e,0xd3,0xd5,0xe8,0x77,0xf1,0x35,0x64,0xa3,0xa5,
    0xcb,0x91,0xfa,0xb1,0xf2,0x0c,0xba,0xb6,0xd1,0xc6,0xd1,0x58,0x20,0xbd,0xba,0x73,
};
static const unsigned char E_acpkm[] = {
    0xF1,0x95,0xD8,0xBE,0xC1,0x0E,0xD1,0xDB,0xD5,0x7B,0x5F,0xA2,0x40,0xBD,0xA1,0xB8,
    0x85,0xEE,0xE7,0x33,0xF6,0xA1,0x3E,0x5D,0xF3,0x3C,0xE4,0xB3,0x3C,0x45,0xDE,0xE4,
    0x4B,0xCE,0xEB,0x8F,0x64,0x6F,0x4C,0x55,0x00,0x17,0x06,0x27,0x5E,0x85,0xE8,0x00,
    0x58,0x7C,0x4D,0xF5,0x68,0xD0,0x94,0x39,0x3E,0x48,0x34,0xAF,0xD0,0x80,0x50,0x46,
    0xCF,0x30,0xF5,0x76,0x86,0xAE,0xEC,0xE1,0x1C,0xFC,0x6C,0x31,0x6B,0x8A,0x89,0x6E,
    0xDF,0xFD,0x07,0xEC,0x81,0x36,0x36,0x46,0x0C,0x4F,0x3B,0x74,0x34,0x23,0x16,0x3E,
    0x64,0x09,0xA9,0xC2,0x82,0xFA,0xC8,0xD4,0x69,0xD2,0x21,0xE7,0xFB,0xD6,0xDE,0x5D,
};
/* Test vector from R 23565.1.017-2018 A.4.2.
 * Key material from ACPKM-Master(K,768,3) for OMAC-ACPKM. */
static const unsigned char E_acpkm_master[] = {
    0x0C,0xAB,0xF1,0xF2,0xEF,0xBC,0x4A,0xC1,0x60,0x48,0xDF,0x1A,0x24,0xC6,0x05,0xB2,
    0xC0,0xD1,0x67,0x3D,0x75,0x86,0xA8,0xEC,0x0D,0xD4,0x2C,0x45,0xA4,0xF9,0x5B,0xAE,
    0x0F,0x2E,0x26,0x17,0xE4,0x71,0x48,0x68,0x0F,0xC3,0xE6,0x17,0x8D,0xF2,0xC1,0x37,
    0xC9,0xDD,0xA8,0x9C,0xFF,0xA4,0x91,0xFE,0xAD,0xD9,0xB3,0xEA,0xB7,0x03,0xBB,0x31,
    0xBC,0x7E,0x92,0x7F,0x04,0x94,0x72,0x9F,0x51,0xB4,0x9D,0x3D,0xF9,0xC9,0x46,0x08,
    0x00,0xFB,0xBC,0xF5,0xED,0xEE,0x61,0x0E,0xA0,0x2F,0x01,0x09,0x3C,0x7B,0xC7,0x42,
    0xD7,0xD6,0x27,0x15,0x01,0xB1,0x77,0x77,0x52,0x63,0xC2,0xA3,0x49,0x5A,0x83,0x18,
    0xA8,0x1C,0x79,0xA0,0x4F,0x29,0x66,0x0E,0xA3,0xFD,0xA8,0x74,0xC6,0x30,0x79,0x9E,
    0x14,0x2C,0x57,0x79,0x14,0xFE,0xA9,0x0D,0x3B,0xC2,0x50,0x2E,0x83,0x36,0x85,0xD9,
};
static const unsigned char P_acpkm_master[sizeof(E_acpkm_master)] = { 0 };
/*
 * Other modes (ofb, cbc, cfb) is impossible to test to match GOST R
 * 34.13-2015 test vectors exactly, due to these vectors having exceeding
 * IV length value (m) = 256 bits, while openssl have hard-coded limit
 * of maximum IV length of 128 bits (EVP_MAX_IV_LENGTH).
 * Also, current grasshopper code having fixed IV length of 128 bits.
 *
 * Thus, new test vectors are generated with truncated 128-bit IV using
 * canonical GOST implementation from TC26.
 */
static const unsigned char E_ofb[] = {
    /* OFB test vector generated from canonical implementation */
    0x81,0x80,0x0a,0x59,0xb1,0x84,0x2b,0x24,0xff,0x1f,0x79,0x5e,0x89,0x7a,0xbd,0x95,
    0x77,0x91,0x46,0xdb,0x2d,0x93,0xa9,0x4e,0xd9,0x3c,0xf6,0x8b,0x32,0x39,0x7f,0x19,
    0xe9,0x3c,0x9e,0x57,0x44,0x1d,0x87,0x05,0x45,0xf2,0x40,0x36,0xa5,0x8c,0xee,0xa3,
    0xcf,0x3f,0x00,0x61,0xd5,0x64,0x23,0x54,0x5b,0x96,0x0d,0x86,0x4c,0xc8,0x68,0xda,
};
static const unsigned char E_cbc[] = {
    /* CBC test vector generated from canonical implementation */
    0x68,0x99,0x72,0xd4,0xa0,0x85,0xfa,0x4d,0x90,0xe5,0x2e,0x3d,0x6d,0x7d,0xcc,0x27,
    0xab,0xf1,0x70,0xb2,0xb2,0x26,0xc3,0x01,0x0c,0xcf,0xa1,0x36,0xd6,0x59,0xcd,0xaa,
    0xca,0x71,0x92,0x72,0xab,0x1d,0x43,0x8e,0x15,0x50,0x7d,0x52,0x1e,0xcd,0x55,0x22,
    0xe0,0x11,0x08,0xff,0x8d,0x9d,0x3a,0x6d,0x8c,0xa2,0xa5,0x33,0xfa,0x61,0x4e,0x71,
};
static const unsigned char E_cfb[] = {
    /* CFB test vector generated from canonical implementation */
    0x81,0x80,0x0a,0x59,0xb1,0x84,0x2b,0x24,0xff,0x1f,0x79,0x5e,0x89,0x7a,0xbd,0x95,
    0x68,0xc1,0xb9,0x9c,0x4d,0xf5,0x9c,0xc7,0x95,0x1e,0x37,0x39,0xb5,0xb3,0xcd,0xbf,
    0x07,0x3f,0x4d,0xd2,0xd6,0xde,0xb3,0xcf,0xb0,0x26,0x54,0x5f,0x7a,0xf1,0xd8,0xe8,
    0xe1,0xc8,0x52,0xe9,0xa8,0x56,0x71,0x62,0xdb,0xb5,0xda,0x7f,0x66,0xde,0xa9,0x26,
};

static const unsigned char iv_ctr[]	= { 0x12,0x34,0x56,0x78,0x90,0xab,0xce,0xf0,
					    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };
/* Truncated to 128-bits IV from GOST examples. */
static const unsigned char iv_128bit[]	= { 0x12,0x34,0x56,0x78,0x90,0xab,0xce,0xf0,
					    0xa1,0xb2,0xc3,0xd4,0xe5,0xf0,0x01,0x12 };
/* Universal IV for ACPKM-Master. */
static const unsigned char iv_acpkm_m[]	= { 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
					    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };
static const unsigned char MAC_omac[] = { 0x33,0x6f,0x4d,0x29,0x60,0x59,0xfb,0xe3 };
static const unsigned char MAC_omac_acpkm1[] = {
    0xB5,0x36,0x7F,0x47,0xB6,0x2B,0x99,0x5E,0xEB,0x2A,0x64,0x8C,0x58,0x43,0x14,0x5E,
};
static const unsigned char MAC_omac_acpkm2[] = {
    0xFB,0xB8,0xDC,0xEE,0x45,0xBE,0xA6,0x7C,0x35,0xF5,0x8C,0x57,0x00,0x89,0x8E,0x5D,
};

struct testcase {
    const char *name;
    const EVP_CIPHER *(*type)(void);
    int stream;
    const unsigned char *plaintext;
    const unsigned char *expected;
    size_t size;
    const unsigned char *iv;
    size_t iv_size;
    int acpkm;
};
static struct testcase testcases[] = {
    { "ecb", cipher_gost_grasshopper_ecb, 0, P,  E_ecb,  sizeof(P),  NULL,       0, 0 },
    { "ctr", cipher_gost_grasshopper_ctr, 1, P,  E_ctr,  sizeof(P),  iv_ctr,     sizeof(iv_ctr), 0 },
    { "ctr-no-acpkm", cipher_gost_grasshopper_ctracpkm, 1, P,   E_ctr,   sizeof(P),       iv_ctr, sizeof(iv_ctr), 0 },
    { "ctracpkm", cipher_gost_grasshopper_ctracpkm, 1, P_acpkm, E_acpkm, sizeof(P_acpkm), iv_ctr, sizeof(iv_ctr), 256 / 8 },
    { "acpkm-Master", cipher_gost_grasshopper_ctracpkm, 0, P_acpkm_master, E_acpkm_master, sizeof(P_acpkm_master),
	iv_acpkm_m, sizeof(iv_acpkm_m), 768 / 8 },
    { "ofb", cipher_gost_grasshopper_ofb, 1, P,  E_ofb,  sizeof(P),  iv_128bit,  sizeof(iv_128bit), 0 },
    { "cbc", cipher_gost_grasshopper_cbc, 0, P,  E_cbc,  sizeof(P),  iv_128bit,  sizeof(iv_128bit), 0 },
    { "cfb", cipher_gost_grasshopper_cfb, 0, P,  E_cfb,  sizeof(P),  iv_128bit,  sizeof(iv_128bit), 0 },
    NULL
};

static void hexdump(const void *ptr, size_t len)
{
    const unsigned char *p = ptr;
    size_t i, j;

    for (i = 0; i < len; i += j) {
	for (j = 0; j < 16 && i + j < len; j++)
	    printf("%s%02x", j? "" : " ", p[i + j]);
    }
    printf("\n");
}

static int test_block(const EVP_CIPHER *type, const char *name,
    const unsigned char *pt, const unsigned char *exp, size_t size,
    const unsigned char *iv, size_t iv_size, int acpkm,
    int inplace)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    const char *standard = acpkm? "R 23565.1.017-2018" : "GOST R 34.13-2015";
    unsigned char c[size];
    int outlen, tmplen;
    int ret = 0, test;

    OPENSSL_assert(ctx);
    printf("Encryption test from %s [%s] %s\n", standard, name,
	inplace ? "in-place" : "out-of-place");
    /* test with single big chunk */
    EVP_CIPHER_CTX_init(ctx);
    T(EVP_CipherInit_ex(ctx, type, NULL, K, iv, 1));
    T(EVP_CIPHER_CTX_set_padding(ctx, 0));
    if (inplace)
	memcpy(c, pt, size);
    else
	memset(c, 0, sizeof(c));
    if (acpkm)
	T(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_KEY_MESH, acpkm, NULL));
    T(EVP_CipherUpdate(ctx, c, &outlen, inplace? c : pt, size));
    T(EVP_CipherFinal_ex(ctx, c + outlen, &tmplen));
    EVP_CIPHER_CTX_cleanup(ctx);
    printf("  c[%d] = ", outlen);
    hexdump(c, outlen);

    TEST_ASSERT(outlen != size || memcmp(c, exp, size));
    ret |= test;

    /* test with small chunks of block size */
    printf("Chunked encryption test from %s [%s] %s\n", standard, name,
	inplace ? "in-place" : "out-of-place");
    int blocks = size / GRASSHOPPER_BLOCK_SIZE;
    int z;
    EVP_CIPHER_CTX_init(ctx);
    T(EVP_CipherInit_ex(ctx, type, NULL, K, iv, 1));
    T(EVP_CIPHER_CTX_set_padding(ctx, 0));
    if (inplace)
	memcpy(c, pt, size);
    else
	memset(c, 0, sizeof(c));
    if (acpkm)
	T(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_KEY_MESH, acpkm, NULL));
    for (z = 0; z < blocks; z++) {
	int offset = z * GRASSHOPPER_BLOCK_SIZE;
	int sz = GRASSHOPPER_BLOCK_SIZE;

	T(EVP_CipherUpdate(ctx, c + offset, &outlen, (inplace ? c : pt) + offset, sz));
    }
    outlen = z * GRASSHOPPER_BLOCK_SIZE;
    T(EVP_CipherFinal_ex(ctx, c + outlen, &tmplen));
    EVP_CIPHER_CTX_cleanup(ctx);
    printf("  c[%d] = ", outlen);
    hexdump(c, outlen);

    TEST_ASSERT(outlen != size || memcmp(c, exp, size));
    ret |= test;

    /* test with single big chunk */
    printf("Decryption test from %s [%s] %s\n", standard, name,
	inplace ? "in-place" : "out-of-place");
    EVP_CIPHER_CTX_init(ctx);
    T(EVP_CipherInit_ex(ctx, type, NULL, K, iv, 0));
    T(EVP_CIPHER_CTX_set_padding(ctx, 0));
    if (inplace)
	memcpy(c, exp, size);
    else
	memset(c, 0, sizeof(c));
    if (acpkm)
	T(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_KEY_MESH, acpkm, NULL));
    T(EVP_CipherUpdate(ctx, c, &outlen, inplace ? c : exp, size));
    T(EVP_CipherFinal_ex(ctx, c + outlen, &tmplen));
    EVP_CIPHER_CTX_cleanup(ctx);
    EVP_CIPHER_CTX_free(ctx);
    printf("  d[%d] = ", outlen);
    hexdump(c, outlen);

    TEST_ASSERT(outlen != size || memcmp(c, pt, size));
    ret |= test;

    return ret;
}

static int test_stream(const EVP_CIPHER *type, const char *name,
    const unsigned char *pt, const unsigned char *exp, size_t size,
    const unsigned char *iv, size_t iv_size, int acpkm)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    const char *standard = acpkm? "R 23565.1.017-2018" : "GOST R 34.13-2015";
    int ret = 0, test;
    int z;

    OPENSSL_assert(ctx);
    /* Cycle through all lengths from 1 upto maximum size */
    printf("Stream encryption test from %s [%s] \n", standard, name);
    for (z = 1; z <= size; z++) {
	unsigned char c[size];
	int outlen, tmplen;
	int sz = 0;
	int i;

	EVP_CIPHER_CTX_init(ctx);
	T(EVP_CipherInit_ex(ctx, type, NULL, K, iv, 1));
	T(EVP_CIPHER_CTX_set_padding(ctx, 0));
	memset(c, 0xff, sizeof(c));
	if (acpkm)
	    T(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_KEY_MESH, acpkm, NULL));
	for (i = 0; i < size; i += z) {
	    if (i + z > size)
		sz = size - i;
	    else
		sz = z;
	    T(EVP_CipherUpdate(ctx, c + i, &outlen, pt + i, sz));
	    OPENSSL_assert(outlen == sz);
	}
	outlen = i - z + sz;
	T(EVP_CipherFinal_ex(ctx, c + outlen, &tmplen));
	EVP_CIPHER_CTX_cleanup(ctx);

	test = outlen != size || memcmp(c, exp, size);
	printf("%c", test ? 'E' : '+');
	ret |= test;
    }
    printf("\n");
    TEST_ASSERT(ret);
    EVP_CIPHER_CTX_free(ctx);

    return ret;
}

static int test_mac(const char *name, const char *from,
    const EVP_MD *type, int acpkm, int acpkm_t,
    const unsigned char *pt, size_t pt_size,
    const unsigned char *mac, size_t mac_size)
{
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    unsigned char md_value[EVP_MAX_MD_SIZE];
    int test;
    unsigned int md_len;

    OPENSSL_assert(ctx);
    printf("%s test from %s\n", name, from);
    EVP_MD_CTX_init(ctx);
    T(EVP_DigestInit_ex(ctx, type, NULL));
    T(EVP_MD_CTX_ctrl(ctx, EVP_MD_CTRL_SET_KEY, sizeof(K), (void *)K));
    if (acpkm)
	T(EVP_MD_CTX_ctrl(ctx, EVP_CTRL_KEY_MESH, acpkm, acpkm_t ? &acpkm_t : NULL));
    T(EVP_DigestUpdate(ctx, pt, pt_size));
    if (EVP_MD_flags(EVP_MD_CTX_md(ctx)) & EVP_MD_FLAG_XOF) {
	T(EVP_DigestFinalXOF(ctx, md_value, mac_size));
	md_len = (unsigned int)mac_size;
    } else {
	T(EVP_MD_CTX_size(ctx) == mac_size);
	T(EVP_DigestFinal_ex(ctx, md_value, &md_len));
    }
    EVP_MD_CTX_free(ctx);
    printf("  MAC[%u] = ", md_len);
    hexdump(md_value, mac_size);

    TEST_ASSERT(md_len != mac_size ||
	memcmp(mac, md_value, mac_size));

    return test;
}

int main(int argc, char **argv)
{
    int ret = 0;
    const struct testcase *t;

    setenv("OPENSSL_ENGINES", ENGINE_DIR, 0);
    OPENSSL_add_all_algorithms_conf();
    ERR_load_crypto_strings();
    ENGINE *eng;
    T(eng = ENGINE_by_id("gost"));
    T(ENGINE_init(eng));
    T(ENGINE_set_default(eng, ENGINE_METHOD_ALL));

    for (t = testcases; t->name; t++) {
	int inplace;
	const char *standard = t->acpkm? "R 23565.1.017-2018" : "GOST R 34.13-2015";

	printf(cBLUE "# Tests for %s [%s]\n" cNORM, t->name, standard);
	for (inplace = 0; inplace <= 1; inplace++)
	    ret |= test_block(t->type(), t->name,
		t->plaintext, t->expected, t->size,
		t->iv, t->iv_size, t->acpkm, inplace);
	if (t->stream)
	    ret |= test_stream(t->type(), t->name,
		t->plaintext, t->expected, t->size,
		t->iv, t->iv_size, t->acpkm);
    }

    printf(cBLUE "# Tests for omac\n" cNORM);
    ret |= test_mac("OMAC", "GOST R 34.13-2015", grasshopper_omac(), 0, 0,
        P, sizeof(P), MAC_omac, sizeof(MAC_omac));
    ret |= test_mac("OMAC-ACPKM", "R 1323565.1.017-2018 A.4.1",
        grasshopper_omac_acpkm(), 32, 768 / 8,
        P_omac_acpkm1, sizeof(P_omac_acpkm1),
        MAC_omac_acpkm1, sizeof(MAC_omac_acpkm1));
    ret |= test_mac("OMAC-ACPKM", "R 1323565.1.017-2018 A.4.2",
        grasshopper_omac_acpkm(), 32, 768 / 8,
        P_omac_acpkm2, sizeof(P_omac_acpkm2),
        MAC_omac_acpkm2, sizeof(MAC_omac_acpkm2));

    ENGINE_finish(eng);
    ENGINE_free(eng);

    if (ret)
	printf(cDRED "= Some tests FAILED!\n" cNORM);
    else
	printf(cDGREEN "= All tests passed!\n" cNORM);
    return ret;
}
