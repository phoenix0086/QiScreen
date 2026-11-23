#include "pcm_ring.h"
#include "ui_events.h"
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <thread>
#include <deque>
#include <chrono>
static void Check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
int main(){try{
 qiscreen::PcmRing ring;std::vector<uint8_t> source(ring.Capacity),out(ring.Capacity);
 for(size_t i=0;i<source.size();++i)source[i]=i%251;
 Check(ring.Write(source.data(),source.size()),"fill PCM");Check(ring.Read(out.data(),100)==100,"partial read");
 Check(ring.Write(source.data(),100),"wrap write");Check(ring.Read(out.data(),out.size())==out.size(),"wrap read");
 for(size_t i=0;i<source.size()-100;++i)Check(out[i]==source[i+100],"wrapped order");
 for(size_t i=0;i<100;++i)Check(out[out.size()-100+i]==source[i],"wrapped tail");
 ring.Clear();ring.Write(source.data(),source.size());ring.Write(source.data(),100);
 Check(ring.Size()==ring.Capacity,"overrun bounded");ring.Read(out.data(),4);Check(out[0]==source[100],"only oldest stereo samples evicted");
 Check(!ring.Write(source.data(),3),"unaligned samples rejected");Check(!ring.Write(source.data(),source.size()+4),"oversized callback rejected");
 ring.Clear();Check(ring.Read(out.data(),100)==0,"underflow reads zero bytes");
 qiscreen::UiEvents events;events.Add(true);events.Add(true);Check(events.TakeViewOnly(),"gesture notifications coalesce");
 events.Add(false);events.Add(true);Check(!events.TakeViewOnly(),"full lifecycle notification dominates a gesture");
 std::thread producer([&]{for(int i=0;i<10000;++i)events.Add(true);events.Add(false);});producer.join();Check(!events.TakeViewOnly(),"critical state not lost under gesture flood");
 constexpr int loops=12000;constexpr size_t block=4096;std::deque<uint8_t> old;volatile uint64_t sum=0;
 auto begin=std::chrono::steady_clock::now();
 for(int i=0;i<loops;++i){old.insert(old.end(),source.begin(),source.begin()+block);for(size_t j=0;j<block;++j){out[j]=old.front();old.pop_front();}sum+=out[100];}
 auto oldUs=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-begin).count();
 begin=std::chrono::steady_clock::now();
 for(int i=0;i<loops;++i){ring.Write(source.data(),block);ring.Read(out.data(),block);sum+=out[100];}
 auto newUs=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-begin).count();
 printf("host_pcm_copy_deque_us=%lld ring_us=%lld checksum=%llu\n",(long long)oldUs,(long long)newUs,(unsigned long long)sum);
 puts("PASS: PCM wrap/overrun/alignment and lifecycle notification priority");return 0;
}catch(const std::exception&e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
