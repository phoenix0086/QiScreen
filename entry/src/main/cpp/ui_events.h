#pragma once
#include <atomic>
namespace qiscreen {
class UiEvents {
public:
 void Add(bool viewOnly){flags_.fetch_or(viewOnly?2u:1u);}
 bool TakeViewOnly(){return flags_.exchange(0)==2u;}
private:std::atomic<unsigned> flags_{0};
};
}
