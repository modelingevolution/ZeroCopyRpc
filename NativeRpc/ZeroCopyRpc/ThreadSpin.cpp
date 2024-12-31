#include "ThreadSpin.h"

ThreadSpin::ThreadSpin(): ns_per_cycle(Calibrate())
{
	
}
void ThreadSpin::Wait(int cycles)
{
    for (int i = 0; i < cycles; ++i) {
        _mm_pause();
    }

}
void ThreadSpin::OnWait(int cycles)
{
    auto start = std::chrono::steady_clock::now();

    ThreadSpin::Wait(cycles);

    if (cycles > 100) {
        auto elapsed = std::chrono::steady_clock::now() - start;
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
        auto new_ns_per_cycle = static_cast<double>(ns) / cycles;
        ns_per_cycle.store(new_ns_per_cycle);
    }
}

double ThreadSpin::Calibrate()
{
	const int CALIBRATION_CYCLES = 1000;
	auto start = std::chrono::high_resolution_clock::now();
	Wait(CALIBRATION_CYCLES);
	auto elapsed = std::chrono::high_resolution_clock::now() - start;

	auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
	return static_cast<double>(ns) / CALIBRATION_CYCLES;
}