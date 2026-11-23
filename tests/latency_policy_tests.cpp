#include "latency_policy.h"
#include <cstdio>
#include <stdexcept>

static void Check(bool value,const char *message) { if(!value)throw std::runtime_error(message); }
int main() {
    using qiscreen::SkipObsoleteOutput;
    try {
        qiscreen::LatencyWindow window;
        Check(window.Snapshot().samples==0,"empty measurements remain unknown");
        for(int i=1;i<=100;++i)window.Add(i*1000);
        auto s=window.Snapshot();Check(s.p50Ms==50 && s.p95Ms==95 && s.maxMs==100,"percentile calculation");
        for(int i=0;i<120;++i)window.Add(1000);
        s=window.Snapshot();Check(s.samples==120 && s.maxMs==1,"old measurements expire with bounded storage");
        window.Add(-100);Check(window.Snapshot().samples==120,"invalid timestamps ignored");
        window.Reset();Check(window.Snapshot().p95Ms==-1,"mode changes clear prior window");
        Check(!SkipObsoleteOutput(false,100000,300000,900000,1000000,990000),"full-frame mode preserves output");
        Check(!SkipObsoleteOutput(true,100000,300000,900000,1000000,0),"never discard first frame");
        Check(!SkipObsoleteOutput(true,300000,300000,500000,1000000,990000),"always present latest frame even after a stall");
        Check(!SkipObsoleteOutput(true,100000,130000,980000,1000000,990000),"normal small pipeline lead is retained");
        Check(SkipObsoleteOutput(true,100000,300000,900000,1000000,990000),"obsolete decoded frames must not fill the display queue");
        Check(SkipObsoleteOutput(true,100000,110000,900000,1000000,990000),"old local backlog can be skipped when a newer frame exists");
        Check(!SkipObsoleteOutput(true,100000,300000,900000,1000000,890000),"catch-up must not starve presentation");
        Check(!SkipObsoleteOutput(true,0,300000,900000,1000000,990000),"unknown timing preserves frames");
        Check(!SkipObsoleteOutput(true,100000,9000000,900000,1000000,990000),"clock discontinuity is not treated as backlog");
        puts("PASS: bounded metrics and safe post-decode catch-up");return 0;
    } catch(const std::exception &e) { fprintf(stderr,"FAIL: %s\n",e.what());return 1; }
}
