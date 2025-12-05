# Event System

## Возможности

- Типобезопасные события (один тип — один диспетчер).
- Приоритеты обработчиков: `HIGH → NORMAL → LOW`.
- Подписка / отписка:
  - `subscribe`, `subscribeOnce`, `unsubscribe`.
  - `getHandlerCount<Event>()`.
- Работа во время `dispatch`:
  - подписка / отписка прямо из обработчиков;
  - рекурсивный `dispatch`.
- RAII-обёртка: `EventSystem::ScopedConnection`.

## Зависимости

- C++20-совместимый компилятор (GCC / Clang / MSVC).
- CMake ≥ 3.20.
- GoogleTest — для сборки тестов.
- (Опционально) `clang-format` + файл `.clang-format` для проверки стиля.

## Сборка

```bash
cmake -S . -B build
cmake --build build
````

## Тесты и стиль

Запуск всех тестов:

```bash
ctest --test-dir build
```

Если `clang-format` доступен, дополнительно будет запущен тест `clang_format`.

## Демонстрация

После сборки запустить исполняемый файл из `build/src`
(имя таргета см. в `src/CMakeLists.txt`), чтобы увидеть примеры использования
(EventSystem, приоритеты, ScopedConnection, subscribeOnce, простая многопоточность).

```
::contentReference[oaicite:0]{index=0}
```
