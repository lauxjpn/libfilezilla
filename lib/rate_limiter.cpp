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

auto const delay = duration::from_milliseconds(200);
int const frequency = 5;

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
		process(limiter);
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

	{
		limiter->lock_tree();

		limiter->set_mgr_recursive(this);
		limiter->parent_ = this;
		limiter->idx_ = limiters_.size();

		limiter->unlock_tree();

		limiters_.push_back(limiter);
	}
	process(limiter);
}

void rate_limit_manager::process(rate_limiter* limiter)
{
	if (!limiter) {
		return;
	}

	// Step 0: Lock all mutexes
	limiter->lock_tree();

	// Step 1: Update stats such as weight and unsaturated buckets
	bool active{};
	limiter->update_stats(active);
	if (active) {
		record_activity();
	}
	for (size_t i = 0; i < 2; ++i) {
		// Step 2: Add the normal tokens
		limiter->add_tokens(i, unlimited, unlimited);

		// Step 3: Distribute overflow to unsaturated buckets
		limiter->distribute_overflow(i, 0);
	}

	// Step 4: Unlock the tree and potentially wake up consumers
	limiter->unlock_tree();
}

void bucket_base::remove_bucket()
{
	scoped_lock l(mtx_);
	while (idx_ != unlimited && parent_) {
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
	idx_ = unlimited;
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
			bucket->idx_ = unlimited;
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

void rate_limiter::set_limits(size_t download_limit, size_t upload_limit)
{
	scoped_lock l(mtx_);
	limit_[0] = download_limit;
	limit_[1] = upload_limit;
	size_t weight = weight_ ? weight_ : 1;
	for (size_t i = 0; i < 2; ++i) {
		if (limit_[i] != unlimited) {
			merged_tokens_[i] = std::min(merged_tokens_[i], limit_[0] / weight);
		}
	}
	if (mgr_) {
		mgr_->record_activity();
	}
}

size_t rate_limiter::limit(size_t direction)
{
	scoped_lock l(mtx_);
	return limit_[direction ? 1 : 0];
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

	for (size_t i = 0; i < 2; ++i) {

		size_t tokens;
		if (merged_tokens_[i] == unlimited) {
			tokens = unlimited;
		}
		else {
			tokens = merged_tokens_[i] / (bucket_weight * 2);
		}
		bucket->add_tokens(i, tokens, tokens);
		bucket->distribute_overflow(i, 0);

		if (tokens != unlimited) {
			debt_[i] += tokens * bucket_weight;
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

void rate_limiter::pay_debt(size_t direction)
{
	if (merged_tokens_[direction] != unlimited) {
		size_t weight = weight_ ? weight_ : 1;
		size_t debt_reduction = std::min(merged_tokens_[direction], debt_[direction] / weight);
		merged_tokens_[direction] -= debt_reduction;
		debt_[direction] -= debt_reduction;
	}
	else {
		debt_[direction] = 0;
	}
}

size_t rate_limiter::add_tokens(size_t direction, size_t tokens, size_t limit)
{
	if (!weight_) {
		merged_tokens_[direction] = std::min(limit_[direction], tokens);
		pay_debt(direction);
		return (tokens == unlimited) ? 0 : tokens;
	}

	size_t merged_limit = limit;
	if (limit_[direction] != unlimited) {
		size_t my_limit = (carry_[direction] + limit_[direction]) / weight_;
		carry_[direction] = (carry_[direction] + limit_[direction]) % weight_;
		if (my_limit < merged_limit) {
			merged_limit = my_limit;
		}
		carry_[direction] += (merged_limit % frequency) * weight_;
	}

	unused_capacity_[direction] = 0;

	if (merged_limit != unlimited) {
		merged_tokens_[direction] = merged_limit / frequency;
	}
	else {
		merged_tokens_[direction] = unlimited;
	}

	if (tokens < merged_tokens_[direction]) {
		merged_tokens_[direction] = tokens;
	}

	pay_debt(direction);

	if (limit_[direction] == unlimited) {
		unused_capacity_[direction] = unlimited;
	}
	else {
		if (merged_tokens_[direction] * weight_ * frequency < limit_[direction]) {
			unused_capacity_[direction] = limit_[direction] - merged_tokens_[direction] * weight_ * frequency;
			unused_capacity_[direction] /= frequency;
		}
		else {
			unused_capacity_[direction] = 0;
		}
	}

	overflow_[direction] = 0;
	scratch_buffer_.clear();
	for (size_t i = 0; i < buckets_.size(); ++i) {
		size_t overflow = buckets_[i]->add_tokens(direction, merged_tokens_[direction], merged_limit);
		if (overflow) {
			overflow_[direction] += overflow;
		}
		if (buckets_[i]->unsaturated(direction)) {
			scratch_buffer_.push_back(i);
		}
		else {
			overflow_[direction] += buckets_[i]->distribute_overflow(direction, 0);
		}
	}
	if (overflow_[direction] >= unused_capacity_[direction]) {
		unused_capacity_[direction] = 0;
	}
	else if (unused_capacity_[direction] != unlimited) {
		unused_capacity_[direction] -= overflow_[direction];
	}

	if (tokens == unlimited) {
		return 0;
	}
	else {
		return (tokens - merged_tokens_[direction]) * weight_;
	}
}


size_t rate_limiter::distribute_overflow(size_t direction, size_t overflow)
{
	size_t usable_external_overflow;
	if (unused_capacity_[direction] == unlimited) {
		usable_external_overflow = overflow;
	}
	else {
		usable_external_overflow = std::min(overflow, unused_capacity_[direction]);
	}
	size_t const overflow_sum = overflow_[direction] + usable_external_overflow;
	size_t remaining = overflow_sum;

	while (true) {
		size_t size{};
		for (auto idx : scratch_buffer_) {
			size += buckets_[idx]->unsaturated(direction);
		}
		unsaturated_[direction] = size;

		if (!remaining || scratch_buffer_.empty()) {
			break;
		}

		size_t const extra_tokens = remaining / size;
		remaining %= size;
		for (size_t i = 0; i < scratch_buffer_.size(); ) {
			size_t sub_overflow = buckets_[scratch_buffer_[i]]->distribute_overflow(direction, extra_tokens);
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
		unused_capacity_[direction] -= usable_external_overflow - remaining;
		overflow_[direction] = 0;
		return remaining + overflow - usable_external_overflow;
	}
	else {
		// Internal overflow not exhausted
		overflow_[direction] = remaining - usable_external_overflow;
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
		unsaturated_[0] += buckets_[i]->unsaturated(0);
		unsaturated_[1] += buckets_[i]->unsaturated(1);
	}
}


bucket::~bucket()
{
	remove_bucket();
}

size_t bucket::add_tokens(size_t direction, size_t tokens, size_t limit)
{
	if (limit == unlimited) {
		bucket_size_[direction] = unlimited;
		available_[direction] = unlimited;
		return 0;
	}
	else {
		bucket_size_[direction] = limit * overflow_multiplier_[direction]; // TODO: Tolerance
		if (available_[direction] == unlimited) {
			available_[direction] = tokens;
			return 0;
		}
		else if (bucket_size_[direction] < available_[direction]) {
			available_[direction] = bucket_size_[direction];
			return tokens;
		}
		else {
			size_t capacity = bucket_size_[direction] - available_[direction];
			if (capacity < tokens && unsaturated_[direction]) {
				unsaturated_[direction] = false;
				if (overflow_multiplier_[direction] < 1024*1024) {
					capacity += bucket_size_[direction];
					bucket_size_[direction] *= 2;
					overflow_multiplier_[direction] *= 2;
				}
			}
			size_t added = std::min(tokens, capacity);
			size_t ret = tokens - added;
			available_[direction] += added;
			return ret;
		}
	}
}

size_t bucket::distribute_overflow(size_t direction, size_t tokens)
{
	if (available_[direction] == unlimited) {
		return 0;
	}

	size_t capacity = bucket_size_[direction] - available_[direction];
	if (capacity < tokens && unsaturated_[direction]) {
		unsaturated_[direction] = false;
		if (overflow_multiplier_[direction] < 1024*1024) {
			capacity += bucket_size_[direction];
			bucket_size_[direction] *= 2;
			overflow_multiplier_[direction] *= 2;
		}
	}
	size_t added = std::min(tokens, capacity);
	size_t ret = tokens - added;
	available_[direction] += added;
	return ret;
}

void bucket::unlock_tree()
{
	for (size_t i = 0; i < 2; ++i) {
		if (waiting_[i] && available_[i]) {
			waiting_[i] = false;
			wakeup(i);
		}
	}
	bucket_base::unlock_tree();
}

void bucket::update_stats(bool & active)
{
	for (size_t i = 0; i < 2; ++i) {
		if (bucket_size_[i] == unlimited) {
			overflow_multiplier_[i] = 1;
		}
		else {
			if (available_[i] > bucket_size_[i] / 2 && overflow_multiplier_[i] > 1) {
				overflow_multiplier_[i] /= 2;
			}
			else {
				unsaturated_[i] = waiting_[i];
				if (waiting_[i]) {
					active = true;
				}
			}
		}
	}
}

size_t bucket::available(int direction)
{
	if (direction != 0) {
		direction = 1;
	}
	scoped_lock l(mtx_);
	if (!available_[direction]) {
		waiting_[direction] = true;
		if (mgr_) {
			mgr_->record_activity();
		}
	}
	return available_[direction];
}

void bucket::consume(int direction, size_t amount)
{
	if (!amount) {
		return;
	}
	if (direction != 0) {
		direction = 1;
	}
	scoped_lock l(mtx_);
	if (available_[direction] != unlimited) {
		if (mgr_) {
			mgr_->record_activity();
		}
		if (available_[direction] > amount) {
			available_[direction] -= amount;
		}
		else {
			available_[direction] = 0;
		}
	}
}

}
