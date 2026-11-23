#include "view_gesture.h"
#include <cstdio>
#include <stdexcept>
#include <limits>
static void Check(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
static bool Near(double a,double b) { return std::abs(a-b)<0.01; }
int main() {
    try {
        qiscreen::ViewGesture v; v.Viewport(1000,800,20,40); v.Content(1000,800);
        qiscreen::ViewPoint p[]={{1,620,440},{2,820,440}};
        v.Touch(p,2,p[0],1000); p[0].x=520;p[1].x=920;v.Touch(p,2,p[0],2000);
        Check(Near(v.scale,2)&&Near(v.panX,-200),"pinch preserves off-center anchor");
        v.Touch(p,1,p[0],3000); Check(Near(v.panX,-200),"pinch to pan has no jump");
        p[0].x+=100;v.Touch(p,1,p[0],4000);Check(Near(v.panX,-100),"one finger pans after pinch");
        Check(!v.Touch(nullptr,0,p[0],5000),"pinch release never toggles toolbar");
        v.SetScale(9);Check(v.scale==4,"maximum zoom");
        v.SetScale(std::numeric_limits<double>::quiet_NaN());Check(v.scale==4,"invalid zoom ignored");
        v.SetScale(0);Check(v.scale==1&&v.panX==0&&v.panY==0,"minimum scale recenters");
        v.Content(1600,900);v.SetScale(2);p[0]={1,500,400};v.Touch(p,1,p[0],10000);
        p[0].x=3000;p[0].y=3000;v.Touch(p,1,p[0],20000);
        Check(Near(v.panX,500)&&Near(v.panY,162.5),"pan clamps to fitted video, not black bars");
        Check(!v.Touch(nullptr,0,p[0],30000),"drag is not tap");
        p[0]={1,500,400};v.Touch(p,1,p[0],1000000);
        Check(!v.Touch(nullptr,0,p[0],1100000),"single tap must not toggle controls");
        v.Touch(p,1,p[0],1200000);
        Check(v.Touch(nullptr,0,p[0],1300000)&&v.scale==2,"double tap toggles controls without changing zoom");
        v.Touch(p,1,p[0],1400000);
        Check(!v.Touch(nullptr,0,p[0],1500000),"third tap starts a new pair");
        v.Touch(p,1,p[0],1600000);
        Check(v.Touch(nullptr,0,p[0],1700000),"four taps produce exactly two toggles");
        v.Cancel();v.Touch(p,1,p[0],2000000);Check(!v.Touch(nullptr,0,p[0],2100000),"first tap is quiet");
        v.Touch(p,1,p[0],2500000);Check(!v.Touch(nullptr,0,p[0],2600000),"slow second tap must not toggle");
        p[0].x+=150;v.Touch(p,1,p[0],2700000);
        Check(!v.Touch(nullptr,0,p[0],2800000),"distant tap must not complete a double tap");
        v.Touch(p,1,p[0],2900000);Check(v.Touch(nullptr,0,p[0],3000000),"nearby rapid pair toggles");
        v.SetScale(3);v.Viewport(800,1000,0,0);Check(v.scale==1,"rotation resets view");
        v.Touch(p,1,p[0],2000000);v.Touch(nullptr,0,p[0],2100000,true);
        Check(!v.Touch(nullptr,0,p[0],2200000),"cancel cannot become tap");
        v.Viewport(2800,1840,0,0);v.Content(2560,1440);v.Fill();
        Check(Near(v.scale,1840.0/1575)&&v.panX==0&&v.panY==0,"fill removes outer bars with centered aspect crop");
        puts("PASS: pinch, drag bounds, double-tap-only controls, timing/distance gates, rotation, cancellation");return 0;
    } catch(const std::exception &e) { fprintf(stderr,"FAIL: %s\n",e.what());return 1; }
}
