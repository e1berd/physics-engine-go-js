# Physics Engine Go

Упрощенный графический физический движок на Go с:

- встроенным JS runtime на базе V8 через `rogchap.com/v8go`
- физическим миром с гравитацией, интеграцией скоростей и простыми сферическими коллизиями
- подсистемой времени и фиксированным шагом симуляции
- оконным Vulkan renderer через native SDL2 backend
- API, через которое JS-код управляет миром во время runtime

Это MVP-архитектура, показывающая основную идею:

- Go отвечает за ядро движка
- JS выступает как встраиваемый runtime-слой для игровой логики и управления сценой
- Go управляет сценой и runtime, а оконный Vulkan path вынесен в отдельный native backend

## Запуск

Требования:

- Go 1.26+
- установленный V8 для сборки `v8go`
- установленный Vulkan loader (`libvulkan.so` на Linux)
- установленный `SDL2`
- установленный `glslc`

Запуск демо:

```bash
go run ./cmd/demo
```

## Что делает демо

Демо:

- открывает окно
- инициализирует Vulkan backend
- создает мир с гравитацией
- регистрирует JS API в отдельном V8 context
- исполняет `examples/demo.js`
- дает JS возможность:
  - создавать тела
  - управлять гравитацией
  - создавать источники света
  - применять силу к телам
  - читать `deltaSeconds`, `elapsedSeconds`, `frame`
  - подписываться на `engine.onStart` и `engine.onUpdate`

## Структура

- `cmd/demo` - точка входа
- `engine` - главный цикл и оркестрация подсистем
- `physics` - физический мир
- `render` - Vulkan backend, SDL2 windowing и scene snapshot
- `script` - V8 runtime и JS bridge
- `examples/demo.js` - пример скрипта

## Дальнейшее развитие

Следующий логичный этап:

1. передавать transform/light буферы через uniform/storage buffers вместо push constants
2. добавить camera system, scene graph и материалы
3. добавить broadphase, shape system и material model
4. расширить JS API событиями ввода, ресурсами и управлением сценой
5. добавить resize/recreate swapchain и полноценный render graph
