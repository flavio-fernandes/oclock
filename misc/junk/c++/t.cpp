
// REF: http://www.cplusplus.com/reference/thread/thread/

#include <iostream>       // std::cout
#include <thread>         // std::thread

using namespace std;

void foo() 
{
  // do stuff...
  cout << "hi from foo" << "\n";
}

void bar(int x)
{
  // do stuff...
  cout << "hi from bar " << x << "\n";
}

int main() 
{
  thread first (foo);     // spawn new thread that calls foo()
  thread second (bar,123);  // spawn new thread that calls bar(0)

  cout << "main, foo and bar now execute concurrently...\n";

  // synchronize threads:
  first.join();                // pauses until first finishes
  second.join();               // pauses until second finishes

  cout << "foo and bar completed.\n";

  return 0;
}
