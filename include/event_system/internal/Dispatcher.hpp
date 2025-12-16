#pragma once

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <vector>

#include "IDispatcher.hpp"

namespace NEventSystem::NInternal {

    /// Диспетчер для конкретного типа события EventType.
    template <typename TEvent>
    class TDispatcher: public TIDispatcher {
    public:
        using Callback = std::function<void(const TEvent&)>;

        struct TSlot {
            THandlerId Id;
            EPriority Priority;
            Callback CallbackV;
            bool IsOneShot;
            std::atomic<bool> Active;

            TSlot(THandlerId id_,
                  EPriority PriorityV,
                  Callback Cb,
                  bool OneShot)
                : Id(id_)
                , Priority(PriorityV)
                , CallbackV(std::move(Cb))
                , IsOneShot(OneShot)
                , Active(true) {
            }
        };

        TDispatcher() = default;
        ~TDispatcher() override = default;

        TDispatcher(const TDispatcher&) = delete;
        TDispatcher& operator=(const TDispatcher&) = delete;

        /// Подписка обработчика (внутренняя, вызывается из EventSystem).
        void Subscribe(THandlerId Id,
                       EPriority Priority,
                       Callback Callback,
                       bool OneShot) {
            auto slot = std::make_shared<TSlot>(Id, Priority, std::move(Callback), OneShot);

            std::unique_lock lock(Mutex);
            Slots.push_back(std::move(slot));

            std::stable_sort(
                Slots.begin(), Slots.end(),
                [](const std::shared_ptr<TSlot>& a,
                   const std::shared_ptr<TSlot>& b) {
                    return static_cast<int>(a->Priority) >
                           static_cast<int>(b->Priority);
                });
        }

        /// Логическое удаление обработчика по Id.
        bool Remove(THandlerId Id) override {
            std::unique_lock lock(Mutex);
            auto it = std::find_if(
                Slots.begin(), Slots.end(),
                [Id](const std::shared_ptr<TSlot>& slot) {
                    return slot->Id == Id;
                });

            if (it == Slots.end()) {
                return false;
            }

            (*it)->Active.store(false, std::memory_order_release);

            CleanupUnlocked();
            return true;
        }

        /// Обычная диспетчеризация события.
        /// Используется EventSystem::dispatch.
        void Dispatch(const TEvent& event) {
            std::vector<std::shared_ptr<TSlot>> snapshot;
            {
                std::shared_lock lock(Mutex);
                snapshot = Slots;
            }

            bool NeedCleanup = false;

            try {
                for (const auto& slot : snapshot) {
                    if (slot->IsOneShot) {
                        bool expected = true;
                        if (!slot->Active.compare_exchange_strong(
                                expected,
                                false,
                                std::memory_order_acq_rel,
                                std::memory_order_acquire)) {
                            continue;
                        }

                        NeedCleanup = true;
                        slot->CallbackV(event);
                        continue;
                    }

                    if (!slot->Active.load(std::memory_order_acquire)) {
                        continue;
                    }

                    slot->CallbackV(event);
                }
            } catch (...) {
                if (NeedCleanup) {
                    Cleanup();
                }
                throw;
            }

            if (NeedCleanup) {
                Cleanup();
            }
        }

        /// Вызвать ровно один обработчик по Id.
        /// Используется EventSystem при подписке во время dispatch,
        /// чтобы новый обработчик поучаствовал в текущей диспетчеризации.
        void InvokeSingle(THandlerId id, const TEvent& Event) {
            std::shared_ptr<TSlot> slot;

            {
                std::shared_lock lock(Mutex);
                auto it = std::find_if(
                    Slots.begin(), Slots.end(),
                    [id](const std::shared_ptr<TSlot>& s) {
                        return s->Id == id;
                    });

                if (it == Slots.end()) {
                    return;
                }

                slot = *it;
            }

            bool NeedCleanup = false;

            if (slot->IsOneShot) {
                bool Expected = true;
                if (!slot->Active.compare_exchange_strong(
                        Expected,
                        false,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire)) {
                    return;
                }
                NeedCleanup = true;
            } else {
                if (!slot->Active.load(std::memory_order_acquire)) {
                    return;
                }
            }

            try {
                slot->CallbackV(Event);
            } catch (...) {
                if (NeedCleanup) {
                    Cleanup();
                }
                throw;
            }

            if (NeedCleanup) {
                Cleanup();
            }
        }

        /// Количество активных обработчиков.
        std::size_t Count() const override {
            std::shared_lock lock(Mutex);
            return static_cast<std::size_t>(std::count_if(
                Slots.begin(), Slots.end(),
                [](const std::shared_ptr<TSlot>& slot) {
                    return slot->Active.load(std::memory_order_relaxed);
                }));
        }

    private:
        void Cleanup() {
            std::unique_lock lock(Mutex);
            CleanupUnlocked();
        }

        void CleanupUnlocked() {
            Slots.erase(
                std::remove_if(
                    Slots.begin(), Slots.end(),
                    [](const std::shared_ptr<TSlot>& slot) {
                        return !slot->Active.load(std::memory_order_relaxed);
                    }),
                Slots.end());
        }

        mutable std::shared_mutex Mutex;
        std::vector<std::shared_ptr<TSlot>> Slots;
    };

} // namespace NEventSystem::NInternal
