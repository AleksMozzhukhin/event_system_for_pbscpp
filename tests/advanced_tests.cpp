#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "event_system/EventSystem.hpp"

using namespace NEventSystem;

namespace {

    class StartGate {
    public:
        explicit StartGate(int Total)
            : total_(Total) {
        }

        void ArriveAndWait() {
            ready_.fetch_add(1, std::memory_order_relaxed);

            std::unique_lock<std::mutex> Lk(m_);
            cv_.wait(Lk, [&] { return go_; });
        }

        void OpenWhenAllReady() {
            while (ready_.load(std::memory_order_relaxed) < total_) {
                std::this_thread::yield();
            }
            {
                std::lock_guard<std::mutex> Lk(m_);
                go_ = true;
            }
            cv_.notify_all();
        }

    private:
        const int total_;
        std::atomic<int> ready_{0};
        std::mutex m_;
        std::condition_variable cv_;
        bool go_ = false;
    };

} // namespace

struct TSimpleEvent {
    int Id;
};

// Тест на одноразовую подписку
TEST(EventSystemAdvanced, SubscribeOnce) {
    TEventSystem Sys;
    int Counter = 0;

    Sys.SubscribeOnce<TSimpleEvent>(
        TEventSystem::Priority::Normal,
        [&](const TSimpleEvent&) {
            ++Counter;
        });

    // Первый Dispatch: обработчик вызывается и отписывается
    Sys.Dispatch(TSimpleEvent{1});
    EXPECT_EQ(Counter, 1);
    EXPECT_EQ(Sys.GetHandlerCount<TSimpleEvent>(), 0u);

    // Второй Dispatch: обработчик уже не вызывается
    Sys.Dispatch(TSimpleEvent{2});
    EXPECT_EQ(Counter, 1);
}

// Тест: обработчик отписывает сам себя во время Dispatch
TEST(EventSystemAdvanced, SelfUnsubscribe) {
    TEventSystem Sys;
    TEventSystem::HandlerId Id = 0;
    bool Called = false;
    bool SecondCalled = false;

    Id = Sys.Subscribe<TSimpleEvent>(
        TEventSystem::Priority::Normal,
        [&](const TSimpleEvent&) {
            Called = true;
            Sys.Unsubscribe(Id); // отписка самого себя
        });

    // Второй обработчик остаётся подписанным
    Sys.Subscribe<TSimpleEvent>(
        TEventSystem::Priority::Low,
        [&](const TSimpleEvent&) {
            SecondCalled = true;
        });

    Sys.Dispatch(TSimpleEvent{1});

    EXPECT_TRUE(Called);
    EXPECT_TRUE(SecondCalled);
    EXPECT_EQ(Sys.GetHandlerCount<TSimpleEvent>(), 1u); // остался только второй обработчик
}

// Тест: обработчик отписывает другой обработчик во время Dispatch.
// Второй обработчик не должен быть вызван.
TEST(EventSystemAdvanced, UnsubscribeOtherHandlerInsideDispatch) {
    TEventSystem Sys;
    bool FirstCalled = false;
    bool SecondCalled = false;

    // Второй обработчик (Normal)
    TEventSystem::HandlerId SecondId =
        Sys.Subscribe<TSimpleEvent>(
            TEventSystem::Priority::Normal,
            [&](const TSimpleEvent&) {
                SecondCalled = true;
            });

    // Первый обработчик (High) отписывает второй
    Sys.Subscribe<TSimpleEvent>(
        TEventSystem::Priority::High,
        [&](const TSimpleEvent&) {
            FirstCalled = true;
            Sys.Unsubscribe(SecondId);
        });

    Sys.Dispatch(TSimpleEvent{0});

    EXPECT_TRUE(FirstCalled);
    EXPECT_FALSE(SecondCalled);
    EXPECT_EQ(Sys.GetHandlerCount<TSimpleEvent>(), 1u);
}

// Тест: рекурсивная диспетчеризация
TEST(EventSystemAdvanced, RecursiveDispatch) {
    TEventSystem Sys;
    int Depth = 0;
    constexpr int KMaxDepth = 3;

    Sys.Subscribe<TSimpleEvent>(
        TEventSystem::Priority::Normal,
        [&](const TSimpleEvent& e) {
            if (e.Id < KMaxDepth) {
                ++Depth;
                Sys.Dispatch(TSimpleEvent{e.Id + 1}); // рекурсивный Dispatch
            }
        });

    Sys.Dispatch(TSimpleEvent{0});

    // 0 -> 1 -> 2 -> 3 (stop), depth увеличился 3 раза
    EXPECT_EQ(Depth, KMaxDepth);
}

