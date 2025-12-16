#pragma once

#include <atomic>
#include <concepts>
#include <functional>
#include <memory>
#include <mutex>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "event_system/internal/IDispatcher.hpp"
#include "event_system/internal/Dispatcher.hpp"

namespace NEventSystem {

    template <typename T>
    concept EventConstraint =
        std::is_class_v<T> && std::move_constructible<T>;

    /// Основной класс системы событий.
    class TEventSystem {
    public:
        using HandlerId = NEventSystem::THandlerId;
        using Priority = NEventSystem::EPriority;

        /// RAII-обёртка для автоматической отписки обработчика.
        class TScopedConnection {
        public:
            TScopedConnection() = default;

            TScopedConnection(TEventSystem& system, HandlerId id)
                : System(&system)
                , Id(id) {
            }

            TScopedConnection(const TScopedConnection&) = delete;
            TScopedConnection& operator=(const TScopedConnection&) = delete;

            TScopedConnection(TScopedConnection&& other) noexcept
                : System(other.System)
                , Id(other.Id) {
                other.System = nullptr;
                other.Id = 0;
            }

            TScopedConnection& operator=(TScopedConnection&& other) noexcept {
                if (this != &other) {
                    Disconnect();
                    System = other.System;
                    Id = other.Id;
                    other.System = nullptr;
                    other.Id = 0;
                }
                return *this;
            }

            ~TScopedConnection() {
                Disconnect();
            }

            void Disconnect() {
                if (System && Id != 0) {
                    System->Unsubscribe(Id);
                    System = nullptr;
                    Id = 0;
                }
            }

        private:
            TEventSystem* System = nullptr;
            HandlerId Id = 0;
        };

        TEventSystem() = default;
        TEventSystem(const TEventSystem&) = delete;
        TEventSystem& operator=(const TEventSystem&) = delete;

        /// Подписка обработчика.
        template <EventConstraint TEvent>
        HandlerId Subscribe(Priority priority,
                            std::function<void(const TEvent&)> handler) {
            return SubscribeImpl<TEvent>(priority, std::move(handler), false);
        }

        template <EventConstraint TEvent>
        HandlerId SubscribeOnce(Priority priority,
                                std::function<void(const TEvent&)> handler) {
            return SubscribeImpl<TEvent>(priority, std::move(handler), true);
        }

        template <EventConstraint TEvent>
        HandlerId SubscribeImpl(Priority priority,
                                std::function<void(const TEvent&)> handler,
                                bool OneShot) {
            const HandlerId id =
                NextId.fetch_add(1, std::memory_order_relaxed);

            auto& dispatcher = GetDispatcher<TEvent>();
            dispatcher.Subscribe(id, priority, std::move(handler), OneShot);

            {
                std::lock_guard lock(Mutex);
                HandlerTypes.emplace(id, std::type_index(typeid(TEvent)));
            }

            NotifyCurrentDispatch<TEvent>(id);
            return id;
        }

        /// Отписка обработчика (как в ТЗ): с указанием типа события.
        template <EventConstraint TEvent>
        void Unsubscribe(HandlerId Id) {
            UnsubscribeImpl(Id);
        }

        /// Отписка обработчика по Id (тип события знать не нужно).
        void Unsubscribe(HandlerId Id) {
            UnsubscribeImpl(Id);
        }

        /// Диспетчеризация события.
        template <EventConstraint TEvent>
        void Dispatch(const TEvent& Event) {
            auto& Dispatcher = GetDispatcher<TEvent>();

            TDispatchFrame<TEvent> Frame(this, &Dispatcher, &Event);
            TDispatchFrameGuard Guard(&Frame);

            Dispatcher.Dispatch(Event);
        }

        /// Количество активных обработчиков для заданного типа события.
        template <typename TEvent>
        std::size_t GetHandlerCount() const {
            const auto type = std::type_index(typeid(TEvent));

            std::shared_ptr<NInternal::TIDispatcher> base;
            {
                std::lock_guard lock(Mutex);
                auto it = Dispatchers.find(type);
                if (it == Dispatchers.end()) {
                    return 0;
                }
                base = it->second;
            }

            return base->Count();
        }

