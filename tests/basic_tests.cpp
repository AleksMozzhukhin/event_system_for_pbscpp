#include <gtest/gtest.h>
#include <vector>
#include <string>
#include "event_system/EventSystem.hpp"

using namespace NEventSystem;

// --- Вспомогательные события ---
struct TIntEvent {
    int Value;
};

struct TStringEvent {
    std::string Text;
};

// --- Тесты ---

TEST(EventSystemBasic, SubscribeAndDispatch) {
    TEventSystem Sys;
    int Acc = 0;

    Sys.Subscribe<TIntEvent>(TEventSystem::Priority::Normal, [&Acc](const TIntEvent& e) {
        Acc += e.Value;
    });

    Sys.Dispatch(TIntEvent{10});
    Sys.Dispatch(TIntEvent{20});

    EXPECT_EQ(Acc, 30);
}

TEST(EventSystemBasic, Unsubscribe) {
    TEventSystem Sys;
    int CallCount = 0;

    auto Id = Sys.Subscribe<TStringEvent>(TEventSystem::Priority::Normal,
                                          [&CallCount](const TStringEvent&) {
                                              CallCount++;
                                          });

    Sys.Dispatch(TStringEvent{"Hello"});
    EXPECT_EQ(CallCount, 1);

    Sys.Unsubscribe(Id);
    Sys.Dispatch(TStringEvent{"World"});
    EXPECT_EQ(CallCount, 1); // Не должно увеличиться
}

TEST(EventSystemBasic, HandlerCount) {
    TEventSystem Sys;
    // Изначально 0
    EXPECT_EQ(Sys.GetHandlerCount<TIntEvent>(), 0);

    auto Id1 = Sys.Subscribe<TIntEvent>(TEventSystem::Priority::Low, [](const auto&) {});
    auto Id2 = Sys.Subscribe<TIntEvent>(TEventSystem::Priority::High, [](const auto&) {});

    EXPECT_EQ(Sys.GetHandlerCount<TIntEvent>(), 2);

    Sys.Unsubscribe(Id1);
    EXPECT_EQ(Sys.GetHandlerCount<TIntEvent>(), 1);

    Sys.Unsubscribe(Id2);
    EXPECT_EQ(Sys.GetHandlerCount<TIntEvent>(), 0);
}

TEST(EventSystemBasic, PriorityOrder) {
    TEventSystem Sys;
    std::vector<std::string> Log;

    Sys.Subscribe<TIntEvent>(TEventSystem::Priority::Low, [&Log](const auto&) {
        Log.emplace_back("Low");
    });
    Sys.Subscribe<TIntEvent>(TEventSystem::Priority::High, [&Log](const auto&) {
        Log.emplace_back("High");
    });
    Sys.Subscribe<TIntEvent>(TEventSystem::Priority::Normal, [&Log](const auto&) {
        Log.emplace_back("Normal");
    });

    Sys.Dispatch(TIntEvent{0});

    ASSERT_EQ(Log.size(), 3);
    EXPECT_EQ(Log[0], "High");
    EXPECT_EQ(Log[1], "Normal");
    EXPECT_EQ(Log[2], "Low");
}

TEST(EventSystemBasic, MultipleEventTypes) {
    TEventSystem Sys;
    bool IntCalled = false;
    bool StrCalled = false;

    Sys.Subscribe<TIntEvent>(TEventSystem::Priority::Normal, [&](const auto&) {
        IntCalled = true;
    });
    Sys.Subscribe<TStringEvent>(TEventSystem::Priority::Normal, [&](const auto&) {
        StrCalled = true;
    });

    Sys.Dispatch(TIntEvent{1});
    EXPECT_TRUE(IntCalled);
    EXPECT_FALSE(StrCalled);

    IntCalled = false;
    Sys.Dispatch(TStringEvent{"test"});
    EXPECT_FALSE(IntCalled);
    EXPECT_TRUE(StrCalled);
}