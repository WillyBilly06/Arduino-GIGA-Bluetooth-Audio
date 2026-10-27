#include "../src/platform/TxMailbox.h"
#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <vector>
using giga_bredr::detail::TxMailbox;
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#condition);abort(); } } while (0)

// Real complete H4 ACL packets, including their ACL length field. The mailbox
// owns whole-buffer publication; protocol framing remains its caller's job.
static std::vector<uint8_t> packet(uint32_t serial,size_t length) {
    CHECK(length>=5 && length<=TxMailbox::Capacity);
    std::vector<uint8_t> out(length);
    uint32_t value=serial^0x91e10da5u;
    for(size_t i=0;i<length;++i) {value=value*1664525u+1013904223u;out[i]=uint8_t(value>>24);}
    out[0]=2;out[1]=uint8_t(serial);out[2]=uint8_t((serial>>8)&15);
    const uint16_t payload=uint16_t(length-5);out[3]=uint8_t(payload);out[4]=uint8_t(payload>>8);
    return out;
}
static void empty_and_invalid() {
    TxMailbox box;
    uint8_t sentinel=0x7b;
    CHECK(box.empty() && !box.drained());
    CHECK(!box.peek(sentinel) && sentinel==0x7b);
    CHECK(!box.advance() && !box.finish());
    auto valid=packet(1,5);
    CHECK(!box.submit(nullptr,1));
    CHECK(!box.submit(valid.data(),0));
    CHECK(!box.submit(valid.data(),size_t(TxMailbox::Capacity)+1));
    CHECK(box.empty() && !box.peek(sentinel));
}
static void boundary_copy_and_two_phase_completion() {
    TxMailbox box;
    auto input=packet(31,TxMailbox::Capacity),wanted=input;
    CHECK(box.submit(input.data(),input.size()));
    memset(input.data(),0x55,input.size()); // Producer's original storage is borrowed no longer.
    auto replacement=packet(77,5);
    CHECK(!box.empty() && !box.drained() && !box.finish());
    CHECK(!box.submit(replacement.data(),replacement.size()));
    CHECK(!box.submit(nullptr,0));
    for(size_t i=0;i<wanted.size();++i) {
        uint8_t byte=0,again=0;
        CHECK(box.peek(byte) && byte==wanted[i]);
        CHECK(box.peek(again) && again==byte); // Peeking does not consume.
        CHECK(!box.finish());
        CHECK(box.advance());
        CHECK(!box.empty()); // Even the last byte cannot give ownership away.
        CHECK(box.drained()==(i+1==wanted.size()));
    }
    uint8_t sentinel=0xe3;
    CHECK(!box.peek(sentinel) && sentinel==0xe3 && !box.advance());
    CHECK(!box.submit(replacement.data(),replacement.size()));
    bool tx_irq_attached=true,completion_saved=false;
    CHECK(box.drained() && !box.empty());
    tx_irq_attached=false;completion_saved=true; // Real owner detaches and records before finish().
    CHECK(!tx_irq_attached && completion_saved && box.finish());
    CHECK(box.empty() && !box.drained() && !box.finish());
    CHECK(box.submit(replacement.data(),replacement.size()));
    for(uint8_t expected:replacement) {uint8_t byte;CHECK(box.peek(byte)&&byte==expected);CHECK(box.advance());}
    CHECK(box.finish());
}
static void stalled_uart_and_partitions() {
    const uint32_t budgets[]={0,1,2,3,7,16,31,64,127,1030};
    const size_t lengths[]={5,6,17,128,259,1029,1030};
    for(size_t length:lengths)for(uint32_t budget:budgets) {
        TxMailbox box;auto wanted=packet(uint32_t(length+budget),length);
        CHECK(box.submit(wanted.data(),wanted.size()));
        std::vector<uint8_t> accepted;uint32_t polls=0;
        while(!box.drained()) {
            ++polls;CHECK(polls<10000);
            uint32_t allowance=budget?budget:(polls%2);
            for(uint32_t i=0;i<allowance;++i) {
                uint8_t byte; if(!box.peek(byte))break;
                // On every third poll UART acceptance stalls. No advance may
                // occur, and the next peek must expose exactly the same byte.
                if(polls%3==0) {uint8_t again;CHECK(box.peek(again)&&again==byte);break;}
                accepted.push_back(byte);CHECK(box.advance());
            }
            CHECK(!box.empty());
        }
        CHECK(accepted==wanted && box.finish() && box.empty());
    }
}
static void stopped_reset_and_restart() {
    TxMailbox box;auto first=packet(44,91),next=packet(45,37);
    CHECK(box.submit(first.data(),first.size()));
    for(unsigned i=0;i<17;++i) {uint8_t byte;CHECK(box.peek(byte)&&byte==first[i]);CHECK(box.advance());}
    // The external owner has stopped its IRQ and producer before reset.
    box.resetStopped();CHECK(box.empty()&&!box.drained());
    CHECK(box.submit(next.data(),next.size()));
    for(uint8_t expected:next) {uint8_t byte;CHECK(box.peek(byte)&&byte==expected);CHECK(box.advance());}
    CHECK(box.drained());box.resetStopped();CHECK(box.empty());
    box.resetStopped();CHECK(box.empty());
    CHECK(box.submit(first.data(),first.size()));
    for(uint8_t expected:first) {uint8_t byte;CHECK(box.peek(byte)&&byte==expected);CHECK(box.advance());}
    CHECK(box.finish());
}
static void real_thread_handoff_stress() {
    TxMailbox box;
    constexpr uint32_t Count=4096;
    // These metadata fields are deliberately non-atomic: finish's release and
    // the producer's empty acquire must publish them before mailbox reuse.
    uint32_t completion_saved=0,irq_detached=0;
    std::atomic<uint64_t> checked_bytes{0};
    std::thread producer([&]() {
        for(uint32_t serial=0;serial<Count;++serial) {
            auto input=packet(serial,5+(serial*137u)%(TxMailbox::Capacity-4));
            while(!box.empty())std::this_thread::yield();
            // The consumer cannot change metadata again until this producer
            // publishes the next packet, so these reads are race-free.
            CHECK(completion_saved==serial && irq_detached==serial);
            CHECK(box.submit(input.data(),input.size()));
            memset(input.data(),0xac,input.size());
        }
        while(!box.empty())std::this_thread::yield();
        CHECK(completion_saved==Count && irq_detached==Count);
    });
    std::thread consumer([&]() {
        uint64_t count=0;uint32_t turn=0;
        for(uint32_t serial=0;serial<Count;++serial) {
            const auto expected=packet(serial,5+(serial*137u)%(TxMailbox::Capacity-4));
            size_t cursor=0;
            while(cursor<expected.size()) {
                uint8_t byte=0;
                if(!box.peek(byte)) {std::this_thread::yield();continue;}
                CHECK(byte==expected[cursor]);
                if((++turn&7u)==0) { // Hardware refused this attempt; preserve ownership.
                    uint8_t again;CHECK(box.peek(again)&&again==byte);std::this_thread::yield();continue;
                }
                CHECK(box.advance());++cursor;++count;
            }
            CHECK(box.drained() && !box.empty());
            irq_detached=serial+1;completion_saved=serial+1;
            CHECK(box.finish()); // No access to this packet after publication.
        }
        checked_bytes.store(count);
    });
    producer.join();consumer.join();
    CHECK(box.empty() && checked_bytes.load()>2000000);
    printf("Threaded mailbox: %u packets, %llu exact bytes\n",Count,(unsigned long long)checked_bytes.load());
}
int main() {
    empty_and_invalid();boundary_copy_and_two_phase_completion();stalled_uart_and_partitions();
    stopped_reset_and_restart();real_thread_handoff_stress();
    puts("PASS UART TX mailbox ownership, stalls, budgets, capacity, finish and restart");
}
