// Freestanding WASM shims only. No WASI, network, filesystem, allocator or JS imports.
#include <stddef.h>
extern "C" {
void* memset(void* destination,int value,size_t n) {
    auto* d=static_cast<unsigned char*>(destination);
    for(size_t i=0;i<n;++i) d[i]=static_cast<unsigned char>(value);
    return destination;
}
void* memcpy(void* destination,const void* source,size_t n) {
    auto* d=static_cast<unsigned char*>(destination); auto* s=static_cast<const unsigned char*>(source);
    for(size_t i=0;i<n;++i) d[i]=s[i];
    return destination;
}
void* memmove(void* destination,const void* source,size_t n) {
    auto* d=static_cast<unsigned char*>(destination); auto* s=static_cast<const unsigned char*>(source);
    if(d<s) for(size_t i=0;i<n;++i) d[i]=s[i];
    else for(size_t i=n;i>0;--i) d[i-1]=s[i-1];
    return destination;
}
}