// Тест: RAII-обёртка TScopedConnection
TEST(EventSystemAdvanced, ScopedConnection) {
    TEventSystem Sys;
    int Count = 0;

    {
        auto Id = Sys.Subscribe<TSimpleEvent>(
            TEventSystem::Priority::Normal,
            [&](const TSimpleEvent&) {
                ++Count;
            });

        TEventSystem::TScopedConnection Conn(Sys, Id);

        Sys.Dispatch(TSimpleEvent{1});
        EXPECT_EQ(Count, 1);
        // При выходе из блока conn деструктор должен отписать обработчик
    }

    // Обработчик должен быть уже отписан
    Sys.Dispatch(TSimpleEvent{2});
    EXPECT_EQ(Count, 1);
}

// Тест: подписка нового обработчика во время Dispatch.
// Новый обработчик должен выполниться в этой же диспетчеризации.
TEST(EventSystemAdvanced, SubscribeHandlerDuringDispatch) {
    TEventSystem Sys;
    bool FirstCalled = false;
    bool SecondCalled = false;

    // Первый обработчик подписывает второй
    Sys.Subscribe<TSimpleEvent>(
        TEventSystem::Priority::High,
        [&](const TSimpleEvent&) {
            FirstCalled = true;

            Sys.Subscribe<TSimpleEvent>(
                TEventSystem::Priority::Low,
                [&](const TSimpleEvent&) {
                    SecondCalled = true;
                });
        });

    Sys.Dispatch(TSimpleEvent{0});

    EXPECT_TRUE(FirstCalled);
    EXPECT_TRUE(SecondCalled);
}

TEST(EventSystemAdvanced, OneShotIsTrulyOnceInMultithreading) {
    TEventSystem Sys;
    std::atomic<int> Counter{0};

    Sys.SubscribeOnce<TSimpleEvent>(
        TEventSystem::Priority::Normal,
        [&](const TSimpleEvent&) {
            Counter.fetch_add(1, std::memory_order_relaxed);
        });

    constexpr int kThreads = 8;

    std::atomic<int> Ready{0};
    std::mutex m;
    std::condition_variable CV;
    bool Go = false;

    auto worker = [&]() {
        Ready.fetch_add(1, std::memory_order_relaxed);

        std::unique_lock<std::mutex> Lk(m);
        CV.wait(Lk, [&]() { return Go; });
        Lk.unlock();

        Sys.Dispatch(TSimpleEvent{0});
    };

    std::vector<std::thread> Threads;
    Threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        Threads.emplace_back(worker);
    }

    while (Ready.load(std::memory_order_relaxed) < kThreads) {
        std::this_thread::yield();
    }

    {
        std::lock_guard<std::mutex> Lk(m);
        Go = true;
    }
    CV.notify_all();

    for (auto& t : Threads) {
        t.join();
    }

    EXPECT_EQ(Counter.load(std::memory_order_relaxed), 1);
}

TEST(EventSystemAdvanced, PriorityGroupsAreOrdered) {
    TEventSystem Sys;
    std::vector<std::string> Log;
    std::mutex Log_m;

    auto push = [&](std::string s) {
        std::lock_guard<std::mutex> lk(Log_m);
        Log.push_back(std::move(s));
    };

    Sys.Subscribe<TSimpleEvent>(TEventSystem::Priority::Low, [&](const auto&) { push("L1"); });
    Sys.Subscribe<TSimpleEvent>(TEventSystem::Priority::High, [&](const auto&) { push("H1"); });
    Sys.Subscribe<TSimpleEvent>(TEventSystem::Priority::Normal, [&](const auto&) { push("N1"); });
    Sys.Subscribe<TSimpleEvent>(TEventSystem::Priority::High, [&](const auto&) { push("H2"); });
    Sys.Subscribe<TSimpleEvent>(TEventSystem::Priority::Low, [&](const auto&) { push("L2"); });
    Sys.Subscribe<TSimpleEvent>(TEventSystem::Priority::Normal, [&](const auto&) { push("N2"); });

    Sys.Dispatch(TSimpleEvent{0});

    auto Pos = [&](const std::string& s) -> int {
        for (int i = 0; i < static_cast<int>(Log.size()); ++i) {
            if (Log[i] == s) {
                return i;
            }
        }
        return -1;
    };

    // Проверяем только межгрупповый порядок.
    const int h1 = Pos("H1"), h2 = Pos("H2");
    const int n1 = Pos("N1"), n2 = Pos("N2");
    const int l1 = Pos("L1"), l2 = Pos("L2");

    ASSERT_NE(h1, -1);
    ASSERT_NE(h2, -1);
    ASSERT_NE(n1, -1);
    ASSERT_NE(n2, -1);
    ASSERT_NE(l1, -1);
    ASSERT_NE(l2, -1);

    EXPECT_LT(std::max(h1, h2), std::min(n1, n2));
    EXPECT_LT(std::max(n1, n2), std::min(l1, l2));
}

