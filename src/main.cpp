#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "event_system/EventSystem.hpp"

using namespace NEventSystem;

// --- События ---

struct TPlayerLoginEvent {
    std::string Username;
    int PlayerId;
};

struct TPhysicsTickEvent {
    float DeltaTime;
};

struct KeyPressEvent {
    int KeyCode;
};

// --- Демонстрация ---

void demoPriorities(TEventSystem& Sys) {
    std::cout << "\n--- Demo 1: Priorities ---\n";

    // Подписываемся в разнобой, но ожидаем порядок High -> Normal -> Low
    Sys.Subscribe<KeyPressEvent>(TEventSystem::Priority::Low, [](const KeyPressEvent&) {
        std::cout << "[Low]    Handling Input (Logging)\n";
    });

    Sys.Subscribe<KeyPressEvent>(TEventSystem::Priority::High, [](const KeyPressEvent&) {
        std::cout << "[High]   Handling Input (Immediate Action)\n";
    });

    Sys.Subscribe<KeyPressEvent>(TEventSystem::Priority::Normal, [](const KeyPressEvent&) {
        std::cout << "[Normal] Handling Input (UI Update)\n";
    });

    std::cout << "Dispatching KeyPressEvent(Space)...\n";
    Sys.Dispatch(KeyPressEvent{32});
}

void demoRAII(TEventSystem& Sys) {
    std::cout << "\n--- Demo 2: RAII TScopedConnection ---\n";

    {
        std::cout << "Entering scope.\n";
        // Используем TScopedConnection для автоматической отписки
        TEventSystem::TScopedConnection conn(
            Sys,
            Sys.Subscribe<TPlayerLoginEvent>(
                TEventSystem::Priority::Normal,
                [](const auto& e) {
                    std::cout << "Player " << e.Username
                              << " logged in!\n";
                }));

        Sys.Dispatch(TPlayerLoginEvent{"Nagibator2000", 1});
        std::cout << "Leaving scope.\n";
    }
    // Здесь conn уничтожается и отписывает обработчик

    std::cout << "Dispatching again (Should be silent).\n";
    Sys.Dispatch(TPlayerLoginEvent{"NoobMaster69", 2});
}

void demoOneShot(TEventSystem& Sys) {
    std::cout << "\n--- Demo 3: One-Shot Handler ---\n";

    Sys.SubscribeOnce<TPhysicsTickEvent>(TEventSystem::Priority::Normal, [](const auto&) {
        std::cout << "This runs only ONCE (Initialization)\n";
    });

    std::cout << "Tick 1:\n";
    Sys.Dispatch(TPhysicsTickEvent{0.016f});

    std::cout << "Tick 2:\n";
    Sys.Dispatch(TPhysicsTickEvent{0.016f});
}

void demoMultithreading(TEventSystem& Sys) {
    std::cout << "\n--- Demo 4: Multithreading Stress Test ---\n";

    std::atomic<int> Counter{0};
    constexpr int kIterations = 1000;

    // Подписчик просто считает события
    auto CounterId = Sys.Subscribe<TPhysicsTickEvent>(
        TEventSystem::Priority::Normal,
        [&](const auto&) {
            ++Counter;
        });

    TEventSystem::TScopedConnection counter_conn(Sys, CounterId);

    std::cout << "Launching 4 threads sending " << kIterations << " events each...\n";

    std::vector<std::jthread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&Sys]() {
            for (int j = 0; j < kIterations; ++j) {
                Sys.Dispatch(TPhysicsTickEvent{0.1f});

                // Иногда добавляем случайную подписку/отписку, чтобы создать нагрузку на локи
                if (j % 100 == 0) {
                    Sys.SubscribeOnce<TPhysicsTickEvent>(TEventSystem::Priority::Low, [](const auto&) {});
                }
            }
        });
    }

    // jthread автоматически джойнится при выходе из скоупа
}

void demoOneShotRace(TEventSystem& Sys) {
    std::cout << "\n--- Demo 5: One-Shot Handler with data race ---\n";

    std::atomic<int> counter{0};

    Sys.SubscribeOnce<TPhysicsTickEvent>(TEventSystem::Priority::Normal, [&](const auto&) {
        ++counter;
    });

    std::vector<std::jthread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&Sys]() {
            Sys.Dispatch(TPhysicsTickEvent{0.1f});
        });
    }
}

int main() {
    TEventSystem Sys;

    demoPriorities(Sys);
    demoRAII(Sys);
    demoOneShot(Sys);
    demoMultithreading(Sys);
    demoOneShotRace(Sys);
    std::cout << "\nAll demos finished successfully!\n";
    return 0;
}