#include <iostream>
#include "../pthread_wrapper.hpp"
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>

// MOBA game example using Condition Variables, only when all players are ready, the game starts.
std::atomic<int> ready_players = 0;
const int k_total_players = 10;

void Player(congzhi::ConditionVariable* cond_system, // system->player
            congzhi::ConditionVariable* cond_player, // player->system
            congzhi::Mutex* mutex) {

    std::this_thread::sleep_for(std::chrono::milliseconds(100 + rand() % 500));
    
    {
        congzhi::LockGuard<congzhi::Mutex> lock(*mutex);
        ready_players++;
        std::cout << "Player " << std::this_thread::get_id() 
                  << " ready (" << ready_players << "/" << k_total_players << ")" << std::endl;
    }
    
    cond_player->NotifyOne(); // one player notifies the system due to readiness
    
    congzhi::LockGuard<congzhi::Mutex> lock(*mutex); // wait for other players
    cond_system->Wait(*mutex, [](){ 
        return ready_players == k_total_players; 
    });
    
    std::cout << "Player " << std::this_thread::get_id() << " enters game!" << std::endl;
}

void System(congzhi::ConditionVariable* cond_system, // system->player
            congzhi::ConditionVariable* cond_player, // player->system
            congzhi::Mutex* mutex) {
    congzhi::LockGuard<congzhi::Mutex> lock(*mutex);
    std::cout << "System waiting for all players..." << std::endl;
    
    // 
    cond_player->Wait(*mutex, [](){ 
        return ready_players == k_total_players; 
    });
    
    std::cout << "\nAll players ready! Starting game..." << std::endl;
    cond_system->NotifyAll();
}

int main() {
    congzhi::Mutex mutex;
    congzhi::ConditionVariable cond_system;  // system->player
    congzhi::ConditionVariable cond_player;  // player->system

    std::vector<std::thread> threads;

    threads.emplace_back(System, &cond_system, &cond_player, &mutex);    
    
    for (int i = 0; i < k_total_players; ++i) {
        threads.emplace_back(Player, &cond_system, &cond_player, &mutex);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "\nGame started successfully!" << std::endl;
    return 0;
}