    private:
        void UnsubscribeImpl(HandlerId id) {
            std::type_index type(typeid(void));

            {
                std::lock_guard lock(Mutex);
                auto it = HandlerTypes.find(id);
                if (it == HandlerTypes.end()) {
                    return;
                }
                type = it->second;
                HandlerTypes.erase(it);
            }

            std::shared_ptr<NInternal::TIDispatcher> base;
            {
                std::lock_guard lock(Mutex);
                auto dit = Dispatchers.find(type);
                if (dit == Dispatchers.end()) {
                    return;
                }
                base = dit->second;
            }

            base->Remove(id);
        }

        template <typename EventType>
        using TDispatcher = NInternal::TDispatcher<EventType>;

        template <typename EventType>
        TDispatcher<EventType>& GetDispatcher() {
            const auto type = std::type_index(typeid(EventType));

            std::lock_guard lock(Mutex);

            auto it = Dispatchers.find(type);
            if (it != Dispatchers.end()) {
                return *static_cast<TDispatcher<EventType>*>(it->second.get());
            }

            auto concrete = std::make_shared<TDispatcher<EventType>>();
            Dispatchers.emplace(type, concrete);
            return *concrete;
        }

        // --------- Стек контекстов диспетчеризации (thread_local) ---------

        struct TDispatchFrameBase {
            const TEventSystem* System;
            std::type_index EventTypeIndex;

            TDispatchFrameBase(const TEventSystem* Sys, std::type_index Type)
                : System(Sys)
                , EventTypeIndex(Type) {
            }
            virtual ~TDispatchFrameBase() = default;
            virtual void InvokeNewHandler(THandlerId Id) = 0;
        };

        template <typename EventType>
        struct TDispatchFrame: TDispatchFrameBase {
            TDispatcher<EventType>* Dispatcher;
            const EventType* Event;

            TDispatchFrame(const TEventSystem* Sys,
                           TDispatcher<EventType>* Disp,
                           const EventType* Ev)
                : TDispatchFrameBase(Sys, std::type_index(typeid(EventType)))
                , Dispatcher(Disp)
                , Event(Ev) {
            }

            void InvokeNewHandler(HandlerId id) override {
                Dispatcher->InvokeSingle(id, *Event);
            }
        };

        /// RAII-guard: гарантирует, что pushFrame/popFrame будут парными
        /// даже при исключениях в обработчиках.
        struct TDispatchFrameGuard {
            explicit TDispatchFrameGuard(TDispatchFrameBase* frame)
                : Frame(frame) {
                PushFrame(Frame);
            }

            ~TDispatchFrameGuard() {
                PopFrame();
            }

            TDispatchFrameGuard(const TDispatchFrameGuard&) = delete;
            TDispatchFrameGuard& operator=(const TDispatchFrameGuard&) = delete;

        private:
            TDispatchFrameBase* Frame;
        };

        inline static thread_local std::vector<TDispatchFrameBase*>
            DispatchStack;

        static void PushFrame(TDispatchFrameBase* frame) {
            DispatchStack.push_back(frame);
        }

        static void PopFrame() {
            DispatchStack.pop_back();
        }

        /// Если в текущем потоке идёт dispatch того же TEvent для
        /// этого же EventSystem, вызываем только что подписанный обработчик
        /// на "текущем" событии.
        template <typename EventType>
        void NotifyCurrentDispatch(THandlerId Id) {
            if (DispatchStack.empty()) {
                return;
            }

            const std::type_index Type(typeid(EventType));

            for (auto it = DispatchStack.rbegin(); it != DispatchStack.rend(); ++it) {
                auto* Frame = *it;
                if (Frame->System == this && Frame->EventTypeIndex == Type) {
                    Frame->InvokeNewHandler(Id);
                    break;
                }
            }
        }

        // --------- Данные ---------

        mutable std::mutex Mutex;

        std::unordered_map<std::type_index,
                           std::shared_ptr<NInternal::TIDispatcher>>
            Dispatchers;

        std::unordered_map<HandlerId, std::type_index> HandlerTypes;

        std::atomic<HandlerId> NextId{1};
    };

} // namespace NEventSystem
