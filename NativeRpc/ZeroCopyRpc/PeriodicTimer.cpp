#include "PeriodicTimer.h"

PeriodicTimer::PeriodicTimer(std::chrono::nanoseconds period): _period(period), _nxTick(std::chrono::steady_clock::now() + period)
{
}

bool PeriodicTimer::WaitForNext()
{
	auto now = std::chrono::steady_clock::now();
	auto target = _nxTick;
	_nxTick = target + _period;
	return WaitUntil(target);
}

PeriodicTimer PeriodicTimer::CreateFromFrequency(int Hz)
{
	if (Hz <= 0) {
		throw std::invalid_argument("Frequency must be greater than 0");
	}
	auto period = std::chrono::nanoseconds(1'000'000'000 / Hz); // Convert Hz to nanoseconds
	return PeriodicTimer(period);
}

bool PeriodicTimer::WaitUntil(std::chrono::steady_clock::time_point target)
{
	auto now = std::chrono::steady_clock::now();
	if (now >= target) return true;

	auto remaining = target - now;
	if (remaining > std::chrono::milliseconds(50)) {
		auto sleep_duration = remaining - std::chrono::milliseconds(50);
		std::this_thread::sleep_for(sleep_duration);
		remaining = target - std::chrono::steady_clock::now();
	}

	if (remaining > std::chrono::nanoseconds(0)) {
		_spin.WaitFor(remaining);
	}

	return true;
}
