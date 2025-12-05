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

namespace event_system {

    template <typename T>
    concept EventConstraint =
        std::is_class_v<T> && std::move_constructible<T>;

    /// Основной класс системы событий.
    class EventSystem {
    public:
        using HandlerId = event_system::HandlerId;
        using Priority = event_system::Priority;

        /// RAII-обёртка для автоматической отписки обработчика.
        class ScopedConnection {
        public:
            ScopedConnection() = default;

            ScopedConnection(EventSystem& system, HandlerId id)
                : system_(&system)
                , id_(id) {
            }

            ScopedConnection(const ScopedConnection&) = delete;
            ScopedConnection& operator=(const ScopedConnection&) = delete;

            ScopedConnection(ScopedConnection&& other) noexcept
                : system_(other.system_)
                , id_(other.id_) {
                other.system_ = nullptr;
                other.id_ = 0;
            }

            ScopedConnection& operator=(ScopedConnection&& other) noexcept {
                if (this != &other) {
                    disconnect();
                    system_ = other.system_;
                    id_ = other.id_;
                    other.system_ = nullptr;
                    other.id_ = 0;
                }
                return *this;
            }

            ~ScopedConnection() {
                disconnect();
            }

            void disconnect() {
                if (system_ && id_ != 0) {
                    system_->unsubscribe(id_);
                    system_ = nullptr;
                    id_ = 0;
                }
            }

        private:
            EventSystem* system_ = nullptr;
            HandlerId id_ = 0;
        };

        EventSystem() = default;
        EventSystem(const EventSystem&) = delete;
        EventSystem& operator=(const EventSystem&) = delete;

        /// Подписка обработчика.
        template <EventConstraint EventType>
        HandlerId subscribe(Priority priority,
                            std::function<void(const EventType&)> handler) {
            const HandlerId id =
                next_id_.fetch_add(1, std::memory_order_relaxed);

            auto& dispatcher = getDispatcher<EventType>();
            dispatcher.subscribe(id, priority, std::move(handler), false);

            {
                std::lock_guard lock(mutex_);
                handler_types_.emplace(id,
                                       std::type_index(typeid(EventType)));
            }

            // Если подписка происходит во время dispatch этого же события,
            // сразу вызываем обработчик на текущем событии.
            notifyCurrentDispatch<EventType>(id);

            return id;
        }

        /// Подписка одноразового обработчика.
        template <EventConstraint EventType>
        HandlerId subscribeOnce(Priority priority,
                                std::function<void(const EventType&)> handler) {
            const HandlerId id =
                next_id_.fetch_add(1, std::memory_order_relaxed);

            auto& dispatcher = getDispatcher<EventType>();
            dispatcher.subscribe(id, priority, std::move(handler), true);

            {
                std::lock_guard lock(mutex_);
                handler_types_.emplace(id,
                                       std::type_index(typeid(EventType)));
            }

            // Аналогично: если идёт текущий dispatch, новый обработчик
            // должен выполниться на текущем событии.
            notifyCurrentDispatch<EventType>(id);

            return id;
        }

        /// Отписка обработчика по id (тип события знать не нужно).
        void unsubscribe(HandlerId id) {
            std::type_index type(typeid(void));

            {
                std::lock_guard lock(mutex_);
                auto it = handler_types_.find(id);
                if (it == handler_types_.end()) {
                    return;
                }
                type = it->second;
                handler_types_.erase(it);
            }

            std::shared_ptr<internal::IDispatcher> base;
            {
                std::lock_guard lock(mutex_);
                auto dit = dispatchers_.find(type);
                if (dit == dispatchers_.end()) {
                    return;
                }
                base = dit->second;
            }

            base->remove(id);
        }

        /// Диспетчеризация события.
        template <EventConstraint EventType>
        void dispatch(const EventType& event) {
            auto& dispatcher = getDispatcher<EventType>();

            DispatchFrame<EventType> frame(this, &dispatcher, &event);
            DispatchFrameGuard guard(&frame);

            dispatcher.dispatch(event);
        }