TEST(EventSystemAdvanced, UnsubscribeOtherDuringDispatchSkipsItInSameDispatch) {
    TEventSystem Sys;
    std::vector<std::string> Log;

    auto Id2 = Sys.Subscribe<TSimpleEvent>(TEventSystem::Priority::Low, [&](const auto&) {
        Log.push_back("handler2");
    });

    Sys.Subscribe<TSimpleEvent>(TEventSystem::Priority::High, [&](const auto&) {
        Log.push_back("handler1");
        Sys.Unsubscribe<TSimpleEvent>(Id2);
    });

    Sys.Dispatch(TSimpleEvent{0});

    ASSERT_EQ(Log.size(), 1u);
    EXPECT_EQ(Log[0], "handler1");
}

TEST(EventSystemAdvanced, HandlerCanUnsubscribeItself) {
    TEventSystem Sys;
    std::atomic<int> Calls{0};

    TEventSystem::HandlerId Self = 0;
    Self = Sys.Subscribe<TSimpleEvent>(TEventSystem::Priority::Normal, [&](const auto&) {
        Calls.fetch_add(1, std::memory_order_relaxed);
        Sys.Unsubscribe<TSimpleEvent>(Self);
    });

    Sys.Dispatch(TSimpleEvent{0});
    Sys.Dispatch(TSimpleEvent{0});

    EXPECT_EQ(Calls.load(std::memory_order_relaxed), 1);
}

TEST(EventSystemAdvanced, SubscribeDuringDispatchRunsInSameDispatch) {
    TEventSystem Sys;
    std::vector<std::string> Log;

    Sys.Subscribe<TSimpleEvent>(TEventSystem::Priority::Normal, [&](const auto&) {
        Log.push_back("h1");
        Sys.Subscribe<TSimpleEvent>(TEventSystem::Priority::Normal, [&](const auto&) {
            Log.push_back("h2");
        });
    });

    Sys.Dispatch(TSimpleEvent{0});

    // Требуем только факт, что h2 успел выполниться.
    ASSERT_EQ(Log.size(), 2u);
    EXPECT_EQ(Log[0], "h1");
    EXPECT_EQ(Log[1], "h2");
}

struct TOtherEvent {
    int x;
};

TEST(EventSystemAdvanced, SubscribeToOuterEventDuringNestedDispatchRunsInSameOuterDispatch) {
    TEventSystem Sys;
    std::vector<std::string> Log;

    Sys.Subscribe<TOtherEvent>(TEventSystem::Priority::Normal, [&](const TOtherEvent&) {
        Sys.Subscribe<TSimpleEvent>(TEventSystem::Priority::Normal, [&](const TSimpleEvent&) {
            Log.push_back("new-simple");
        });
    });

    Sys.Subscribe<TSimpleEvent>(TEventSystem::Priority::Normal, [&](const TSimpleEvent&) {
        Log.push_back("outer-simple");
        Sys.Dispatch(TOtherEvent{1});
    });

    Sys.Dispatch(TSimpleEvent{0});

    ASSERT_EQ(Log.size(), 2u);
    EXPECT_EQ(Log[0], "outer-simple");
    EXPECT_EQ(Log[1], "new-simple");
}

TEST(EventSystemAdvanced, RecursiveDispatchOtherEventTypeWorks) {
    TEventSystem Sys;
    std::vector<std::string> Log;

    Sys.Subscribe<TOtherEvent>(TEventSystem::Priority::Normal, [&](const TOtherEvent& e) {
        Log.push_back("other:" + std::to_string(e.x));
    });

    Sys.Subscribe<TSimpleEvent>(TEventSystem::Priority::Normal, [&](const TSimpleEvent&) {
        Log.push_back("simple");
        Sys.Dispatch(TOtherEvent{42});
    });

    Sys.Dispatch(TSimpleEvent{0});

    ASSERT_EQ(Log.size(), 2u);
    EXPECT_EQ(Log[0], "simple");
    EXPECT_EQ(Log[1], "other:42");
}

