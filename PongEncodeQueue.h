/* SPDX-License-Identifier: MIT
 * Adapted from AdrenoMetal's reusable closure batches and interactive encode
 * worker. Calls retain their actual inputs until execution; no fake encoder,
 * command completion or cross-queue GPU reordering is introduced.
 */
#include <condition_variable>
#include <deque>
#include <memory>
#include <type_traits>
#include <utility>
namespace pongencode {
class Batch {
    struct Chunk {std::unique_ptr<uint8_t[]> bytes;size_t capacity=0,used=0;};
    struct Header {void (*run)(void *);size_t size;};
    static constexpr size_t align=16,chunkSize=16384;
    static constexpr size_t rounded(size_t n){return (n+align-1)&~(align-1);}
    std::vector<Chunk> chunks;size_t at=0;
public:
    uint64_t serial=0,queuedUs=0;size_t count=0;
    template<class F> void add(F &&work) {
        using Closure=std::decay_t<F>;static_assert(alignof(Closure)<=align);
        const size_t need=rounded(sizeof(Header))+rounded(sizeof(Closure));
        while(at<chunks.size()&&chunks[at].used+need>chunks[at].capacity)++at;
        if(at==chunks.size()) {
            Chunk c;c.capacity=std::max(chunkSize,need);c.bytes.reset(new uint8_t[c.capacity]);chunks.push_back(std::move(c));
        }
        auto &chunk=chunks[at];auto *slot=chunk.bytes.get()+chunk.used;chunk.used+=need;++count;
        new(slot) Header{[](void *p){auto &call=*static_cast<Closure *>(p);call();call.~Closure();},need};
        new(slot+rounded(sizeof(Header)))Closure(std::forward<F>(work));
    }
    void run() {
        for(auto &chunk:chunks) {
            for(size_t offset=0;offset<chunk.used;) {
                auto &header=*reinterpret_cast<Header *>(chunk.bytes.get()+offset);
                header.run(chunk.bytes.get()+offset+rounded(sizeof(Header)));offset+=header.size;
            }
            chunk.used=0;
        }
        at=0;count=0;
    }
};
struct Worker {
    std::mutex lock;std::condition_variable work,idle;
    std::deque<Batch *> batches;std::vector<Batch *> spare;
    uint64_t queued=0,done=0;bool running=false;
};
static thread_local bool onWorker;
static thread_local Batch *batch;
static std::atomic<pthread_t> reference{nullptr};
static void noteReference() {
    char name[64]={};pthread_getname_np(pthread_self(),name,sizeof name);
    if(!strncmp(name,"com.apple.coreanimation",23)) {
        pthread_t none=nullptr;reference.compare_exchange_strong(none,pthread_self());
    }
}
static void adoptReference() {
    static bool adopted=false;if(adopted)return;
    const auto source=reference.load();if(!source)return;
    int policy=SCHED_OTHER;sched_param parameters{};
    if(pthread_getschedparam(source,&policy,&parameters))return;
    adopted=true;
    if(policy!=SCHED_OTHER)(void)pthread_setschedparam(pthread_self(),policy,&parameters);
}
static Worker &worker() {
    static Worker *state=[]{
        auto *w=new Worker;pthread_attr_t attr;pthread_t thread;
        if(pthread_attr_init(&attr))return w;
        pthread_attr_set_qos_class_np(&attr,QOS_CLASS_USER_INTERACTIVE,0);
        pthread_attr_setstacksize(&attr,8u<<20);
        const int error=pthread_create(&thread,&attr,[](void *p)->void * {
            auto &w=*static_cast<Worker *>(p);onWorker=true;pthread_setname_np("pong-metal-encode");
            std::unique_lock<std::mutex> lock(w.lock);
            for(;;) {
                w.work.wait(lock,[&]{return !w.batches.empty();});
                auto *next=w.batches.front();w.batches.pop_front();lock.unlock();adoptReference();
                const uint64_t began=nowUs();
                frameEvent(70,next->serial,0,next->queuedUs,began,next->count);
                @autoreleasepool {next->run();}
                frameEvent(70,next->serial,1,next->queuedUs,nowUs(),0);
                lock.lock();w.spare.push_back(next);++w.done;w.idle.notify_all();
            }
            return nullptr;
        },w);
        pthread_attr_destroy(&attr);w->running=!error;
        if(!error)pthread_detach(thread);
        return w;
    }();return *state;
}
static bool enabled(){static bool on=!(getenv("PONG_METAL_ENCODE_SYNC")&&!strcmp(getenv("PONG_METAL_ENCODE_SYNC"),"1"));return on;}
static void flush() {
    if(!batch||!batch->count)return;auto &w=worker();
    {std::lock_guard<std::mutex> lock(w.lock);batch->serial=++w.queued;batch->queuedUs=nowUs();w.batches.push_back(batch);batch=nullptr;}
    w.work.notify_one();
}
template<class F> static void later(F &&call) {
    if(!enabled()||onWorker||!worker().running){call();return;}
    auto &w=worker();
    if(!batch) {
        std::lock_guard<std::mutex> lock(w.lock);
        if(!w.spare.empty()){batch=w.spare.back();w.spare.pop_back();}
    }
    if(!batch)batch=new Batch;
    batch->add(std::forward<F>(call));if(batch->count>=64)flush();
}
static void drain() {
    if(!enabled()||onWorker||!worker().running)return;flush();auto &w=worker();
    std::unique_lock<std::mutex> lock(w.lock);const uint64_t target=w.queued;
    w.idle.wait(lock,[&]{return w.done>=target;});
}
}
#define PONG_ENCODE(...) pongencode::later([=]{__VA_ARGS__;})
