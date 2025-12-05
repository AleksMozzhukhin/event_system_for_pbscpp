#include <gtest/gtest.h>
#include "event_system/EventSystem.hpp"

using namespace event_system;

struct SimpleEvent {
    int id;
};

// Тест на одноразовую подписку
TEST(EventSystemAdvanced, SubscribeOnce) {
    EventSystem sys;
    int counter = 0;

    sys.subscribeOnce<SimpleEvent>(
        EventSystem::Priority::NORMAL,
        [&](const SimpleEvent&) {
            ++counter;
        });

    // Первый dispatch: обработчик вызывается и отписывается
    sys.dispatch(SimpleEvent{1});
    EXPECT_EQ(counter, 1);
    EXPECT_EQ(sys.getHandlerCount<SimpleEvent>(), 0u);

    // Второй dispatch: обработчик уже не вызывается
    sys.dispatch(SimpleEvent{2});
    EXPECT_EQ(counter, 1);
}

// Тест: обработчик отписывает сам себя во время dispatch
TEST(EventSystemAdvanced, SelfUnsubscribe) {
    EventSystem sys;
    EventSystem::HandlerId id = 0;
    bool called = false;
    bool second_called = false;

    id = sys.subscribe<SimpleEvent>(
        EventSystem::Priority::NORMAL,
        [&](const SimpleEvent&) {
            called = true;
            sys.unsubscribe(id); // отписка самого себя
        });

    // Второй обработчик остаётся подписанным
    sys.subscribe<SimpleEvent>(
        EventSystem::Priority::LOW,
        [&](const SimpleEvent&) {
            second_called = true;
        });

    sys.dispatch(SimpleEvent{1});

    EXPECT_TRUE(called);
    EXPECT_TRUE(second_called);
    EXPECT_EQ(sys.getHandlerCount<SimpleEvent>(), 1u); // остался только второй обработчик
}

// Тест: обработчик отписывает другой обработчик во время dispatch.
// Второй обработчик не должен быть вызван.
TEST(EventSystemAdvanced, UnsubscribeOtherHandlerInsideDispatch) {
    EventSystem sys;
    bool first_called = false;
    bool second_called = false;

    // Второй обработчик (NORMAL)
    EventSystem::HandlerId second_id =
        sys.subscribe<SimpleEvent>(
            EventSystem::Priority::NORMAL,
            [&](const SimpleEvent&) {
                second_called = true;
            });

    // Первый обработчик (HIGH) отписывает второй
    sys.subscribe<SimpleEvent>(
        EventSystem::Priority::HIGH,
        [&](const SimpleEvent&) {
            first_called = true;
            sys.unsubscribe(second_id);
        });

    sys.dispatch(SimpleEvent{0});

    EXPECT_TRUE(first_called);
    EXPECT_FALSE(second_called);
    EXPECT_EQ(sys.getHandlerCount<SimpleEvent>(), 1u);
}

// Тест: рекурсивная диспетчеризация
TEST(EventSystemAdvanced, RecursiveDispatch) {
    EventSystem sys;
    int depth = 0;
    constexpr int max_depth = 3;

    sys.subscribe<SimpleEvent>(
        EventSystem::Priority::NORMAL,
        [&](const SimpleEvent& e) {
            if (e.id < max_depth) {
                ++depth;
                sys.dispatch(SimpleEvent{e.id + 1}); // рекурсивный dispatch
            }
        });

    sys.dispatch(SimpleEvent{0});

    // 0 -> 1 -> 2 -> 3 (stop), depth увеличился 3 раза
    EXPECT_EQ(depth, max_depth);
}

// Тест: RAII-обёртка ScopedConnection
TEST(EventSystemAdvanced, ScopedConnection) {
    EventSystem sys;
    int count = 0;

    {
        auto id = sys.subscribe<SimpleEvent>(
            EventSystem::Priority::NORMAL,
            [&](const SimpleEvent&) {
                ++count;
            });

        EventSystem::ScopedConnection conn(sys, id);

        sys.dispatch(SimpleEvent{1});
        EXPECT_EQ(count, 1);
        // При выходе из блока conn деструктор должен отписать обработчик
    }

    // Обработчик должен быть уже отписан
    sys.dispatch(SimpleEvent{2});
    EXPECT_EQ(count, 1);
}

// Тест: подписка нового обработчика во время dispatch.
// Новый обработчик должен выполниться в этой же диспетчеризации.
TEST(EventSystemAdvanced, SubscribeHandlerDuringDispatch) {
    EventSystem sys;
    bool first_called = false;
    bool second_called = false;

    // Первый обработчик подписывает второй
    sys.subscribe<SimpleEvent>(
        EventSystem::Priority::HIGH,
        [&](const SimpleEvent&) {
            first_called = true;

            sys.subscribe<SimpleEvent>(
                EventSystem::Priority::LOW,
                [&](const SimpleEvent&) {
                    second_called = true;
                });
        });

    sys.dispatch(SimpleEvent{0});

    EXPECT_TRUE(first_called);
    EXPECT_TRUE(second_called);
}
