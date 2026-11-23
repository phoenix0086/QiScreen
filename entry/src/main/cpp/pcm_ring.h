#pragma once
#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace qiscreen {
// Stereo S16LE. Caller owns synchronization; callbacks do at most two bulk copies.
class PcmRing {
public:
 static constexpr size_t Capacity=44100; // 250 ms, aligned to a four-byte stereo sample.
 void Clear(){read_=0;size_=0;}
 size_t Size()const{return size_;}
 bool Write(const uint8_t *data,size_t count){
  if(!data||count%4||count>Capacity)return false;
  if(size_+count>Capacity){size_t discard=size_+count-Capacity;read_=(read_+discard)%Capacity;size_-=discard;}
  size_t write=(read_+size_)%Capacity,first=std::min(count,Capacity-write);
  std::memcpy(bytes_.data()+write,data,first);std::memcpy(bytes_.data(),data+first,count-first);size_+=count;return true;
 }
 size_t Read(uint8_t *out,size_t count){
  if(!out)return 0;count=std::min(count-count%4,size_);size_t first=std::min(count,Capacity-read_);
  std::memcpy(out,bytes_.data()+read_,first);std::memcpy(out+first,bytes_.data(),count-first);read_=(read_+count)%Capacity;size_-=count;return count;
 }
private:
 std::array<uint8_t,Capacity> bytes_{};size_t read_=0,size_=0;
};
}
