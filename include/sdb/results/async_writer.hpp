#pragma once
#include "results.hpp"
#include <condition_variable>
#include <list>
#include <mutex>
#include <thread>

namespace sdb::lifecycle {
// One producer, one worker. Storage must outlive this object and must not be
// accessed by another thread until close(). Frames transfer ownership after
// admission; a blocked producer retains its frame. No borrowed StateDB views.
class AsyncResultWriter {
  public:
    struct Stats {
        // committed means stream-flushed frames. Stronger durability is only
        // acknowledged by a successful barrier(Level) return.
        std::uint64_t queued = 0, written = 0, committed = 0;
        std::uint64_t resident_bytes = 0, peak_bytes = 0;
        bool cancelled = false, failed = false;
    };
    // Conservative admission charge: owned capacities + native encoding +
    // validation tree scratch. Persistent model/storage and caller-owned frames
    // are outside this queue budget. Storage-specific codec scratch is charged
    // separately for each admitted frame (conservative for one worker).
    static std::uint64_t required_bytes(const ResultFrame &f) {
        std::size_t n = sizeof(ResultFrame) + 65536; // codec/footer/stream and list bookkeeping reserve
        auto add = [&](std::size_t v) { n = sum(n, v); };
        auto str = [&](const std::string &s) { add(sum(s.capacity(), 1)); };
        add(product(f.fields.capacity(), sizeof(ResultField)));
        for (const auto &field : f.fields) {
            const auto &d = field.descriptor;
            str(d.name);
            str(d.region);
            str(d.basis);
            str(d.provenance);
            add(product(d.components.capacity(), sizeof(std::string)));
            for (auto &c : d.components)
                str(c);
            add(product(field.ids.capacity(), sizeof(Id)));
            add(product(field.values.capacity(), sizeof(double)));
            if (field.typed) {
                const auto &p = *field.typed;
                // Admission accounting must not allocate validation trees before
                // the budget check/backpressure. The worker validates after the
                // owned frame and all validation scratch have been charged.
                str(p.schema_id); str(p.schema.name); str(p.schema.units); str(p.schema.basis);
                add(product(p.schema.shape.capacity(), sizeof(std::uint32_t)));
                add(product(p.schema.components.capacity(), sizeof(std::string)));
                for (const auto &c : p.schema.components) str(c);
                add(product(p.addresses.capacity(),sizeof(ResultAddress)));
                add(p.validity.capacity());
                std::visit([&](const auto &v) { add(product(v.capacity(),sizeof(typename std::decay_t<decltype(v)>::value_type))); },p.values);
                // Address validation tree, sorted row permutation, block/codec scratch,
                // and directory metadata. Charges are conservative and checked.
                add(product(p.addresses.size(),sizeof(ResultAddress)+64+sizeof(std::size_t)));
                add(product(3, native::encoded_size(p,std::numeric_limits<std::size_t>::max())));
            }
            // validate_frame builds per-field entity sets and one key per field.
            add(product(field.ids.size(), 64));
            add(256);
            str(d.name);
            str(d.region);
        }
        if (std::none_of(f.fields.begin(),f.fields.end(),[](const auto &v){return bool(v.typed);}))
            add(static_cast<std::size_t>(native::encoded_size(f, std::numeric_limits<std::size_t>::max())));
        else {
            add(native::encoded_size(f.info,std::numeric_limits<std::size_t>::max()));
            for (const auto &field : f.fields) {
                add(native::encoded_size(field.descriptor,std::numeric_limits<std::size_t>::max()));
                if (!field.typed) add(native::encoded_size(field,std::numeric_limits<std::size_t>::max()));
            }
        }
        return n;
    }

  private:
    struct Entry {
        ResultFrame frame;
        std::uint64_t bytes;
    };
    IResultStorage &storage_;
    const std::uint64_t budget_;
    const std::uint64_t storage_overhead_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::list<Entry> queue_;
    Stats stats_;
    std::optional<FrameInfo> last_;
    std::exception_ptr error_;
    bool stopping_ = false, closed_ = false;
    std::uint64_t flush_requested_ = 0, flush_completed_ = 0;
    durability::Level requested_level_ = durability::Level::StreamFlush;
    ResultBarrier receipt_;
    std::thread worker_;

