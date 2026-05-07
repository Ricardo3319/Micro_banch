#pragma once
#include "sim/model/task.h"
#include <cstddef>

namespace sim {

// Intrusive doubly-linked FIFO queue for O(1) push / pop / middle-remove.
class TaskQueue {
public:
    void push_back(Task* t) {
        t->prev = tail_;
        t->next = nullptr;
        if (tail_) tail_->next = t;
        else       head_ = t;
        tail_ = t;
        ++size_;
    }

    Task* pop_front() {
        if (!head_) return nullptr;
        Task* t = head_;
        head_ = head_->next;
        if (head_) head_->prev = nullptr;
        else       tail_ = nullptr;
        t->prev = t->next = nullptr;
        --size_;
        return t;
    }

    void remove(Task* t) {
        if (t->prev) t->prev->next = t->next;
        else         head_ = t->next;
        if (t->next) t->next->prev = t->prev;
        else         tail_ = t->prev;
        t->prev = t->next = nullptr;
        --size_;
    }

    Task*  front() const { return head_; }
    size_t size()  const { return size_; }
    bool   empty() const { return size_ == 0; }
    void   clear() { head_ = tail_ = nullptr; size_ = 0; }

    // Iteration helpers.
    Task* begin() const { return head_; }

private:
    Task*  head_ = nullptr;
    Task*  tail_ = nullptr;
    size_t size_ = 0;
};

// Per-core execution resource.
struct Core {
    int     core_id    = 0;
    int     host_id    = 0;
    double  capacity   = 1.0; // C_j
    bool    idle       = true;
    Task*   running    = nullptr;
    double  finish_time_us = 0.0;
    TaskQueue wait_queue;

    double local_workload_us(double now_us) const {
        double work = 0.0;
        if (!idle && running) {
            double residual = finish_time_us - now_us;
            if (residual > 0.0) work += residual;
        }
        Task* cur = wait_queue.begin();
        while (cur) {
            work += cur->expected_service_time_us / capacity + T_host_us;
            cur = cur->next;
        }
        return work;
    }
};

} // namespace sim
