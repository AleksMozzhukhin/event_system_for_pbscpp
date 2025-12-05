#pragma once

#include <cstddef>

namespace event_system {

    /// Идентификатор обработчика события.
    using HandlerId = std::size_t;

    /// Приоритет выполнения обработчиков.
    enum class Priority { LOW,
                          NORMAL,
                          HIGH };

    namespace internal {

        /// Базовый интерфейс диспетчера для конкретного типа события.
        /// Нужен для type-erasure в EventSystem.
        class IDispatcher {
        public:
            virtual ~IDispatcher() = default;

            /// Логическое удаление обработчика по id.
            /// Возвращает true, если обработчик найден.
            virtual bool remove(HandlerId id) = 0;

            /// Количество активных обработчиков.
            virtual std::size_t count() const = 0;
        };

    } // namespace internal
} // namespace event_system
