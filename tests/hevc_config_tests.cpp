#include "mirror_hevc_config.h"
#include <array>
#include <cstdio>
#include <stdexcept>
static void Check(bool ok,const char *message) { if(!ok)throw std::runtime_error(message); }
int main() {
    try {
        std::array<unsigned char,138> packet{};
        memcpy(packet.data()+4,"hvc1",4);
        for(size_t i=0;i<3;++i) { size_t p=117+i*7;
            packet[p]=0xa0+i;packet[p+2]=1;packet[p+4]=2;packet[p+5]=(32+i)<<1;packet[p+6]=1; }
        std::array<unsigned char,18> output{};size_t length=99;
        Check(mirror_hevc_config(packet.data(),packet.size(),output.data(),output.size(),&length)&&length==18,"valid VPS SPS PPS");
        Check(output[3]==1&&output[4]==64&&output[9]==1&&output[10]==66&&output[15]==1&&output[16]==68,"Annex B order");
        for(size_t n=0;n<packet.size();++n)
            Check(!mirror_hevc_config(packet.data(),n,output.data(),output.size(),&length)&&length==0,"truncations rejected");
        Check(!mirror_hevc_config(packet.data(),packet.size(),output.data(),17,&length),"destination bound");
        packet[120]=255;packet[121]=255;
        Check(!mirror_hevc_config(packet.data(),packet.size(),output.data(),output.size(),&length),"oversized length rejected");
        packet[120]=0;packet[121]=2;packet[122]=66;
        Check(!mirror_hevc_config(packet.data(),packet.size(),output.data(),output.size(),&length),"incorrect parameter type rejected");
        puts("PASS: HEVC parameter sets and malformed lengths");return 0;
    } catch(const std::exception &e) {fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
