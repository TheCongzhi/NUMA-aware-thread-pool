#include <iostream>
#include "../pthread_wrapper.hpp"
class AnyLock {
public:
  void Lock() {
    std::cout << "Locked" << std::endl;
  }
  void Unlock() {
    std::cout << "Unlocked" << std::endl;
  }
};

int main() {
  AnyLock any_lock;
  // Using congzhi::LockGuard with AnyLock
  congzhi::LockGuard<AnyLock> lock_guard(any_lock);
  return 0;
}