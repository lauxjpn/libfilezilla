#include "libfilezilla/rate_limiter.hpp"
#include "libfilezilla/util.hpp"

#include <assert.h>

/*
  Rate limiting machinery based on token buckets with hierarchical limits.
  - Hierarchical: Limits can be nested
  - Fairness: All buckets get a fair share of tokens
  - No waste, excess tokens distributed fairly to buckets with spare capacity
  - Complexity:
	- Token distribution in O(n)
	- Adding/removing buckets/limiters in O(1)
  - No uneeded wakeups during periods of idleness
  - Thread-safe
*/
namespace fz {

namespace {
auto const delay = duration::from_milliseconds(200);
int const frequency = 5;
std::array<direction::type, 2> directions { direction::inbound, direction::outbound };
}

rate_limit_manager::rate_limit_manager(event_loop & loop)
	: event_handler(loop)
{
}

rate_limit_manager::~rate_limit_manager()
{
	assert(limiters_.empty());
	remove_handler();
}

void rate_limit_manager::operator()(event_base const& ev)
{
	dispatch<timer_event>(ev, this, &rate_limit_manager::on_timer);
}

void rate_limit_manager::on_timer(timer_id const& id)
{
	scoped_lock l(mtx_);
	if (++activity_ == 2) {
		timer_id expected = id;
		if (timer_.compare_exchange_strong(expected, 0)) {
			stop_timer(id);
		}

	}
	for (auto * limiter : limiters_) {
		process(limiter, false);
	}
}

void rate_limit_manager::record_activity()
{
	if (activity_.exchange(0) == 2) {
		timer_id old = timer_.exchange(add_timer(duration::from_milliseconds(1000 / frequency), false));
		stop_timer(old);
	}
}

void rate_limit_manager::add(rate_limiter* limiter)
{
	if (!limiter) {
		return;
	}

	limiter->remove_bucket();

	scoped_lock l(mtx_);

	limiter->lock_tree();

	limiter->set_mgr_recursive(this);
	limiter->parent_ = this;
	limiter->idx_ = limiters_.size();
	limiters_.push_back(limiter);

	process(limiter, true);

	limiter->unlock_tree();
}

void rate_limit_manager::process(rate_limiter* limiter, bool locked)
{
	if (!limiter) {
		return;
	}

	// Step 0: Lock all mutexes
	if (!locked) {
		limiter->lock_tree();
	}

	// Step 1: Update stats such as weight and unsaturated buckets
	bool active{};
	limiter->update_stats(active);
	if (active) {
		record_activity();
	}
	for (auto const& d : directions) {
		// Step 2: Add the normal tokens
		limiter->add_tokens(d, rate::unlimited, rate::unlimited);

		// Step 3: Distribute overflow to unsaturated buckets
		limiter->distribute_overflow(d, 0);
	}

	// Step 4: Unlock the tree and potentially wake up consumers
	if (!locked) {
		limiter->unlock_tree();
	}
}

void bucket_base::remove_bucket()
{
	scoped_lock l(mtx_);
	while (idx_ != rate::unlimited && parent_) {
		if (parent_ == mgr_) {
			if (mgr_->mtx_.try_lock()) {
				auto * other = mgr_->limiters_.back();
				if (other != this) {
					scoped_lock ol(other->mtx_);
					other->idx_ = idx_;
					mgr_->limiters_[idx_] = other;
				}
				mgr_->limiters_.pop_back();
				mgr_->mtx_.unlock();
				break;
			}
		}
		else {
			auto * parent = reinterpret_cast<rate_limiter*>(parent_);
			if (parent->mtx_.try_lock()) {
				auto * other = parent->buckets_.back();
				if (other != this) {
					scoped_lock ol(other->mtx_);
					other->idx_ = idx_;
					parent->buckets_[idx_] = other;
				}
				parent->buckets_.pop_back();
				parent->mtx_.unlock();
				break;
			}
		}

		// Break deadlock
		l.unlock();
		sleep(duration::from_milliseconds(1));
		l.lock();
	}
	parent_ = nullptr;
	idx_ = rate::unlimited;
}

void bucket_base::set_mgr_recursive(rate_limit_manager * mgr)
{
	mgr_ = mgr;
}

rate_limiter::~rate_limiter()
{
	{
		scoped_lock l(mtx_);
		for (auto * bucket : buckets_) {
			bucket->parent_ = nullptr;
			bucket->idx_ = rate::unlimited;
		}
		buckets_.clear();
	}

	remove_bucket();
}

void rate_limiter::set_mgr_recursive(rate_limit_manager * mgr)
{
	if (mgr != mgr_) {
		mgr_ = mgr;
		for (auto * bucket : buckets_) {
			bucket->set_mgr_recursive(mgr);
		}
	}
}

void rate_limiter::set_limits(rate::type download_limit, rate::type upload_limit)
{
	scoped_lock l(mtx_);
	bool changed = do_set_limit(direction::inbound, download_limit);
	changed |= do_set_limit(direction::outbound, upload_limit);
	if (changed && mgr_) {
		mgr_->record_activity();
	}
}

bool rate_limiter::do_set_limit(direction::type const d, rate::type limit)
{
	if (limit_[d] == limit) {
		return false;
	}

	limit_[d] = limit;

	size_t weight = weight_ ? weight_ : 1;
	if (limit_[d] != rate::unlimited) {
		merged_tokens_[d] = std::min(merged_tokens_[d], limit_[d] / weight);
	}
	return true;
}

rate::type rate_limiter::limit(direction::type const d)
{
	scoped_lock l(mtx_);
	return limit_[d ? 1 : 0];
}

void rate_limiter::add(bucket_base* bucket)
{
	if (!bucket) {
		return;
	}

	bucket->remove_bucket();

	scoped_lock l(mtx_);

	bucket->lock_tree();

	bucket->set_mgr_recursive(mgr_);
	bucket->parent_ = this;
	bucket->idx_ = buckets_.size();
	buckets_.push_back(bucket);

	bool active{};
	bucket->update_stats(active);
	if (active && mgr_) {
		mgr_->record_activity();
	}

	size_t bucket_weight = bucket->weight();
	if (!bucket_weight) {
		bucket_weight = 1;
	}
	weight_ += bucket_weight;

	for (auto const& d : directions) {
		rate::type tokens;
		if (merged_tokens_[d] == rate::unlimited) {
			tokens = rate::unlimited;
		}
		else {
			tokens = merged_tokens_[d] / (bucket_weight * 2);
		}
		bucket->add_tokens(d, tokens, tokens);
		bucket->distribute_overflow(d, 0);

		if (tokens != rate::unlimited) {
			debt_[d] += tokens * bucket_weight;
		}
	}

	bucket->unlock_tree();
}

void rate_limiter::lock_tree()
{
	mtx_.lock();
	for (auto * bucket : buckets_) {
		bucket->lock_tree();
	}
}

void rate_limiter::unlock_tree()
{
	for (auto * bucket : buckets_) {
		bucket->unlock_tree();
	}
	mtx_.unlock();
}

void rate_limiter::pay_debt(direction::type const d)
{
	if (merged_tokens_[d] != rate::unlimited) {
		size_t weight = weight_ ? weight_ : 1;
		rate::type debt_reduction = std::min(merged_tokens_[d], debt_[d] / weight);
		merged_tokens_[d] -= debt_reduction;
		debt_[d] -= debt_reduction;
	}
	else {
		debt_[d] = 0;
	}
}

rate::type rate_limiter::add_tokens(direction::type const d, rate::type tokens, rate::type limit)
{
	if (!weight_) {
		merged_tokens_[d] = std::min(limit_[d], tokens);
		pay_debt(d);
		return (tokens == rate::unlimited) ? 0 : tokens;
	}

	rate::type merged_limit = limit;
	if (limit_[d] != rate::unlimited) {
		rate::type my_limit = (carry_[d] + limit_[d]) / weight_;
		carry_[d] = (carry_[d] + limit_[d]) % weight_;
		if (my_limit < merged_limit) {
			merged_limit = my_limit;
		}
		carry_[d] += (merged_limit % frequency) * weight_;
	}

	unused_capacity_[d] = 0;

	if (merged_limit != rate::unlimited) {
		merged_tokens_[d] = merged_limit / frequency;
	}
	else {
		merged_tokens_[d] = rate::unlimited;
	}

	if (tokens < merged_tokens_[d]) {
		merged_tokens_[d] = tokens;
	}

	pay_debt(d);

	if (limit_[d] == rate::unlimited) {
		unused_capacity_[d] = rate::unlimited;
	}
	else {
		if (merged_tokens_[d] * weight_ * frequency < limit_[d]) {
			unused_capacity_[d] = limit_[d] - merged_tokens_[d] * weight_ * frequency;
			unused_capacity_[d] /= frequency;
		}
		else {
			unused_capacity_[d] = 0;
		}
	}

	overflow_[d] = 0;
	scratch_buffer_.clear();
	for (size_t i = 0; i < buckets_.size(); ++i) {
		size_t overflow = buckets_[i]->add_tokens(d, merged_tokens_[d], merged_limit);
		if (overflow) {
			overflow_[d] += overflow;
		}
		if (buckets_[i]->unsaturated(d)) {
			scratch_buffer_.push_back(i);
		}
		else {
			overflow_[d] += buckets_[i]->distribute_overflow(d, 0);
		}
	}
	if (overflow_[d] >= unused_capacity_[d]) {
		unused_capacity_[d] = 0;
	}
	else if (unused_capacity_[d] != rate::unlimited) {
		unused_capacity_[d] -= overflow_[d];
	}

	if (tokens == rate::unlimited) {
		return 0;
	}
	else {
		return (tokens - merged_tokens_[d]) * weight_;
	}
}


rate::type rate_limiter::distribute_overflow(direction::type const d, rate::type overflow)
{
	rate::type usable_external_overflow;
	if (unused_capacity_[d] == rate::unlimited) {
		usable_external_overflow = overflow;
	}
	else {
		usable_external_overflow = std::min(overflow, unused_capacity_[d]);
	}
	rate::type const overflow_sum = overflow_[d] + usable_external_overflow;
	rate::type remaining = overflow_sum;

	while (true) {
		size_t size{};
		for (auto idx : scratch_buffer_) {
			size += buckets_[idx]->unsaturated(d);
		}
		unsaturated_[d] = size;

		if (!remaining || scratch_buffer_.empty()) {
			break;
		}

		rate::type const extra_tokens = remaining / size;
		remaining %= size;
		for (size_t i = 0; i < scratch_buffer_.size(); ) {
			rate::type sub_overflow = buckets_[scratch_buffer_[i]]->distribute_overflow(d, extra_tokens);
			if (sub_overflow) {
				remaining += sub_overflow;
				scratch_buffer_[i] = scratch_buffer_.back();
				scratch_buffer_.pop_back();
			}
			else {
				++i;
			}
		}
		if (!extra_tokens) {
			break;
		}
	}

	if (usable_external_overflow > remaining) {
		// Exhausted internal overflow
		unused_capacity_[d] -= usable_external_overflow - remaining;
		overflow_[d] = 0;
		return remaining + overflow - usable_external_overflow;
	}
	else {
		// Internal overflow not exhausted
		overflow_[d] = remaining - usable_external_overflow;
		return overflow;
	}
}

void rate_limiter::update_stats(bool & active)
{
	weight_ = 0;

	unsaturated_[0] = 0;
	unsaturated_[1] = 0;
	for (size_t i = 0; i < buckets_.size(); ++i) {
		buckets_[i]->update_stats(active);
		weight_ += buckets_[i]->weight();
		for (auto const d : directions) {
			unsaturated_[d] += buckets_[i]->unsaturated(d);
		}
	}
}


bucket::~bucket()
{
	remove_bucket();
}

rate::type bucket::add_tokens(direction::type const d, rate::type tokens, rate::type limit)
{
	if (limit == rate::unlimited) {
		bucket_size_[d] = rate::unlimited;
		available_[d] = rate::unlimited;
		return 0;
	}
	else {
		bucket_size_[d] = limit * overflow_multiplier_[d]; // TODO: Tolerance
		if (available_[d] == rate::unlimited) {
			available_[d] = tokens;
			return 0;
		}
		else if (bucket_size_[d] < available_[d]) {
			available_[d] = bucket_size_[d];
			return tokens;
		}
		else {
			rate::type capacity = bucket_size_[d] - available_[d];
			if (capacity < tokens && unsaturated_[d]) {
				unsaturated_[d] = false;
				if (overflow_multiplier_[d] < 1024*1024) {
					capacity += bucket_size_[d];
					bucket_size_[d] *= 2;
					overflow_multiplier_[d] *= 2;
				}
			}
			rate::type added = std::min(tokens, capacity);
			rate::type ret = tokens - added;
			available_[d] += added;
			return ret;
		}
	}
}

rate::type bucket::distribute_overflow(direction::type const d, rate::type tokens)
{
	if (available_[d] == rate::unlimited) {
		return 0;
	}

	rate::type capacity = bucket_size_[d] - available_[d];
	if (capacity < tokens && unsaturated_[d]) {
		unsaturated_[d] = false;
		if (overflow_multiplier_[d] < 1024*1024) {
			capacity += bucket_size_[d];
			bucket_size_[d] *= 2;
			overflow_multiplier_[d] *= 2;
		}
	}
	rate::type added = std::min(tokens, capacity);
	rate::type ret = tokens - added;
	available_[d] += added;
	return ret;
}

void bucket::unlock_tree()
{
	for (auto const& d : directions) {
		if (waiting_[d] && available_[d]) {
			waiting_[d] = false;
			wakeup(static_cast<direction::type>(d));
		}
	}
	bucket_base::unlock_tree();
}

void bucket::update_stats(bool & active)
{
	for (auto const& d : directions) {
		if (bucket_size_[d] == rate::unlimited) {
			overflow_multiplier_[d] = 1;
		}
		else {
			if (available_[d] > bucket_size_[d] / 2 && overflow_multiplier_[d] > 1) {
				overflow_multiplier_[d] /= 2;
			}
			else {
				unsaturated_[d] = waiting_[d];
				if (waiting_[d]) {
					active = true;
				}
			}
		}
	}
}

rate::type bucket::available(direction::type const d)
{
	if (d != direction::inbound && d != direction::outbound) {
		return rate::unlimited;
	}

	scoped_lock l(mtx_);
	if (!available_[d]) {
		waiting_[d] = true;
		if (mgr_) {
			mgr_->record_activity();
		}
	}
	return available_[d];
}

void bucket::consume(direction::type const d, rate::type amount)
{
	if (!amount) {
		return;
	}
	if (d != direction::inbound && d != direction::outbound) {
		return;
	}
	scoped_lock l(mtx_);
	if (available_[d] != rate::unlimited) {
		if (mgr_) {
			mgr_->record_activity();
		}
		if (available_[d] > amount) {
			available_[d] -= amount;
		}
		else {
			available_[d] = 0;
		}
	}
}

}
