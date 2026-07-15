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

    void push_front(Task* t) {
        t->prev = nullptr;
        t->next = head_;
        if (head_) head_->prev = t;
        else       tail_ = t;
        head_ = t;
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
    bool contains(const Task* task) const {
        for (Task* current = head_; current; current = current->next) {
            if (current == task) return true;
        }
        return false;
    }

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
    double  queued_work_us = 0.0;

    double queued_task_work_us(const Task* t) const {
        if (!t) return 0.0;
        return t->expected_service_time_us / capacity + T_host_us;
    }

    void push_waiting(Task* t) {
        if (!t) return;
        queued_work_us += queued_task_work_us(t);
        wait_queue.push_back(t);
    }

    void push_waiting_front(Task* t) {
        if (!t) return;
        queued_work_us += queued_task_work_us(t);
        wait_queue.push_front(t);
    }

    Task* pop_waiting_front() {
        Task* t = wait_queue.pop_front();
        if (t) {
            queued_work_us -= queued_task_work_us(t);
            if (queued_work_us < 0.0) queued_work_us = 0.0;
        }
        return t;
    }

    void remove_waiting(Task* t) {
        if (!t || !wait_queue.contains(t)) return;
        queued_work_us -= queued_task_work_us(t);
        if (queued_work_us < 0.0) queued_work_us = 0.0;
        wait_queue.remove(t);
    }

    double local_workload_us(double now_us) const {
        double work = queued_work_us;
        if (!idle && running) {
            double residual = finish_time_us - now_us;
            if (residual > 0.0) work += residual;
        }
        return work;
    }
};

} // namespace sim
