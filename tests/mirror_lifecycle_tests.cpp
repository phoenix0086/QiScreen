extern "C" {
#include "raop_rtp_mirror.h"
}
#include <atomic>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
using namespace std::chrono_literals;
static void Check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static int Fds(){int n=0;for(int i=0;i<2048;++i)if(fcntl(i,F_GETFD)!=-1)++n;return n;}
struct Mirror {
 logger_t *log=logger_init();raop_rtp_mirror_t *server=nullptr;std::atomic<bool> running{false};
 Mirror(){logger_set_level(log,LOGGER_EMERG);raop_callbacks_t cb{};cb.cls=this;cb.mirror_video_running=[](void *p,bool v){static_cast<Mirror*>(p)->running=v;};
 unsigned char key[16]{};const char *remote="127.0.0.1";server=raop_rtp_mirror_init(log,&cb,nullptr,remote,4,key);Check(server,"mirror init");}
 ~Mirror(){raop_rtp_mirror_destroy(server);logger_destroy(log);}
 unsigned short Start(){unsigned short port=0;raop_rtp_mirror_start(server,&port,0);return port;}
 void Drop(unsigned short port,bool reset){int fd=socket(AF_INET,SOCK_STREAM,0);Check(fd>=0,"socket");sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(port);Check(connect(fd,reinterpret_cast<sockaddr*>(&a),sizeof(a))==0,"connect");
 std::this_thread::sleep_for(20ms);if(reset){linger l{1,0};setsockopt(fd,SOL_SOCKET,SO_LINGER,&l,sizeof(l));}close(fd);std::this_thread::sleep_for(30ms);}
};
int main(int argc,char **argv){try{
 Check(argc==2,"test required");
 if(!strcmp(argv[1],"eof")){
  {Mirror warm;Check(warm.Start(),"warm start");raop_rtp_mirror_stop(warm.server);}
  int before=Fds();{Mirror m;auto port=m.Start();Check(port,"start");for(int i=0;i<12;++i)m.Drop(port,false);raop_rtp_mirror_stop(m.server);}int after=Fds();printf("graceful_disconnect_fd_delta=%d\n",after-before);Check(after<=before,"graceful reconnect must close every accepted socket");
 }else if(!strcmp(argv[1],"oversize")){
  for(uint32_t size:{uint32_t(4*1024*1024+1),uint32_t(0xffffffff)}){
   Mirror m;auto port=m.Start();Check(port,"start");int fd=socket(AF_INET,SOCK_STREAM,0);sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(port);
   Check(connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"connect malformed sender");
   unsigned char header[128]{};for(int i=0;i<4;++i)header[i]=(size>>(8*i))&255;
   Check(send(fd,header,sizeof(header),0)==sizeof(header),"send invalid length");
   for(int i=0;i<60&&m.running;++i)std::this_thread::sleep_for(10ms);
   close(fd);Check(!m.running,"invalid length rejected before allocation and timestamp parsing");raop_rtp_mirror_stop(m.server);
  }
 }else{
  Mirror m;auto port=m.Start();Check(port,"start");m.Drop(port,true);for(int i=0;i<60&&m.running;++i)std::this_thread::sleep_for(10ms);Check(!m.running,"reset exits mirror worker");raop_rtp_mirror_stop(m.server);auto restarted=m.Start();Check(restarted>0&&m.running,"stop reaps naturally exited worker so it can restart");raop_rtp_mirror_stop(m.server);
 }
 puts("PASS mirror lifecycle");return 0;
}catch(const std::exception&e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
