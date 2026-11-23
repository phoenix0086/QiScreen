// Exercises the actual ARM64 protocol core on a HarmonyOS device, without a UI.
// This is a protocol test; it does not claim to test video rendering.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <plist/plist.h>
#include "raop.h"

static void audio(void *p, raop_ntp_t *n, audio_decode_struct *d) { (void)p;(void)n;(void)d; }
static void video(void *p, raop_ntp_t *n, video_decode_struct *d) { (void)p;(void)n;(void)d; }
static double volume(void *p) { (void)p;return 0.0; }
static void reset(void *p, reset_type_t t) { (void)p;(void)t; }
static void log_error(void *p, int level, const char *message) {
    (void)p;
    (void)level;
    fprintf(stderr, "%s\n", message);
}

int main(void) {
    ntp_global_init();
    for (int iteration = 0; iteration < 3; ++iteration) {
        char hw[6] = {2,0,0,0,0,1}; int error = 0;
        dnssd_t *dns = dnssd_init("QiScreen-Test",13,hw,6,0,&error);
        assert(dns && !error);
        raop_callbacks_t cb = {0};
        cb.audio_process = audio; cb.video_process = video; cb.audio_set_client_volume = volume; cb.video_reset = reset;
        raop_t *server = raop_init(&cb); assert(server);
        raop_set_log_callback(server, log_error, NULL);
        raop_set_log_level(server, LOGGER_ERR);
        assert(raop_init2(server,0,"02:00:00:00:00:01","") == 0);
        raop_set_dnssd(server,dns);
        raop_set_plist(server,"maxFPS",60);
        unsigned short port = 0;
        assert(raop_start_httpd(server,&port) >= 0 && port > 0);
        raop_set_port(server,port);
        int fd = socket(AF_INET,SOCK_STREAM,0); assert(fd >= 0);
        struct timeval timeout = {3,0};
        setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
        struct sockaddr_in addr = {0}; addr.sin_family=AF_INET;addr.sin_port=htons(port);addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        assert(connect(fd,(struct sockaddr *)&addr,sizeof(addr)) == 0);
        const char *request = "GET /info RTSP/1.0\r\nCSeq: 1\r\nContent-Length: 0\r\n\r\n";
        assert(send(fd,request,strlen(request),0) == (ssize_t)strlen(request));
        char data[16384] = {0}; size_t used=0, headerSize=0, bodySize=0;
        while (used < sizeof(data)-1) {
            ssize_t count = recv(fd,data+used,sizeof(data)-1-used,0);
            assert(count > 0); used += count; data[used] = 0;
            if (!headerSize) {
                char *end = strstr(data,"\r\n\r\n");
                if (end) {
                    headerSize = (size_t)(end-data)+4;
                    char *length = strstr(data,"Content-Length:"); assert(length && length < end);
                    bodySize = strtoul(length+15,NULL,10); assert(bodySize > 0 && bodySize < 8192);
                }
            }
            if (headerSize && used >= headerSize+bodySize) break;
        }
        assert(strncmp(data,"RTSP/1.0 200 OK",15) == 0);
        plist_t info = NULL;
        assert(plist_from_bin(data+headerSize,(uint32_t)bodySize,&info) == PLIST_ERR_SUCCESS);
        assert(info);
        plist_t display = plist_array_get_item(plist_dict_get_item(info,"displays"),0);
        uint64_t width=0,height=0,fps=0;
        plist_get_uint_val(plist_dict_get_item(display,"width"),&width);
        plist_get_uint_val(plist_dict_get_item(display,"height"),&height);
        plist_get_uint_val(plist_dict_get_item(display,"maxFPS"),&fps);
        assert(width==1920 && height==1080 && fps==60);
        plist_free(info); close(fd); raop_destroy(server); dnssd_destroy(dns);
        printf("PASS cycle %d: RTSP /info 1920x1080@60; crypto init and teardown\n",iteration+1); fflush(stdout);
    }
    return 0;
}
