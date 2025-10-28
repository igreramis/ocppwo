#include <gtest/gtest.h>
#include <boost/asio.hpp>
#include <chrono>

using namespace std;

struct TimerPump {
  void post_after(std::chrono::milliseconds delay, std::function<void()> cb);
  void run_all();                 // run & clear everything
  template<class Pred> void run_while(Pred p); // optional
};

TEST(Harness, TimersRun) {
    TimerPump t;
    int x=0;
    t.post_after(10ms,[&]{x=1;});
    EXPECT_EQ(x,0);
    t.run_all();
}