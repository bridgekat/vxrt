#ifndef UPDATESCHEDULER_H_
#define UPDATESCHEDULER_H_

class UpdateScheduler {
public:
	UpdateScheduler(int frequency): mInterval(1.0 / frequency) {
		init();
		sync();
	}

	void refresh() { mOnline = getTime(); }
	void sync() { mOnline = mOffline = getTime(); }
	bool inSync() { return mOffline + mInterval > mOnline; }
	void increase() { mOffline += mInterval; }
	// Interpolation factor
	double delta() { return (getTime() - mOffline) / mInterval; }
	
	static double timeFromEpoch() { return getTime(); }
	
private:
	double mInterval, mOnline, mOffline;

	static void init();
	static double getTime();
};

#endif

