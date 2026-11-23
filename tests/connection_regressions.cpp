#include "connection_policy.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <chrono>
#include <thread>
#include <fcntl.h>

static void Require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
static void Audio(void *, raop_ntp_t *, audio_decode_struct *) {}
static void Video(void *, raop_ntp_t *, video_decode_struct *) {}
static double Volume(void *) { return 0; }
static void Reset(void *, reset_type_t) {}
struct Server {
    raop_t *raop;
    dnssd_t *dns;
    Server() {
        int error = 0; char hw[6]{2,0,0,0,0,9};
        dns = dnssd_init("QiScreen-Test",13,hw,6,0,&error);
        Require(dns && !error,"dns init");
        raop_callbacks_t cb{}; cb.audio_process=Audio;cb.video_process=Video;
        cb.audio_set_client_volume=Volume;cb.video_reset=Reset;
        raop=raop_init(&cb);Require(raop,"raop init");raop_set_log_level(raop,LOGGER_EMERG);
        Require(raop_init2(raop,0,"02:00:00:00:00:09","")==0,"crypto init");
        raop_set_dnssd(raop,dns);
    }
    ~Server() { raop_destroy(raop);dnssd_destroy(dns); }
};
static void Query(unsigned short port) {
    int fd=socket(AF_INET,SOCK_STREAM,0);Require(fd>=0,"client socket");
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    timeval timeout{2,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    Require(connect(fd,reinterpret_cast<sockaddr *>(&address),sizeof(address))==0,"cached endpoint is reachable");
    const char *q="GET /info RTSP/1.0\r\nCSeq: 1\r\nContent-Length: 0\r\n\r\n";
    send(fd,q,strlen(q),0);char result[128]{};int count=recv(fd,result,sizeof(result)-1,0);close(fd);
    Require(count>=15 && !strncmp(result,"RTSP/1.0 200 OK",15),"real RTSP response");
}
static void LatestIntentWins() {
    qiscreen::LatestCommand gate; bool listening=false;
    auto oldStop=gate.Issue();auto newStart=gate.Issue();
    if(gate.IsCurrent(newStart))listening=true;
    if(gate.IsCurrent(oldStop))listening=false;
    Require(listening,"a delayed old stop must not override the new start");
    auto oldStart=gate.Issue();auto newStop=gate.Issue();
    if(gate.IsCurrent(newStop))listening=false;
    if(gate.IsCurrent(oldStart))listening=true;
    Require(!listening,"a delayed old start must not reopen a stopped receiver");
    auto queuedStart=gate.Issue();gate.Issue();
    Require(!gate.IsCurrent(queuedStart),"surface teardown invalidates queued work");
}
static void ReuseEndpoint() {
    unsigned short saved=0;
    for(int i=0;i<8;++i) {
        Server server;unsigned short actual=0;
        Require(qiscreen::StartWithPreferredPort(server.raop,saved,actual),"start listener");
        if(saved)Require(actual==saved,"restart must keep the endpoint cached by the Mac");
        saved=actual;raop_set_port(server.raop,actual);Query(actual);
    }
}
static void OccupiedPortFallback() {
    int blocker=socket(AF_INET,SOCK_STREAM,0);Require(blocker>=0,"blocker socket");
    sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_ANY);
    Require(bind(blocker,reinterpret_cast<sockaddr *>(&addr),sizeof(addr))==0,"bind blocker");
    Require(listen(blocker,1)==0,"listen blocker");socklen_t len=sizeof(addr);
    getsockname(blocker,reinterpret_cast<sockaddr *>(&addr),&len);unsigned short busy=ntohs(addr.sin_port);
    Server server;unsigned short actual=0;
    bool ok=qiscreen::StartWithPreferredPort(server.raop,busy,actual);close(blocker);
    Require(ok && actual && actual!=busy,"occupied saved port must fall back safely");
    raop_set_port(server.raop,actual);Query(actual);
}
static void IdleStopLatency() {
    Server server; unsigned short port=0;
    Require(qiscreen::StartWithPreferredPort(server.raop,0,port),"start idle listener");
    std::this_thread::sleep_for(std::chrono::milliseconds(75));
    auto begin=std::chrono::steady_clock::now();
    raop_stop_httpd(server.raop);
    auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    printf("idle_listener_stop_ms=%.3f\n",ms);
    Require(ms<500,"stopping an idle receiver must not wait for the one-second select timeout");
    unsigned short reused=0;
    Require(qiscreen::StartWithPreferredPort(server.raop,port,reused) && reused==port,"restart same protocol instance");
    raop_set_port(server.raop,reused);Query(reused);
}
static int OpenDescriptors() { int n=0;for(int i=0;i<1024;++i)if(fcntl(i,F_GETFD)!=-1)++n;return n; }
static void NoWakeDescriptorLeak() {
    { Server warm; unsigned short port=0;Require(qiscreen::StartWithPreferredPort(warm.raop,0,port),"warmup"); }
    int before=OpenDescriptors();
    for(int i=0;i<20;++i) {
        Server server; unsigned short port=0;
        Require(qiscreen::StartWithPreferredPort(server.raop,0,port),"repeat listener");
    }
    int after=OpenDescriptors();printf("open_fd_delta=%d\n",after-before);
    Require(after<=before,"wakeup descriptors must be released after every lifecycle");
}
int main(int argc,char **argv) {
    ntp_global_init();
    try {
        Require(argc==2,"test case required");
        if(!strcmp(argv[1],"lifecycle"))LatestIntentWins();
        else if(!strcmp(argv[1],"reuse"))ReuseEndpoint();
        else if(!strcmp(argv[1],"fallback"))OccupiedPortFallback();
        else if(!strcmp(argv[1],"idle-stop"))IdleStopLatency();
        else if(!strcmp(argv[1],"resources"))NoWakeDescriptorLeak();
        else throw std::runtime_error("unknown test");
        printf("PASS %s\n",argv[1]);return 0;
    } catch(const std::exception &e) { fprintf(stderr,"FAIL: %s\n",e.what());return 1; }
}
