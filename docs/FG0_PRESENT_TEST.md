# Следующий тест: FG=0, NR=0, D3D12 Present

Цель: живое изображение в главном меню Skyrim SE 1.5.97 с ENB NAT III,
затем загрузка сохранения. DLAA, NR и их обработка изображения в этом
изменении не переделывались. Использовать эту инструкцию для следующего теста.

## Что известно

В сборке c617937 Present повторяет `0x887A0001` (DXGI_ERROR_INVALID_CALL)
при SyncInterval=1 и Flags=0. Конфликт DXGI_PRESENT_ALLOW_TEARING этим
логом не подтверждён. Новый путь сохраняет эти аргументы; отключение VSync
или произвольная очистка флагов не используется как предполагаемое лечение.

В старом копировании найдены обе обратные ResourceBarrier в PRESENT/COMMON.
По самому исходнику нельзя заключить, что первопричина — отсутствующий
барьер. Полученный лог также не содержит фактический fullscreen state,
текущий индекс DXGI и validation messages. Первоначальная причина первого
неудачного Present в конкретном ENB-запуске ещё не установлена.

Подтверждённые дефекты старого пути:

* После ExecuteCommandLists исключение из Present пропускало queue Signal
  и обратный D3D11 Wait. При следующем вызове общий fence/counter уже не
  доказывал завершение команд использованного allocator.
* Первый D3D11 release использовал значение 1, равное начальному значению
  shared fence. Такое ожидание не доказывало готовность первой D3D11-записи;
  в новом plain path первый release строго больше начального значения.
* Старый guard после 50 ms таймаута всё равно вызывал allocator Reset.
* QueryInterface даже для IUnknown/IDXGISwapChain возвращал настоящий D3D12
  backend вместо D3D11-facing proxy. Это нарушало identity адаптера и позволяло
  обходить его Present/GetBuffer. В plainMode базовые interfaces теперь
  возвращают proxy; не реализованные им расширения отклоняются с E_NOINTERFACE.
* ResizeBuffers передавался напрямую, хотя плагин удерживал реальные
  backbuffer resources и записанные command lists. Согласно DXGI это мешает
  корректному resize. Для flip model требуется resize после смены exclusive
  fullscreen/windowed; его отсутствие может сделать следующий Present ошибочным.

Источник: [Microsoft ResizeBuffers remarks](https://learn.microsoft.com/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-resizebuffers).
Связь этих дефектов именно с первым сбоем пользователя требует нового игрового
лога; успешная WARP-проверка не устанавливает причину ENB-сбоя.

## Изменения только в обычном swapchain

При создании FG=0 фиксируется plainMode. Путь FSR3/DLSSG остаётся прежним.
В plainMode перед копированием берётся свежий GetCurrentBackBufferIndex.
Размер/формат source и destination проверяются, записывается полный цикл
COMMON/PRESENT -> COPY_SOURCE/COPY_DEST -> COMMON/PRESENT.

Каждый allocator хранит свой submission value отдельного D3D12-only fence.
Таймаут ожидания запрещает его Reset. Общий меж-API fence больше не служит
доказательством завершения allocator: его может сигналить и D3D11.

D3D11 Signal явно Flush-ится, поскольку обычный D3D11 Present заменён
proxy. D3D12 completion, обратный shared-fence Signal и D3D11 Wait
ставятся в очередь **до** DXGI Present и сохраняются при его отказе.
Ошибка возвращается вызывающему как исходный HRESULT, а не E_FAIL/S_OK.
Повторные сообщения ограничены первыми тремя и каждым 120-м отказом.

Present/resize сериализованы неблокирующей блокировкой: конкурирующий вызов
возвращает WAS_STILL_DRAWING, не меняя ресурсы. Resize ждёт завершения GPU,
освобождает старые backbuffer references/command recordings, получает новые
буферы и пересоздаёт presentation-owned shared textures под фактический размер.
Логический запрос legacy-приложения на один буфер преобразуется в два
физических flip buffers; ALLOW_TEARING при resize сохраняется согласно DXGI.
С ForceBorderless=1 proxy сохраняет windowed DXGI state при запросе exclusive.

При отказе `[PRESENT0]` печатает HRESULT и stage, sync/flags, used/current
buffer index, fullscreen state, фактические size/format/count/effect/flags,
submission/completion fence и GetDeviceRemovedReason. Если validation queues
доступны, выводятся последние D3D12/DXGI messages. Глобальный D3D12 debug layer
после создания чужих устройств не включается: это могло бы удалить их устройства.
Отсутствующая validation queue отмечается явно и не выдаётся за нулевое число ошибок.

## Автоматические проверки

Перед основной Windows-сборкой выполняется standalone `present_smoke` без
Skyrim/CommonLib. Он создаёт WARP D3D11/D3D12 устройства, реальный HWND flip
swapchain, shared texture/fence и использует тот же PlainPresent helper.
Меняющиеся цвета GPU копируются в backbuffer и проверяются CPU readback.
Проверяются Present(1,0), completion после внедрённого INVALID_CALL, следующий
кадр после отказа, resize с legacy count=1 и затем с тремя буферами.
Если доступен debug layer, D3D12 error/corruption messages приводят к провалу.
Отсутствие WARP/window presentation в runner помечается CTest как skipped,
а не как успешно проверенная презентация. Результат находится в VALIDATION-WARP.txt
в новом пакете. Это проверка API/копирования/синхронизации, не Skyrim/ENB.

## Установка и проверка на игровом ПК

1. В отдельном тестовом MO2-профиле замените предыдущую тестовую сборку новым
   artifact `DynamicShaderFrameGen-FG0-Present-<commit>`. Новый скачанный ZIP
   содержит SKSE/Plugins и Shaders прямо в корне: его можно устанавливать в MO2.
   COMMIT.txt фиксирует собранный commit, SHA256.txt — hash DLL.
2. Убедитесь, что этот INI выигрывает конфликты, включая Overwrite. Должны быть
   Enable=1, FrameGeneration=0, NeuralRenderingEnabled=0, QualityMode=0,
   RenderResolutionScale=1.0, ForceBorderless=1, PixelProbe=0. Не меняйте DLAA,
   NR или ENB-настройки для этого теста. Старый HashMismatch NR runtime по-прежнему
   не должен быть виден игре; нужен ранее подготовленный согласованный runtime.
3. Запустите игру, убедитесь, что главное меню обновляется и реагирует визуально.
   Загрузите сохранение, проверьте движение камеры, меню и обычное закрытие.
4. Ожидаются `[PRESENT0] created` с фактическим descriptor и `[PRESENT0] hr=0x00000000`
   с меняющимися used/next index и растущим completion. Эти счётчики сами по себе
   не доказывают живое изображение — нужна визуальная проверка на игровом ПК.
5. При повторном сбое сохраните первый полный блок `[PRESENT0]`, включая
   `[D3D12]`/`[DXGI]` или сообщение о недоступности validation queue, и соседние
   строки ResizeBuffers/fullscreen. Повторять сотни одинаковых строк не требуется.

Стабильность Skyrim/ENB объявляется только после игрового теста. Из Linux
и Windows CI наличие корректного изображения в вашем сохранении не проверяется.
