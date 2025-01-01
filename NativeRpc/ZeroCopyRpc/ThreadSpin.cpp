#include "ThreadSpin.h"


ThreadSpin::ThreadSpin(): ns_per_cycle(Calibrate())
{
	
}
#if defined(__aarch64__)
inline uint64_t get_cycles(void) {
    uint64_t cycles;
#if defined(__aarch64__)
    asm volatile("mrs %0, cntvct_el0" : "=r" (cycles));
#endif
    return cycles;
}
#endif

void ThreadSpin::Wait(uint64_t cycles)
{
#if defined(__aarch64__)
    uint64_t start = get_cycles();
    while (get_cycles() - start < cycles) {
        asm volatile("isb" ::: "memory");
    }
#else
    for (uint64_t i = 0; i < cycles; ++i) {
        // Use PAUSE to reduce power consumption on x86/x64
        // This intrinsic is available for MSVC and GCC with -msse2 flag
        _mm_pause();
    }
#endif

}
void ThreadSpin::OnWait(uint64_t cycles)
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
	auto start = std::chrono::steady_clock::now();
    ThreadSpin::Wait(CALIBRATION_CYCLES);
	auto elapsed = std::chrono::steady_clock::now() - start;

	auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
	return static_cast<double>(ns) / CALIBRATION_CYCLES;
}