    void healthy() const {
        if (error_)
            std::rethrow_exception(error_);
        if (stats_.cancelled)
            throw std::runtime_error("result writer cancelled");
    }
    void work() noexcept {
        std::list<Entry> active;
        try {
            for (;;) {
                std::unique_lock lock(mutex_);
                changed_.wait(lock, [&] {
                    return stopping_ || !queue_.empty() || flush_requested_ != flush_completed_;
                });
                if (stats_.cancelled) {
                    queue_.clear();
                    stats_.resident_bytes = 0;
                    changed_.notify_all();
                    return;
                }
                if (!queue_.empty()) {
                    active.splice(active.end(), queue_, queue_.begin());
                    const auto bytes = active.front().bytes;
                    lock.unlock();
                    for (const auto &field : active.front().frame.fields)
                        if (field.typed) validate_typed_payload(*field.typed);
                    storage_.append(active.front().frame);
                    lock.lock();
                    ++stats_.written;
                    lock.unlock();
                    storage_.flush();
                    auto last = storage_.last();
                    active.clear(); // release memory BEFORE releasing admission budget
                    lock.lock();
                    ++stats_.committed;
                    last_ = last;
                    stats_.resident_bytes -= bytes;
                    changed_.notify_all();
                } else if (flush_requested_ != flush_completed_) {
                    auto ticket = flush_requested_;
                    auto level = requested_level_;
                    lock.unlock();
                    auto receipt = storage_.barrier(level);
                    if (receipt.level != level)
                        throw std::runtime_error("result backend returned wrong durability level");
                    lock.lock();
                    receipt_ = std::move(receipt);
                    flush_completed_ = ticket;
                    changed_.notify_all();
                } else if (stopping_)
                    return;
            }
        } catch (...) {
            active.clear();
            std::lock_guard lock(mutex_);
            error_ = std::current_exception();
            stats_.failed = true;
            queue_.clear();
            stats_.resident_bytes = 0;
            changed_.notify_all();
        }
    }

  public:
    explicit AsyncResultWriter(IResultStorage &storage, std::uint64_t budget)
        : storage_(storage), budget_(budget), storage_overhead_(storage.admission_overhead()), last_(storage.last()) {
        if (!budget)
            throw std::invalid_argument("zero async result budget");
        worker_ = std::thread([this] { work(); });
    }
    AsyncResultWriter(const AsyncResultWriter &) = delete;
    AsyncResultWriter &operator=(const AsyncResultWriter &) = delete;
    ~AsyncResultWriter() {
        // Exceptions cannot escape destruction. Applications MUST call close()
        // to observe success/failure; destructor merely guarantees worker lifetime.
        try {
            close();
        } catch (...) {
        }
    }
    void write(ResultFrame &&frame) {
        const auto bytes = frame.fields.empty() ? 0 : sum(required_bytes(frame), storage_overhead_);
        std::unique_lock lock(mutex_);
        healthy();
        if (stopping_ || closed_)
            throw std::logic_error("result writer closed");
        if (!bytes)
            return;
        if (bytes > budget_)
            throw std::length_error("frame exceeds async result budget");
        changed_.wait(lock,
                      [&] { return error_ || stats_.cancelled || bytes <= budget_ - stats_.resident_bytes; });
        healthy();
        // The producer must not access frame after this ownership transfer.
        queue_.push_back({std::move(frame), bytes});
        ++stats_.queued;
        stats_.resident_bytes += bytes;
        stats_.peak_bytes = std::max(stats_.peak_bytes, stats_.resident_bytes);
        changed_.notify_all();
    }
    // One producer issues writes/barriers/close in order. A control thread may
    // only cancel. The worker owns storage until close() has joined it.
    ResultBarrier barrier(durability::Level level) {
        durability::require_supported(level);
        std::unique_lock lock(mutex_);
        healthy();
        if (closed_) {
            if (level != durability::Level::StreamFlush)
                throw std::logic_error("durability barrier requires an open async writer");
            return receipt_;
        }
        requested_level_ = level;
        const auto ticket = ++flush_requested_;
        changed_.notify_all();
        changed_.wait(lock, [&] { return error_ || stats_.cancelled || flush_completed_ >= ticket; });
        healthy();
        return receipt_;
    }
    void flush() { (void)barrier(durability::Level::StreamFlush); }
    void close() {
        std::exception_ptr failure;
        try {
            flush();
        } catch (...) {
            failure = std::current_exception();
        }
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
            changed_.notify_all();
        }
        if (worker_.joinable())
            worker_.join();
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        if (failure)
            std::rethrow_exception(failure);
    }
    // May be called from a control thread to interrupt backpressure. An in-flight
    // storage operation is allowed to finish; close() joins and reports cancel.
    void cancel() {
        std::lock_guard lock(mutex_);
        if (!closed_) {
            stats_.cancelled = true;
            stopping_ = true;
            changed_.notify_all();
        }
    }
    Stats stats() const {
        std::lock_guard lock(mutex_);
        return stats_;
    }
    std::optional<FrameInfo> last() const {
        std::lock_guard lock(mutex_);
        return last_;
    }
};
} // namespace sdb::lifecycle
