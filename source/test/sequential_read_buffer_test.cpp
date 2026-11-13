#include "../core/StarSequentialReadBuffer.hpp"
#include <vector>
#include <cassert>
#include <cstdio>
int main(){
  for(size_t size:{size_t(0),size_t(1),size_t(65535),size_t(65536),size_t(65537),size_t(200001)}){
    std::vector<char> src(size),out(size);for(size_t i=0;i<size;++i)src[i]=char(i*37);
    size_t cursor=0,calls=0;
    auto reader=[&](char* p,size_t n){assert(n&&n<=65536&&n<=size-cursor);std::memcpy(p,src.data()+cursor,n);cursor+=n;++calls;};
    Star::SequentialReadBuffer<decltype(reader)> buffer(reader,size);
    buffer.read(nullptr,0);
    for(size_t pos=0;pos<size;){auto n=std::min(size-pos,(pos%23)?size_t(7):size_t(70001));buffer.read(out.data()+pos,n);pos+=n;}
    assert(src==out&&cursor==size&&calls==(size+65535)/65536);
    bool rejected=false;char x=0;try{buffer.read(&x,1);}catch(std::runtime_error const&){rejected=true;}assert(rejected);
  }
  size_t calls=0;auto reader=[&](char*,size_t){++calls;throw std::runtime_error("simulated I/O failure");};
  Star::SequentialReadBuffer<decltype(reader)> buffer(reader,100);char data[101];
  try{buffer.read(data,101);assert(false);}catch(std::runtime_error const&){}assert(!calls);
  try{buffer.read(data,1);assert(false);}catch(std::runtime_error const&){}assert(calls==1);
  try{buffer.read(data,1);assert(false);}catch(std::runtime_error const&){}assert(calls==2);
  std::puts("sequential-read-buffer: verified boundaries, byte accuracy, bounded calls and I/O failure");
}
