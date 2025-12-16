#pragma once

#include <cstddef>

namespace NEventSystem {

    /// Идентификатор обработчика события.
    using THandlerId = std::size_t;

    /// Приоритет выполнения обработчиков.
    enum class EPriority { Low,
                           Normal,
                           High };

    namespace NInternal {

        /// Базовый интерфейс диспетчера для конкретного типа события.
        /// Нужен для type-erasure в EventSystem.
        class TIDispatcher {
        public:
            virtual ~TIDispatcher() = default;

            /// Логическое удаление обработчика по Id.
            /// Возвращает true, если обработчик найден.
            virtual bool Remove(THandlerId Id) = 0;

            /// Количество активных обработчиков.
            [[nodiscard]] virtual std::size_t Count() const = 0;
        };

    } // namespace NInternal
} // namespace NEventSystem
