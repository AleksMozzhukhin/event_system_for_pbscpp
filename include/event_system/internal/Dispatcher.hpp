#pragma once

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <vector>

#include "IDispatcher.hpp"

namespace event_system::internal {

    /// Диспетчер для конкретного типа события EventType.
    template <typename EventType>
    class Dispatcher: public IDispatcher {
    public:
        using Callback = std::function<void(const EventType&)>;

        struct Slot {
            HandlerId id;
            Priority priority;
            Callback callback;
            bool is_one_shot;
            std::atomic<bool> active;

            Slot(HandlerId id_,
                 Priority priority_,
                 Callback cb,
                 bool one_shot)
                : id(id_)
                , priority(priority_)
                , callback(std::move(cb))
                , is_one_shot(one_shot)
                , active(true) {
            }
        };

        Dispatcher() = default;
        ~Dispatcher() override = default;

        Dispatcher(const Dispatcher&) = delete;
        Dispatcher& operator=(const Dispatcher&) = delete;

        /// Подписка обработчика (внутренняя, вызывается из EventSystem).
        void subscribe(HandlerId id,
                       Priority priority,
                       Callback callback,
                       bool one_shot) {
            auto slot = std::make_shared<Slot>(id, priority, std::move(callback), one_shot);

            std::unique_lock lock(mutex_);
            slots_.push_back(std::move(slot));

            // Стабильная сортировка по убыванию приоритета:
            // HIGH → NORMAL → LOW.
            std::stable_sort(
                slots_.begin(), slots_.end(),
                [](const std::shared_ptr<Slot>& a,
                   const std::shared_ptr<Slot>& b) {
                    return static_cast<int>(a->priority) >
                           static_cast<int>(b->priority);
                });
        }

        /// Логическое удаление обработчика по id.
        bool remove(HandlerId id) override {
            std::unique_lock lock(mutex_);
            auto it = std::find_if(
                slots_.begin(), slots_.end(),
                [id](const std::shared_ptr<Slot>& slot) {
                    return slot->id == id;
                });

            if (it == slots_.end()) {
                return false;
            }

            (*it)->active.store(false, std::memory_order_relaxed);
            cleanupUnlocked();
            return true;
        }

        /// Обычная диспетчеризация события.
        /// Используется EventSystem::dispatch.
        void dispatch(const EventType& event) {
            std::vector<std::shared_ptr<Slot>> snapshot;
            {
                std::shared_lock lock(mutex_);
                snapshot = slots_;
            }

            bool need_cleanup = false;

            for (const auto& slot : snapshot) {
                if (!slot->active.load(std::memory_order_relaxed)) {
                    continue;
                }

                slot->callback(event);

                if (slot->is_one_shot) {
                    slot->active.store(false, std::memory_order_relaxed);
                    need_cleanup = true;
                }
            }

            if (need_cleanup) {
                cleanup();
            }
        }

        /// Вызвать ровно один обработчик по id.
        /// Используется EventSystem при подписке во время dispatch,
        /// чтобы новый обработчик поучаствовал в текущей диспетчеризации.
        void invokeSingle(HandlerId id, const EventType& event) {
            std::shared_ptr<Slot> slot;

            {
                std::shared_lock lock(mutex_);
                auto it = std::find_if(
                    slots_.begin(), slots_.end(),
                    [id](const std::shared_ptr<Slot>& s) {
                        return s->id == id;
                    });

                if (it == slots_.end()) {
                    return;
                }

                slot = *it;
            }

            if (!slot->active.load(std::memory_order_relaxed)) {
                return;
            }

            slot->callback(event);

            if (slot->is_one_shot) {
                slot->active.store(false, std::memory_order_relaxed);
                cleanup();
            }
        }

        /// Количество активных обработчиков.
        std::size_t count() const override {
            std::shared_lock lock(mutex_);
            return static_cast<std::size_t>(std::count_if(
                slots_.begin(), slots_.end(),
                [](const std::shared_ptr<Slot>& slot) {
                    return slot->active.load(std::memory_order_relaxed);
                }));
        }

    private:
        void cleanup() {
            std::unique_lock lock(mutex_);
            cleanupUnlocked();
        }

        void cleanupUnlocked() {
            slots_.erase(
                std::remove_if(
                    slots_.begin(), slots_.end(),
                    [](const std::shared_ptr<Slot>& slot) {
                        return !slot->active.load(std::memory_order_relaxed);
                    }),
                slots_.end());
        }

        mutable std::shared_mutex mutex_;
        std::vector<std::shared_ptr<Slot>> slots_;
    };

} // namespace event_system::internal