TEST(EventSystemAdvanced, ReentrantDispatchSameEventTypeNoDeadlock) {
    TEventSystem Sys;
    std::atomic<int> Depth{0};

    Sys.Subscribe<TSimpleEvent>(TEventSystem::Priority::Normal, [&](const TSimpleEvent&) {
        int d = Depth.fetch_add(1, std::memory_order_relaxed);
        if (d < 3) {
            Sys.Dispatch(TSimpleEvent{0});
        }
    });

    Sys.Dispatch(TSimpleEvent{0});
    EXPECT_EQ(Depth.load(std::memory_order_relaxed), 4); // 0,1,2,3
}

TEST(EventSystemAdvanced, ExceptionDoesNotBreakSystemAndOneShotDoesNotRepeat) {
    TEventSystem Sys;
    std::atomic<int> OneshotCalls{0};
    std::atomic<int> NormalCalls{0};

    Sys.SubscribeOnce<TSimpleEvent>(TEventSystem::Priority::Normal, [&](const auto&) {
        OneshotCalls.fetch_add(1, std::memory_order_relaxed);
        throw std::runtime_error("boom");
    });

    Sys.Subscribe<TSimpleEvent>(TEventSystem::Priority::Low, [&](const auto&) {
        NormalCalls.fetch_add(1, std::memory_order_relaxed);
    });

    EXPECT_THROW(Sys.Dispatch(TSimpleEvent{0}), std::runtime_error);

    EXPECT_NO_THROW(Sys.Dispatch(TSimpleEvent{0}));
    EXPECT_EQ(OneshotCalls.load(std::memory_order_relaxed), 1);
    EXPECT_EQ(NormalCalls.load(std::memory_order_relaxed), 1);
}

TEST(EventSystemAdvanced, ScopedConnectionUnsubscribesOnDestruction) {
    TEventSystem Sys;
    std::atomic<int> Calls{0};

    {
        auto Id = Sys.Subscribe<TSimpleEvent>(TEventSystem::Priority::Normal, [&](const auto&) {
            Calls.fetch_add(1, std::memory_order_relaxed);
        });
        TEventSystem::TScopedConnection c(Sys, Id);

        Sys.Dispatch(TSimpleEvent{0});
        EXPECT_EQ(Calls.load(std::memory_order_relaxed), 1);
    }

    Sys.Dispatch(TSimpleEvent{0});
    EXPECT_EQ(Calls.load(std::memory_order_relaxed), 1);
}

TEST(EventSystemAdvanced, ConcurrentDispatchCallsAllForNormalHandler) {
    TEventSystem Sys;
    std::atomic<int> Calls{0};

    Sys.Subscribe<TSimpleEvent>(TEventSystem::Priority::Normal, [&](const auto&) {
        Calls.fetch_add(1, std::memory_order_relaxed);
    });

    constexpr int kThreads = 8;
    StartGate Gate(kThreads);

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&] {
            Gate.ArriveAndWait();
            Sys.Dispatch(TSimpleEvent{0});
        });
    }

    Gate.OpenWhenAllReady();
    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(Calls.load(std::memory_order_relaxed), kThreads);
}

TEST(EventSystemAdvanced, ConcurrentSubscribeUnsubscribeAndDispatchDoesNotCrash) {
    TEventSystem Sys;
    std::atomic<bool> Stop{false};
    std::atomic<int> Calls{0};

    std::thread Dispatcher([&] {
        while (!Stop.load(std::memory_order_relaxed)) {
            Sys.Dispatch(TSimpleEvent{0});
        }
    });

    constexpr int kWorkers = 4;
    StartGate gate(kWorkers);

    std::vector<std::thread> workers;
    workers.reserve(kWorkers);
    for (int i = 0; i < kWorkers; ++i) {
        workers.emplace_back([&] {
            gate.ArriveAndWait();
            for (int k = 0; k < 2000; ++k) {
                auto id = Sys.Subscribe<TSimpleEvent>(TEventSystem::Priority::Normal, [&](const auto&) {
                    Calls.fetch_add(1, std::memory_order_relaxed);
                });
                Sys.Unsubscribe<TSimpleEvent>(id);
            }
        });
    }

    gate.OpenWhenAllReady();
    for (auto& t : workers) {
        t.join();
    }

    Stop.store(true, std::memory_order_relaxed);
    Dispatcher.join();

    SUCCEED();
}
