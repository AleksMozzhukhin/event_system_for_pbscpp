#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "event_system/EventSystem.hpp"

using namespace event_system;

// --- События ---

struct PlayerLoginEvent {
    std::string username;
    int player_id;
};

struct PhysicsTickEvent {
    float delta_time;
};

struct KeyPressEvent {
    int key_code;
};

// --- Демонстрация ---

void demoPriorities(EventSystem& sys) {
    std::cout << "\n--- Demo 1: Priorities ---\n";

    // Подписываемся в разнобой, но ожидаем порядок HIGH -> NORMAL -> LOW
    sys.subscribe<KeyPressEvent>(EventSystem::Priority::LOW, [](const KeyPressEvent&) {
        std::cout << "[LOW]    Handling Input (Logging)\n";
    });

    sys.subscribe<KeyPressEvent>(EventSystem::Priority::HIGH, [](const KeyPressEvent&) {
        std::cout << "[HIGH]   Handling Input (Immediate Action)\n";
    });

    sys.subscribe<KeyPressEvent>(EventSystem::Priority::NORMAL, [](const KeyPressEvent&) {
        std::cout << "[NORMAL] Handling Input (UI Update)\n";
    });

    std::cout << "Dispatching KeyPressEvent(Space)...\n";
    sys.dispatch(KeyPressEvent{32});
}

void demoRAII(EventSystem& sys) {
    std::cout << "\n--- Demo 2: RAII ScopedConnection ---\n";

    {
        std::cout << "Entering scope.\n";
        // Используем ScopedConnection для автоматической отписки
        EventSystem::ScopedConnection conn(
            sys,
            sys.subscribe<PlayerLoginEvent>(
                EventSystem::Priority::NORMAL,
                [](const auto& e) {
                    std::cout << "Player " << e.username
                              << " logged in!\n";
                }));

        sys.dispatch(PlayerLoginEvent{"Nagibator2000", 1});
        std::cout << "Leaving scope.\n";
    }
    // Здесь conn уничтожается и отписывает обработчик

    std::cout << "Dispatching again (Should be silent).\n";
    sys.dispatch(PlayerLoginEvent{"NoobMaster69", 2});
}

void demoOneShot(EventSystem& sys) {
    std::cout << "\n--- Demo 3: One-Shot Handler ---\n";

    sys.subscribeOnce<PhysicsTickEvent>(EventSystem::Priority::NORMAL, [](const auto&) {
        std::cout << "This runs only ONCE (Initialization)\n";
    });

    std::cout << "Tick 1:\n";
    sys.dispatch(PhysicsTickEvent{0.016f});

    std::cout << "Tick 2:\n";
    sys.dispatch(PhysicsTickEvent{0.016f});
}

void demoMultithreading(EventSystem& sys) {
    std::cout << "\n--- Demo 4: Multithreading Stress Test ---\n";

    std::atomic<int> counter{0};
    constexpr int kIterations = 1000;

    // Подписчик просто считает события
    sys.subscribe<PhysicsTickEvent>(EventSystem::Priority::NORMAL, [&](const auto&) {
        ++counter;
    });

    std::cout << "Launching 4 threads sending " << kIterations << " events each...\n";

    std::vector<std::jthread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&sys]() {
            for (int j = 0; j < kIterations; ++j) {
                sys.dispatch(PhysicsTickEvent{0.1f});

                // Иногда добавляем случайную подписку/отписку, чтобы создать нагрузку на локи
                if (j % 100 == 0) {
                    sys.subscribeOnce<PhysicsTickEvent>(EventSystem::Priority::LOW, [](const auto&) {});
                }
            }
        });
    }

    // jthread автоматически джойнится при выходе из скоупа
}

int main() {
    EventSystem sys;

    demoPriorities(sys);
    demoRAII(sys);
    demoOneShot(sys);
    demoMultithreading(sys);

    std::cout << "\nAll demos finished successfully!\n";
    return 0;
}