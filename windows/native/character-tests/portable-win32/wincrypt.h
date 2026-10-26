/* Abort-only unrelated RUI crypto boundary; character hashing remains real. */
#ifndef CHARACTER_CONTRACT_WINCRYPT_H
#define CHARACTER_CONTRACT_WINCRYPT_H
#include "windows.h"
typedef uintptr_t HCRYPTPROV;typedef uintptr_t HCRYPTHASH;
#define PROV_RSA_AES 24
#define CRYPT_VERIFYCONTEXT 0xf0000000
#define CALG_SHA_256 0x800c
#define HP_HASHVAL 2
UNAVAILABLE(BOOL,CryptAcquireContextW,(HCRYPTPROV *a,const wchar_t *b,const wchar_t *c,DWORD d,DWORD e))
UNAVAILABLE(BOOL,CryptCreateHash,(HCRYPTPROV a,DWORD b,uintptr_t c,DWORD d,HCRYPTHASH *e))
UNAVAILABLE(BOOL,CryptHashData,(HCRYPTHASH a,const unsigned char *b,DWORD c,DWORD d))
UNAVAILABLE(BOOL,CryptGetHashParam,(HCRYPTHASH a,DWORD b,unsigned char *c,DWORD *d,DWORD e))
UNAVAILABLE(BOOL,CryptDestroyHash,(HCRYPTHASH a))
UNAVAILABLE(BOOL,CryptReleaseContext,(HCRYPTPROV a,DWORD b))
#endif
