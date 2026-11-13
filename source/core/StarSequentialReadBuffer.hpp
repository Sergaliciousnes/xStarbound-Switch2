#pragma once
#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <cstdint>
#include <utility>
namespace Star {
// Bounded, forward-only read-ahead for serialized archive indexes. The reader
// must fill exactly the requested bytes or throw. Memory use is constant.
template <typename Reader> class SequentialReadBuffer {
public:
  SequentialReadBuffer(Reader reader,uint64_t bytes)
    : m_reader(std::move(reader)),m_remaining(bytes),m_buffer(new char[Capacity]) {}
  void read(char* data,size_t bytes) {
    if(bytes>m_remaining)throw std::runtime_error("Archive index read exceeds file bounds");
    while(bytes){
      if(m_pos==m_end){
        auto count=size_t(std::min<uint64_t>(Capacity,m_remaining));
        m_reader(m_buffer.get(),count);
        m_end=count;m_pos=0;
      }
      auto count=std::min(bytes,m_end-m_pos);
      std::memcpy(data,m_buffer.get()+m_pos,count);
      m_pos+=count;m_remaining-=count;data+=count;bytes-=count;
    }
  }
private:
  static constexpr size_t Capacity=64*1024;
  Reader m_reader;
  uint64_t m_remaining;
  std::unique_ptr<char[]> m_buffer;
  size_t m_pos=0,m_end=0;
};
}
