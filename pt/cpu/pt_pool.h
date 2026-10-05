// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Ferguson
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace pt {

// Runs "for i in [0, count)" across all cores; the caller takes part.
class Pool
{
public:
	Pool()
	{
		unsigned n = std::thread::hardware_concurrency();
		if (n < 1) n = 1;
		for (unsigned i = 1; i < n; i++)
			workers_.emplace_back([this] { Worker(); });
	}

	~Pool()
	{
		{
			std::lock_guard<std::mutex> lock(mutex_);
			quit_ = true;
			generation_++;
		}
		start_.notify_all();
		for (std::thread &t : workers_)
			t.join();
	}

	int Threads() const { return (int)workers_.size() + 1; }

	void Run(int count, const std::function<void(int)> &job)
	{
		if (count <= 0)
			return;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			job_ = &job;
			count_ = count;
			next_.store(0);
			busy_ = (int)workers_.size();
			generation_++;
		}
		start_.notify_all();

		Drain();

		std::unique_lock<std::mutex> lock(mutex_);
		done_.wait(lock, [this] { return busy_ == 0; });
		job_ = nullptr;
	}

private:
	void Drain()
	{
		for (;;)
		{
			const int i = next_.fetch_add(1);
			if (i >= count_)
				return;
			(*job_)(i);
		}
	}

	void Worker()
	{
		unsigned seen = 0;
		for (;;)
		{
			{
				std::unique_lock<std::mutex> lock(mutex_);
				start_.wait(lock, [&] { return generation_ != seen; });
				seen = generation_;
				if (quit_)
					return;
			}
			Drain();
			{
				std::lock_guard<std::mutex> lock(mutex_);
				busy_--;
			}
			done_.notify_one();
		}
	}

	std::vector<std::thread>	workers_;
	std::mutex					mutex_;
	std::condition_variable		start_, done_;
	const std::function<void(int)> *job_ = nullptr;
	std::atomic<int>			next_{0};
	int							count_ = 0;
	int							busy_ = 0;
	unsigned					generation_ = 0;
	bool						quit_ = false;
};

} // namespace pt
