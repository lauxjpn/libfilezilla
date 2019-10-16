#ifndef LIBFILEZILLA_RATE_LIMITER_HEADER
#define LIBFILEZILLA_RATE_LIMITER_HEADER

#include "event_handler.hpp"

#include <atomic>
#include <vector>

namespace fz {

namespace rate {
using type = uint64_t;
enum : type {
	unlimited = static_cast<type>(-1)
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
	virtual ~bucket_base() noexcept = default;

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

	virtual rate::type add_tokens(direction::type const /*d*/, rate::type /*tokens*/, rate::type /*limit*/) = 0;
	virtual rate::type distribute_overflow(direction::type const /*d*/, rate::type /*tokens*/) { return 0; }

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

	void set_limits(rate::type download_limit, rate::type upload_limit);
	rate::type limit(direction::type const d);

private:
	friend class bucket_base;
	friend class rate_limit_manager;

	virtual void lock_tree() override;

	bool do_set_limit(direction::type const d, rate::type limit);

	virtual void update_stats(bool & active) override;
	virtual size_t weight() const override { return weight_; }
	virtual size_t unsaturated(direction::type const d) const override { return unused_capacity_[d] ? unsaturated_[d] : 0; }
	virtual void set_mgr_recursive(rate_limit_manager * mgr) override;

	virtual rate::type add_tokens(direction::type const d, rate::type tokens, rate::type limit) override;
	virtual rate::type distribute_overflow(direction::type const d, rate::type tokens) override;

	virtual void unlock_tree() override;

	void pay_debt(direction::type const d);

	rate::type limit_[2] = {rate::unlimited, rate::unlimited};

	std::vector<bucket_base*> buckets_;
	size_t weight_{};

	size_t unsaturated_[2]{};
	std::vector<size_t> scratch_buffer_;
	rate::type overflow_[2]{};
	rate::type merged_tokens_[2]{};
	rate::type debt_[2]{};
	rate::type unused_capacity_[2]{};
	rate::type carry_[2]{};
};

class FZ_PUBLIC_SYMBOL bucket : public bucket_base
{
public:
	virtual ~bucket();

	rate::type available(direction::type const d);
	void consume(direction::type const d, rate::type amount);

protected:
	virtual void wakeup(direction::type /*d*/) {}

private:
	virtual void update_stats(bool & active) override;
	virtual size_t unsaturated(direction::type const d) const override { return unsaturated_[d] ? 1 : 0; }

	virtual rate::type add_tokens(direction::type const d, rate::type tokens, rate::type limit) override;
	virtual rate::type distribute_overflow(direction::type const d, rate::type tokens) override;

	virtual void unlock_tree() override;

	rate::type available_[2] = {rate::unlimited, rate::unlimited};
	rate::type overflow_multiplier_[2]{1, 1};
	rate::type bucket_size_[2]{rate::unlimited, rate::unlimited};
	bool waiting_[2]{};
	bool unsaturated_[2]{};
};

}

#endif
