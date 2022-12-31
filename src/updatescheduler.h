#ifndef UPDATESCHEDULER_H_
#define UPDATESCHEDULER_H_

class UpdateScheduler {
public:
  explicit UpdateScheduler(double frequency): mInterval(1.0 / frequency) {
    init();
    sync();
  }

  void refresh() { mOnline = getTime(); }
  void sync() { mOnline = mOffline = getTime(); }
  bool inSync() { return mOffline + mInterval > mOnline; }
  void increase() { mOffline += mInterval; }

  // Returns interpolation factor.
  double delta() { return (getTime() - mOffline) / mInterval; }

  static double timeFromEpoch() { return getTime(); }

private:
  double mInterval, mOnline, mOffline;

  static void init();
  static double getTime();
};

#endif // UPDATESCHEDULER_H_
