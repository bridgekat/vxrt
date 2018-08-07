#include "updatescheduler.h"
#include "common.h"

#ifdef VXRT_TARGET_WINDOWS

#include <Windows.h>

double freq;

void UpdateScheduler::init() {
	LARGE_INTEGER res;
	QueryPerformanceFrequency(&res);
	freq = double(res.QuadPart);
}

double UpdateScheduler::getTime() {
	LARGE_INTEGER res;
	QueryPerformanceCounter(&res);
	return double(res.QuadPart) / freq;
}

#else

#include <chrono>

void UpdateScheduler::init() {}

double UpdateScheduler::getTime() {
	using namespace std::chrono;
	high_resolution_clock::time_point now = high_resolution_clock::now();
	return duration_cast<duration<double> >(now.time_since_epoch()).count();
}

#endif

