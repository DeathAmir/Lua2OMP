#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <iterator>
#include <limits>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#endif
namespace lua2omp_crypto {
static constexpr unsigned char magic[4]={'L','@','L',1};
static constexpr char masterKey[]=
"Lua2OMP-Private-Resource-Key-v1-DeathAmir-2026-"
"7d9c3f1a8b6e4d2f0a5c7e9b1d3f6a8c9e2b4d6f8a0c1e3b5d7f9a2c4e6b8d0f"
"1a3c5e7f9b0d2a4c6e8f1b3d5a7c9e0f2b4d6f8a0c1e3b5d7f9a2c4e6b8d0f"
"1a3c5e7f9b0d2a4c6e8f1b3d5a7c9e0f2b4d6f8a0c1e3b5d7f9a2c4e6b8d0f"
"1a3c5e7f9b0d2a4c6e8f1b3d5a7c9e0f2b4d6f8a0c1e3b5d7f9a2c4e6b8d0f"
"1a3c5e7f9b0d2a4c6e8f1b3d5a7c9e0f2b4d6f8a0c1e3b5d7f9a2c4e6b8d0f"
"1a3c5e7f9b0d2a4c6e8f1b3d5a7c9e0f2b4d6f8a0c1e3b5d7f9a2c4e6b8d0f"
"1a3c5e7f9b0d2a4c6e8f1b3d5a7c9e0f2b4d6f8a0c1e3b5d7f9a2c4e6b8d0f"
"1a3c5e7f9b0d2a4c6e8f1b3d5a7c9e0f2b4d6f8a0c1e3b5d7f9a2c4e6b8d0f";
#ifdef _WIN32
inline bool deriveKey(std::vector<unsigned char>& key) {
 BCRYPT_ALG_HANDLE alg=nullptr; BCRYPT_HASH_HANDLE hash=nullptr; DWORD objSize=0,got=0; std::vector<unsigned char> object; key.resize(32);
 if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;
 bool ok=BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objSize),sizeof(objSize),&got,0)>=0;
 if(ok)object.resize(objSize);
 if(ok)ok=BCryptCreateHash(alg,&hash,object.data(),objSize,nullptr,0,0)>=0;
 if(ok)ok=BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(masterKey)),static_cast<ULONG>(sizeof(masterKey)-1),0)>=0;
 if(ok)ok=BCryptFinishHash(hash,key.data(),static_cast<ULONG>(key.size()),0)>=0;
 if(hash)BCryptDestroyHash(hash);
 BCryptCloseAlgorithmProvider(alg,0); return ok;
}
inline bool transform(bool enc,const std::vector<unsigned char>& in,std::vector<unsigned char>& out,unsigned char* nonce,unsigned char* tag) {
 BCRYPT_ALG_HANDLE alg=nullptr; BCRYPT_KEY_HANDLE kh=nullptr; DWORD objSize=0,got=0; std::vector<unsigned char> object,key;
 if(!deriveKey(key)||BCryptOpenAlgorithmProvider(&alg,BCRYPT_AES_ALGORITHM,nullptr,0)<0)return false;
 bool ok=BCryptSetProperty(alg,BCRYPT_CHAINING_MODE,reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),sizeof(BCRYPT_CHAIN_MODE_GCM),0)>=0;
 if(ok)ok=BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objSize),sizeof(objSize),&got,0)>=0;
 if(ok)object.resize(objSize);
 if(ok)ok=BCryptGenerateSymmetricKey(alg,&kh,object.data(),objSize,key.data(),static_cast<ULONG>(key.size()),0)>=0;
 if(enc&&ok)ok=BCryptGenRandom(nullptr,nonce,12,BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0;
 out.resize(in.size()); BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth; BCRYPT_INIT_AUTH_MODE_INFO(auth); auth.pbNonce=nonce; auth.cbNonce=12; auth.pbTag=tag; auth.cbTag=16;
 ULONG written=0;
 if(ok) {
  const auto* src=in.empty()?reinterpret_cast<const unsigned char*>(""):in.data();
  auto* dst=out.empty()?reinterpret_cast<unsigned char*>(""):out.data();
  const NTSTATUS status=enc
   ? BCryptEncrypt(kh,const_cast<PUCHAR>(src),static_cast<ULONG>(in.size()),&auth,nullptr,0,dst,static_cast<ULONG>(out.size()),&written,0)
   : BCryptDecrypt(kh,const_cast<PUCHAR>(src),static_cast<ULONG>(in.size()),&auth,nullptr,0,dst,static_cast<ULONG>(out.size()),&written,0);
  ok=status>=0;
 }
 if(kh)BCryptDestroyKey(kh);
 BCryptCloseAlgorithmProvider(alg,0);
 if(!ok){out.clear();return false;} out.resize(written); return true;
}
#endif
inline bool encrypt(const std::vector<unsigned char>& plain,std::vector<unsigned char>& file) {
#ifdef _WIN32
 if(plain.size()>std::numeric_limits<ULONG>::max())return false;
 unsigned char nonce[12]{},tag[16]{}; std::vector<unsigned char> cipher;
 if(!transform(true,plain,cipher,nonce,tag))return false;
 file.assign(magic,magic+4); file.insert(file.end(),nonce,nonce+12); file.insert(file.end(),tag,tag+16); file.insert(file.end(),cipher.begin(),cipher.end()); return true;
#else
 (void)plain;(void)file;return false;
#endif
}
inline bool decrypt(const std::vector<unsigned char>& file,std::vector<unsigned char>& plain) {
#ifdef _WIN32
 if(file.size()<32||!std::equal(magic,magic+4,file.begin()))return false;
 std::vector<unsigned char> cipher(file.begin()+32,file.end()); unsigned char nonce[12]{},tag[16]{};
 std::copy(file.begin()+4,file.begin()+16,nonce); std::copy(file.begin()+16,file.begin()+32,tag);
 if(cipher.size()>std::numeric_limits<ULONG>::max())return false;
 return transform(false,cipher,plain,nonce,tag);
#else
 (void)file;(void)plain;return false;
#endif
}
inline bool loadEncrypted(const std::string& path,std::vector<unsigned char>& plain,std::string& error) {
 std::ifstream in(path,std::ios::binary); if(!in){error="cannot open encrypted script";return false;}
 std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
 if(data.size()<32||!std::equal(magic,magic+4,data.begin())){error="invalid L@L header";return false;}
 if(!decrypt(data,plain)){error="AES-256-GCM authentication/decryption failed";return false;} return true;
}
}