// http://www.cplusplus.com/reference/future/future/

// http://www.cprogramming.com/c++11/c++11-lambda-closures.html

// future example
#include <iostream>       // std::cout
#include <future>         // std::async, std::future
#include <chrono>         // std::chrono::milliseconds


// a non-optimized way of checking for prime numbers:
bool is_prime (int x) {
  bool result = true;
  for (int i=2; i<x; ++i) {
    if (x%i==0) {
      result = false;
      break;
    }
  }
  return result;
}

int main ()
{
  int val = 444444443;
  
  // call function asynchronously:
  // std::future<bool> fut = std::async (is_prime, val);   // <---- did not work!
  // std::future<bool> fut = std::async (std::launch::async, [val]() { return is_prime(val); });  // <-- works
  std::future<bool> fut = std::async (std::launch::async, [&]() { return is_prime(val); });

  // do something while waiting for function to set future:
  std::cout << "checking, please wait \n";

  std::future_status futureStatus;
  int iterCount = 0;
  std::chrono::milliseconds span (1000);
  while ( (futureStatus = fut.wait_for(span)) == std::future_status::timeout) {
    // while ( (futureStatus = fut.wait_for(span)) != std::future_status::ready) {
    ++iterCount;
    std::cout << '.' << std::flush;
  }
  std::cout << "\nFuture status was " << (int) futureStatus << " on iteration " << iterCount << std::endl;

  const bool x = fut.get();     // retrieve return value

  std::cout << "\n" << val << (x?" is":" is not") << " prime.\n";

  return 0;
}
