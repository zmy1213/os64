#include <stddef.h>
extern "C" void* memset(void* destination,int value,size_t size) {
  auto* bytes=static_cast<unsigned char*>(destination);
  for(size_t i=0;i<size;++i) bytes[i]=static_cast<unsigned char>(value);
  return destination;
}
extern "C" void* memcpy(void* destination,const void* source,size_t size) {
  auto* output=static_cast<unsigned char*>(destination);
  const auto* input=static_cast<const unsigned char*>(source);
  for(size_t i=0;i<size;++i) output[i]=input[i];
  return destination;
}
