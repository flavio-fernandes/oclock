// notify_all_at_thread_exit
#include <iostream>           // std::cout
#include <thread>             // std::thread
#include <mutex>              // std::mutex, std::unique_lock
#include <condition_variable> // std::condition_variable

using namespace std;

mutex mtx;
condition_variable cv;
bool ready = false;

void print_id (int id) {
  unique_lock<mutex> lck(mtx);
  while (!ready) cv.wait(lck);
  // ...
  cout << "thread " << id << '\n';
}

void go() {
  unique_lock<mutex> lck(mtx);
#if 0
  // http://stackoverflow.com/questions/28220191/notify-all-at-thread-exit-doesnt-exist-in-cygwin-gcc/28222219
  notify_all_at_thread_exit(cv,move(lck));
#else
  cv.notify_all();
#endif
  ready = true;
}

int main ()
{
  thread threads[10];
  // spawn 10 threads:
  for (int i=0; i<10; ++i)
    threads[i] = thread(print_id,i);
  cout << "10 threads ready to race...\n";

  thread(go).detach();   // go!

  for (auto& th : threads) th.join();

  return 0;
}
