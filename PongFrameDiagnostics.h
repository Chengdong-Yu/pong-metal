/* SPDX-License-Identifier: MIT */
#pragma once
#include "NativeGpuTraceStore.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <errno.h>

/* Upstream diagnostic policy: hot render-server records stay in userspace.
 * Use the existing Pong 120-byte record ABI, rather than reinterpret the
 * newer component's 72-byte frames.trace as a Darwin24 kernel trace. */
namespace PongFrameDiagnostics {
static NativeGpuTraceStore store;
static bool owner() {return !strcmp(getprogname(),"backboardd");}
static bool writeAt(int fd,const void *data,size_t bytes,off_t offset) {
    const auto *p=static_cast<const uint8_t *>(data);
    while(bytes) {
        const ssize_t n=pwrite(fd,p,bytes,offset);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)return false;
        p+=n;bytes-=size_t(n);offset+=n;
    }
    return true;
}
static void start() {
    static std::once_flag once;
    std::call_once(once,[]{
        if(!owner())return;
        store.slots=static_cast<NativeGpuTraceSlot *>(calloc(8192,sizeof(NativeGpuTraceSlot)));
        if(!store.slots)return;
        store.capacity=8192;
        std::thread([]{
            pthread_setname_np("pong-frame-trace");
            (void)pthread_set_qos_class_self_np(QOS_CLASS_UTILITY,0);
            const char *path="/private/var/mobile/Library/Caches/org.unixnu.metal/frames.trace";
            const int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0600);
            struct stat info{};
            if(fd<0 || fstat(fd,&info) || !S_ISREG(info.st_mode) || info.st_uid!=getuid()) {
                if(fd>=0)close(fd);
                fprintf(stderr,"Pong frames: local trace unavailable errno=%d\n",errno);return;
            }
            NativeGpuTraceHeader header{0x31545047,NativeGpuTraceVersion,sizeof(NativeGpuTraceRecord),0,0,0};
            uint64_t next=1,lastSync=0,lastSyncUs=0;
            NativeGpuTraceRecord records[256];
            for(;;) {
                usleep(250000);
                const uint64_t issued=__atomic_load_n(&store.issued,__ATOMIC_ACQUIRE);
                if(issued>=next+store.capacity) {
                    const uint64_t oldest=issued-store.capacity+1;
                    __atomic_add_fetch(&store.dropped,oldest-next,__ATOMIC_RELAXED);next=oldest;
                }
                while(next<=issued) {
                    unsigned count=0;
                    while(next<=issued && count<256) {
                        if(store.read(next,&records[count]))++count;
                        ++next;
                    }
                    // A bounded local file, read through the existing Data log path.
                    if(header.count+count>65536) {
                        header.count=0;if(ftruncate(fd,sizeof(header))) {close(fd);return;}
                    }
                    if(count && !writeAt(fd,records,count*sizeof(records[0]),sizeof(header)+off_t(header.count)*sizeof(records[0]))) {close(fd);return;}
                    header.count+=count;
                }
                header.issued=issued;header.dropped=__atomic_load_n(&store.dropped,__ATOMIC_RELAXED);
                if(!writeAt(fd,&header,sizeof(header),0)) {close(fd);return;}
                const uint64_t now=clock_gettime_nsec_np(CLOCK_UPTIME_RAW)/1000;
                if(issued!=lastSync && now-lastSyncUs>=1000000){(void)fsync(fd);lastSync=issued;lastSyncUs=now;}
            }
        }).detach();
    });
}
static void record(NativeGpuTraceRecord row) {
    if(!owner())return;
    // Texture dumps and transfer-site chatter overwhelmed the finite trace
    // during startup. Attachment formats/pass areas and causal command links
    // retain the colour and latency evidence without duplicating descriptors.
    switch(row.event){case 15:case 31:case 35:case 37:case 44:case 55:return;default:break;}
    start();row.pid=uint32_t(getpid());row.us=clock_gettime_nsec_np(CLOCK_UPTIME_RAW)/1000;
    (void)store.push(row);
}
}
