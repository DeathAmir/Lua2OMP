#include <lua.h>
#include <lauxlib.h>
#include <vector>
#include <fstream>
#include <iostream>
#include "crypto.hpp"
static int writer(lua_State*,const void* data,size_t size,void* user) {
 auto* out=static_cast<std::vector<unsigned char>*>(user);
 const auto* bytes=static_cast<const unsigned char*>(data);
 out->insert(out->end(),bytes,bytes+size); return 0;
}
int main(int argc,char** argv) {
 if(argc!=3){std::cerr<<"Lua2OMP compiler/encryptor\nUsage: luaenc input.lua output.lal\n";return 2;}
 lua_State* state=luaL_newstate();
 if(!state){std::cerr<<"error: cannot create Lua state\n";return 1;}
 if(luaL_loadfile(state,argv[1])!=0){const char* e=lua_tostring(state,-1);std::cerr<<"compile error: "<<(e?e:"unknown Lua error")<<"\n";lua_close(state);return 1;}
 std::vector<unsigned char> bytecode;
 if(lua_dump(state,writer,&bytecode)!=0){std::cerr<<"error: bytecode generation failed\n";lua_close(state);return 1;}
 lua_close(state);
 std::vector<unsigned char> encrypted;
 if(!lua2omp_crypto::encrypt(bytecode,encrypted)){std::cerr<<"error: AES-256-GCM encryption failed\n";return 1;}
 std::ofstream out(argv[2],std::ios::binary|std::ios::trunc);
 if(!out){std::cerr<<"error: cannot create output file\n";return 1;}
 out.write(reinterpret_cast<const char*>(encrypted.data()),static_cast<std::streamsize>(encrypted.size()));
 if(!out){std::cerr<<"error: output write failed\n";return 1;}
 std::cout<<"Compiled and encrypted: "<<argv[2]<<"\nHeader: L@L v1 | AES-256-GCM | Lua 5.1 bytecode\n";return 0;
}