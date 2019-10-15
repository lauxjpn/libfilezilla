#ifndef LIBFILEZILLA_RATE_LIMITER_HEADER
#define LIBFILEZILLA_RATE_LIMITER_HEADER

#include "event_handler.hpp"

#include <atomic>
#include <vector>

namespace fz {

namespace rate {
enum : size_t {
	unlimited = static_cast<size_t>(-1)
};
}

namespace direction {
enum type : size_t {
	inbound,
	outbound
};
}

class rate_limiter;
class FZ_PUBLIC_SYMBOL rate_limit_manager final : public event_handler
{
public:
	explicit rate_limit_manager(event_loop & loop);
	virtual ~rate_limit_manager();

	void add(rate_limiter* limiter);

private:
	friend class rate_limiter;
	friend class bucket_base;
	friend class bucket;

	void record_activity();

	void operator()(event_base const& ev);
	void on_timer(timer_id const&);

	void process(rate_limiter* limiter, bool locked);

	mutex mtx_{false};
	std::vector<rate_limiter*> limiters_;

	std::atomic<timer_id> timer_{};

	std::atomic<int> activity_{2};
};

class FZ_PUBLIC_SYMBOL bucket_base
{
public:
	virtual ~bucket_base() = default;

	/// Must be called in the most-received class
	void remove_bucket();

protected:
	friend class rate_limiter;


	virtual void lock_tree() { mtx_.lock(); }

	// The following functions must only be caled with a locked tree
	virtual void update_stats(bool & active) = 0;
	virtual size_t weight() const { return 1; }
	virtual size_t unsaturated(direction::type const /*d*/) const { return 0; }
	virtual void set_mgr_recursive(rate_limit_manager * mgr);

	virtual size_t add_tokens(direction::type const /*d*/, size_t /*tokens*/, size_t /*limit*/) = 0;
	virtual size_t distribute_overflow(direction::type const /*d*/, size_t /*tokens*/) { return 0; }

	virtual void unlock_tree() { mtx_.unlock(); }

	mutex mtx_{false};
	rate_limit_manager * mgr_{};
	void * parent_{};
	size_t idx_{static_cast<size_t>(-1)};
};

class FZ_PUBLIC_SYMBOL rate_limiter final : public bucket_base
{
public:
	virtual ~rate_limiter();

	void add(bucket_base* bucket);

	void set_limits(size_t download_limit, size_t upload_limit);
	size_t limit(direction::type const d);

private:
	friend class bucket_base;
	friend class rate_limit_manager;

	virtual void lock_tree() override;

	bool do_set_limit(direction::type const d, size_t limit);

	virtual void update_stats(bool & active) override;
	virtual size_t weight() const override { return weight_; }
	virtual size_t unsaturated(direction::type const d) const override { return unused_capacity_[d] ? unsaturated_[d] : 0; }
	virtual void set_mgr_recursive(rate_limit_manager * mgr) override;

	virtual size_t add_tokens(direction::type const d, size_t tokens, size_t limit) override;
	virtual size_t distribute_overflow(direction::type const d, size_t tokens) override;

	virtual void unlock_tree() override;

	void pay_debt(direction::type const d);

	size_t limit_[2] = {rate::unlimited, rate::unlimited};

	std::vector<bucket_base*> buckets_;
	size_t weight_{};

	size_t unsaturated_[2]{};
	std::vector<size_t> scratch_buffer_;
	size_t overflow_[2]{};
	size_t merged_tokens_[2]{};
	size_t debt_[2]{};
	size_t unused_capacity_[2]{};
	size_t carry_[2]{};
};

class FZ_PUBLIC_SYMBOL bucket : public bucket_base
{
public:
	virtual ~bucket();

	size_t available(direction::type const d);
	void consume(direction::type const d, size_t amount);

protected:
	virtual void wakeup(direction::type /*d*/) {}

private:
	virtual void update_stats(bool & active) override;
	virtual size_t unsaturated(direction::type const d) const override { return unsaturated_[d] ? 1 : 0; }

	virtual size_t add_tokens(direction::type const d, size_t tokens, size_t limit) override;
	virtual size_t distribute_overflow(direction::type const d, size_t tokens) override;

	virtual void unlock_tree() override;

	size_t available_[2] = {rate::unlimited, rate::unlimited};
	size_t overflow_multiplier_[2]{1, 1};
	bool waiting_[2]{};
	bool unsaturated_[2]{};

	size_t bucket_size_[2]{rate::unlimited, rate::unlimited};
};

}

#endif