        /// Количество активных обработчиков для заданного типа события.
        template <typename EventType>
        std::size_t getHandlerCount() const {
            const auto type = std::type_index(typeid(EventType));

            std::shared_ptr<internal::IDispatcher> base;
            {
                std::lock_guard lock(mutex_);
                auto it = dispatchers_.find(type);
                if (it == dispatchers_.end()) {
                    return 0;
                }
                base = it->second;
            }

            return base->count();
        }

    private:
        template <typename EventType>
        using DispatcherT = internal::Dispatcher<EventType>;

        template <typename EventType>
        DispatcherT<EventType>& getDispatcher() const {
            const auto type = std::type_index(typeid(EventType));

            {
                std::lock_guard lock(mutex_);
                auto it = dispatchers_.find(type);
                if (it != dispatchers_.end()) {
                    return *static_cast<DispatcherT<EventType>*>(
                        it->second.get());
                }

                auto concrete =
                    std::make_shared<DispatcherT<EventType>>();
                dispatchers_.emplace(type, concrete);
                return *concrete;
            }
        }

        // --------- Стек контекстов диспетчеризации (thread_local) ---------

        struct DispatchFrameBase {
            const EventSystem* system;
            std::type_index event_type;

            DispatchFrameBase(const EventSystem* sys,
                              std::type_index type)
                : system(sys)
                , event_type(type) {
            }

            virtual ~DispatchFrameBase() = default;

            virtual void invokeNewHandler(HandlerId id) = 0;
        };

        template <typename EventType>
        struct DispatchFrame: DispatchFrameBase {
            DispatcherT<EventType>* dispatcher;
            const EventType* event;

            DispatchFrame(const EventSystem* sys,
                          DispatcherT<EventType>* disp,
                          const EventType* ev)
                : DispatchFrameBase(sys,
                                    std::type_index(typeid(EventType)))
                , dispatcher(disp)
                , event(ev) {
            }

            void invokeNewHandler(HandlerId id) override {
                dispatcher->invokeSingle(id, *event);
            }
        };

        /// RAII-guard: гарантирует, что pushFrame/popFrame будут парными
        /// даже при исключениях в обработчиках.
        struct DispatchFrameGuard {
            explicit DispatchFrameGuard(DispatchFrameBase* frame)
                : frame_(frame) {
                pushFrame(frame_);
            }

            ~DispatchFrameGuard() {
                popFrame();
            }

            DispatchFrameGuard(const DispatchFrameGuard&) = delete;
            DispatchFrameGuard& operator=(const DispatchFrameGuard&) = delete;

        private:
            DispatchFrameBase* frame_;
        };

        inline static thread_local std::vector<DispatchFrameBase*>
            dispatch_stack_;

        static void pushFrame(DispatchFrameBase* frame) {
            dispatch_stack_.push_back(frame);
        }

        static void popFrame() {
            dispatch_stack_.pop_back();
        }

        /// Если в текущем потоке идёт dispatch того же EventType для
        /// этого же EventSystem, вызываем только что подписанный обработчик
        /// на "текущем" событии.
        template <typename EventType>
        void notifyCurrentDispatch(HandlerId id) {
            if (dispatch_stack_.empty()) {
                return;
            }

            const auto type = std::type_index(typeid(EventType));

            for (auto it = dispatch_stack_.rbegin();
                 it != dispatch_stack_.rend();
                 ++it) {
                DispatchFrameBase* frame = *it;
                if (frame->system == this && frame->event_type == type) {
                    frame->invokeNewHandler(id);
                    break;
                }
            }
        }

        // --------- Данные ---------

        mutable std::mutex mutex_;

        mutable std::unordered_map<std::type_index,
                                   std::shared_ptr<internal::IDispatcher>>
            dispatchers_;

        mutable std::unordered_map<HandlerId, std::type_index>
            handler_types_;

        mutable std::atomic<HandlerId> next_id_{1};
    };

} // namespace event_system
