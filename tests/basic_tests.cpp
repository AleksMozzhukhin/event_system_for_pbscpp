#include <gtest/gtest.h>
#include <vector>
#include <string>
#include "event_system/EventSystem.hpp"

using namespace event_system;

// --- Вспомогательные события ---
struct IntEvent {
    int value;
};

struct StringEvent {
    std::string text;
};

// --- Тесты ---

TEST(EventSystemBasic, SubscribeAndDispatch) {
    EventSystem sys;
    int acc = 0;

    sys.subscribe<IntEvent>(EventSystem::Priority::NORMAL, [&acc](const IntEvent& e) {
        acc += e.value;
    });

    sys.dispatch(IntEvent{10});
    sys.dispatch(IntEvent{20});

    EXPECT_EQ(acc, 30);
}

TEST(EventSystemBasic, Unsubscribe) {
    EventSystem sys;
    int call_count = 0;

    auto id = sys.subscribe<StringEvent>(EventSystem::Priority::NORMAL,
                                         [&call_count](const StringEvent&) {
                                             call_count++;
                                         });

    sys.dispatch(StringEvent{"Hello"});
    EXPECT_EQ(call_count, 1);

    sys.unsubscribe(id);
    sys.dispatch(StringEvent{"World"});
    EXPECT_EQ(call_count, 1); // Не должно увеличиться
}

TEST(EventSystemBasic, HandlerCount) {
    EventSystem sys;
    // Изначально 0
    EXPECT_EQ(sys.getHandlerCount<IntEvent>(), 0);

    auto id1 = sys.subscribe<IntEvent>(EventSystem::Priority::LOW, [](const auto&) {});
    auto id2 = sys.subscribe<IntEvent>(EventSystem::Priority::HIGH, [](const auto&) {});

    EXPECT_EQ(sys.getHandlerCount<IntEvent>(), 2);

    sys.unsubscribe(id1);
    EXPECT_EQ(sys.getHandlerCount<IntEvent>(), 1);

    sys.unsubscribe(id2);
    EXPECT_EQ(sys.getHandlerCount<IntEvent>(), 0);
}

TEST(EventSystemBasic, PriorityOrder) {
    EventSystem sys;
    std::vector<std::string> log;

    sys.subscribe<IntEvent>(EventSystem::Priority::LOW, [&log](const auto&) {
        log.emplace_back("LOW");
    });
    sys.subscribe<IntEvent>(EventSystem::Priority::HIGH, [&log](const auto&) {
        log.emplace_back("HIGH");
    });
    sys.subscribe<IntEvent>(EventSystem::Priority::NORMAL, [&log](const auto&) {
        log.emplace_back("NORMAL");
    });

    sys.dispatch(IntEvent{0});

    ASSERT_EQ(log.size(), 3);
    EXPECT_EQ(log[0], "HIGH");
    EXPECT_EQ(log[1], "NORMAL");
    EXPECT_EQ(log[2], "LOW");
}

TEST(EventSystemBasic, MultipleEventTypes) {
    EventSystem sys;
    bool int_called = false;
    bool str_called = false;

    sys.subscribe<IntEvent>(EventSystem::Priority::NORMAL, [&](const auto&) {
        int_called = true;
    });
    sys.subscribe<StringEvent>(EventSystem::Priority::NORMAL, [&](const auto&) {
        str_called = true;
    });

    sys.dispatch(IntEvent{1});
    EXPECT_TRUE(int_called);
    EXPECT_FALSE(str_called);

    int_called = false;
    sys.dispatch(StringEvent{"test"});
    EXPECT_FALSE(int_called);
    EXPECT_TRUE(str_called);
}