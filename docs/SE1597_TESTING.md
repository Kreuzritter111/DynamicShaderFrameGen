# SE 1.5.97: native DLAA + experimental Neural Rendering

Это **тестовая ветка**, не подтверждённая рабочая замена PureDark DLAA.
Цель: Skyrim SE **1.5.97**, SKSE **2.0.20**, Address Library **для SE**,
RTX 5060 8 GB, 1920×1080, MO2 Portable, NAT.ENB III. Обновление до AE не требуется.

## Подтверждённые результаты аудита

Исходная точка: `879ab2c13404e02f352fa10515abd0fc25996d4f` (v1.52).
Установленная DLL пользователя от 11 сентября здесь отсутствует. Её точное
соответствие Git-коммиту и причина завершения процесса не установлены.

* В `5c0dcfb` из проекта удалены DLSS SR/DLAA, DRS и jitter hooks. На исходной
  точке `EnableUpscale` и `QualityMode` не запускали DLAA. Сообщение
  `Streamline DLSS IS available` означает наличие функции, не её выполнение.
* Размеры proxy по-прежнему вычислялись из QualityMode (1 = 2/3), даже без
  работающего upscaler. При 1080p это даёт лог 1280×720. Этот лог сам по себе
  не доказывает фактический размер kMAIN: старый DRS-код тоже удалён.
* `Provider=1` выбирал DLSSG и D3D12 Streamline даже при `FrameGeneration=0`.
  В ENB factory path FSR loader/context создавались без проверки FG.
  `fgActive=true` и сообщение `frame generation ENABLED` также не учитывали FG.
* `CreateD3D12Device` безусловно вызывал `slSetD3DDevice(D3D12)` после настройки
  D3D11 Streamline. Это меняло устройство сеанса, нужного для DLAA.
* Runtime NR менял IAT `GetModuleFileNameW` у NVIDIA DLL, подставляя путь
  `nvngx.dll` вместо настоящего пути вызывающего модуля. `proxyHits=1` в логе
  согласуется с этим механизмом. В этой ветке он **удалён**, а Authenticode
  проверяется до загрузки NR. Никаких патчей подписи/пути/лицензирования нет.
* Публичные NGX headers называют значение 18 `NVSDK_NGX_Feature_Reserved18`.
  Наличие экспортов в runtime не документирует ABI, параметры или право
  использования закрытого Feature 18. Существующий прямой NR-вызов сохранён
  только как явно включаемый эксперимент с неподменённым runtime.

* После ошибки NR старый код всё равно отправлял на GPU command list, в котором
  сторонний Create/Evaluate мог оставить частичные команды. Теперь весь список
  закрывается без исполнения и NR блокируется до перезапуска; это также касается
  отказа дополнительных NR layers. Механизм отправки подтверждён в коде, но его
  связь с конкретным вылетом пользователя без crash dump не доказана.

* NR больше не запускается в ранних Present до SKSE DataLoaded и наличия
  camera snapshot. ENB compilation не является готовым игровым кадром.
  Это устраняет преждевременный вызов, но не доказывает исправление самого NGX.

`0xBAD00002` = `NVSDK_NGX_Result_FAIL_PlatformError`. Это **не** код
`FeatureNotSupported` (`0xBAD00001`) и не `InvalidParameter` (`0xBAD00005`).
Официальный комментарий относит его к graphics API/OS/NvAPI и требует NGX logs
и validation layers. Успешный Init не подтверждает успешный CreateFeature.
Теперь состояние различает `initialized`, `feature-creation-failed` и
`feature-evaluation-failed`, поэтому первая ошибка не маскируется словом initialized.

Источники, проверенные по доступным официальным checkout:

* [NGX result codes и Reserved18 (Streamline 2.14.16)](https://github.com/NVIDIAGameWorks/Streamline/blob/625db07d4bca5abc67d781c42af7e7fb0761f1b6/external/ngx-sdk/include/nvsdk_ngx_defs.h)
* [Независимая копия публичных NGX headers](https://github.com/NVIDIA/DLSS/blob/9981c6350dc58c0ce21f024fd65e31e851c31689/include/nvsdk_ngx_defs.h)
* [Streamline DLSS integration guide](https://github.com/NVIDIAGameWorks/Streamline/blob/625db07d4bca5abc67d781c42af7e7fb0761f1b6/docs/ProgrammingGuideDLSS.md)

## Проверка экземпляра NR DLL на игровом ПК

Пользователь сообщил: драйвер **621.24**, RTX 5060 8 GB, DLL **310.8.0.0**
из Dynamic Shader FrameGen **v0.48.0.0 от 11.09.2026**, Nexus Mods **190154**.
Первоначальный источник NVIDIA неизвестен. На физическом файле из MO2
`Get-AuthenticodeSignature` вернул **HashMismatch**, хотя Subject сертификата
содержит `CN=NVIDIA Corporation`.

Это подтверждает несоответствие Authenticode digest подписанному содержимому.
Subject читается и у файла с недействительной подписью; он не отменяет HashMismatch.
Причиной могут быть изменение после подписания либо повреждение файла/архива.
Результат не доказывает, кто изменил файл, и не позволяет приписать вину Nexus
или автору без проверки исходного архива. Это также пока не доказывает, что
именно подпись вызвала старый NGX PlatformError: в старой DLL был отдельный
обход проверки пути вызывающего модуля.

**Этот экземпляр не должен быть виден игре в тестовом профиле, включая DLAA
без NR: другие компоненты могут обнаруживать runtime до нашего Probe.
Старый мод выключить только в отдельном MO2-профиле, файл оставить для сравнения.
Не отключать WinVerifyTrust и
не возвращать path proxy.** Новый wrapper отклоняет не прошедший проверку файл
до собственной LoadLibraryExW; для bad digest типичен код `0x80096010`
(`TRUST_E_BAD_DIGEST`). NGX Feature 18 с этим файлом не следует исследовать,
пока он не заменён проверенным артефактом из разрешённого источника.

Следующие проверки не загружают DLL и не меняют игру:

```powershell
# Выполнить для установленной копии и для свежей распаковки архива в отдельную
# временную папку. Указать реальные физические пути, не виртуальный Data MO2.
Get-FileHash -Algorithm SHA256 -LiteralPath 'полный путь к nvngx_dlssnr.dll'
Get-AuthenticodeSignature -LiteralPath 'полный путь к nvngx_dlssnr.dll' |
  Select-Object Status, StatusMessage,
    @{Name='Signer';Expression={$_.SignerCertificate.Subject}}
Get-Item -LiteralPath 'полный путь к nvngx_dlssnr.dll' |
  Select-Object Length,@{Name='Version';Expression={$_.VersionInfo.FileVersion}}
```

* Свежая копия Valid, установленная HashMismatch и другой SHA256: различие
  появилось между распаковкой и установленной копией; проверить копирование,
  локальные патчеры, антивирусный карантин и выигрывающий файл MO2.
* Обе копии имеют одинаковый SHA256 и HashMismatch: проблема воспроизводится
  в этом архиве. Повторная загрузка из разрешённого источника и проверка
  опубликованного hash архива (если есть) отличат повреждённую загрузку от
  такого же артефакта в распространяемом пакете.
* Только Valid с ожидаемым издателем NVIDIA допускает следующий этап. Он
  подтверждает целостность подписи, но ещё не поддержку private Feature 18,
  правильность ABI, право распространения или отсутствие runtime-инъекций.

Проверьте также подписи остальных NVIDIA runtime DLL из того же архива;
результат для NR не доказывает ни их повреждение, ни их целостность. Для DLAA
используйте согласованные официальные компоненты Streamline 2.14.16, которым
соответствуют закреплённые headers, с соблюдением лицензии поставщика.

Драйвер 621.24 выше общего минимума Streamline README (512.15), но публичный
SDK не определяет здесь отдельные требования DLSSNR. Делать вывод о поддержке
Feature 18 по одному номеру драйвера нельзя. Для окончательной связи с вылетом
нужны полный NGX log и Crash Logger stack/dump из запуска с доверенным runtime.

## Версионно-зависимые места

Наличие SE ID подтверждено в исходниках; соответствие реальным инструкциям
**не проверено** без Skyrim executable + Address Library на игровом компьютере.

| Назначение | SE ID / AE ID | Дополнительные предположения |
|---|---|---|
| Per-frame constant buffer | 524768 / 411384 | собственный `FrameBuffer`, memcpy layout, Map/Unmap capture и выбор камеры требуют runtime-проверки |
| Input dispatch call | 67315 / 68617 | +0x7B, legacy optional hook; в тестовом профиле выключен |
| MenuManager DrawInterfaceStart | 79947 / 82084 | Detours function head, конфликт с CS; в тестовом профиле выключен |
| FOV base | 513786 / 388785 | формула FOV + aspect ratio, чтение float |
| Camera near/far | 517032 / 403540 | фиксированные смещения +0x40/+0x44 |
| Delta time (FSR only) | 523660 / 410199 | FG=0 не исполняет FSR dispatch |
| Jitter call (восстановлен) | 75460 / 77245 | +0xE5 / +0xE2, проверка opcode E8 до установки обоих DLAA hooks |
| Post-processing call (восстановлен) | 100430 / 107148 | +0x1F0 / +0x1E7, та же проверка E8 |

Проверка E8 защищает от части конфликтов, но **не доказывает** правильную сигнатуру
функции или владение hook другим модом. Новые DLAA call sites взяты из истории
проекта `d0292a8`; для SE они пока кандидат на игровой тест, не доказанная поддержка.

CommonLibSSE-NG собран с SE+AE и без VR. Renderer/State/UI читаются через его
runtime accessors. `UsesAddressLibrary()` не устраняет фиксированные смещения
или риски собственного constant-buffer layout. Legacy `SKSEPlugin_Query`
сохранён для SKSE 2.0.20. Load отвергает версии кроме Steam 1.5.97/1.6.1170,
до установки адресных hooks; GOG/VR/будущие layouts не обещаны.

D3D IAT (`D3D11CreateDeviceAndSwapChain`) и COM vtable hooks (factory,
Map/Unmap/Present) зависят от Windows ABI, порядка загрузки ENB и других плагинов.
Они не являются адресами Skyrim, но тоже требуют проверки с конкретным ENB.

## Что изменено для качества и FG=0

Нативный D3D11 DLAA работает до оригинального post-processing call; ENB и UI
остаются ниже по существующей цепочке. Новый UAV output повторяет формат kMAIN,
а не формат swapchain. Color/depth/motion должны иметь размер output, иначе
обработка отклоняется с логом. Перед вызовом NGX изолируется состояние D3D11,
после него восстанавливается; копирование обратно выполняется лишь при успехе.

Для `EnableUpscale=1, FrameGeneration=0` включён только нативный режим:
QualityMode приводится к 0, RenderResolutionScale к 1.0 с предупреждением.
Halton jitter и DLAA token привязаны к engine frameCount. Custom jitter
включается после первого успешного resolve и выключается при его отказе.
Это не обещает качество, идентичное PureDark: модель, экспозиция, motion vectors,
history resets, ENB interaction и placement pass ещё требуют A/B.

FG=0 выбирает D3D11 DLSS даже если в старом INI Provider=1, не загружает
FSR loader/context, не включает DLSSG/Reflex/MFG и пишет правдивый лог.
Сам D3D12 proxy и копирование Present остаются: текущая архитектура использует
их для presentation и NR interop. «FG off» не означает нулевую стоимость proxy.

## Вероятные причины NR и границы вывода

Несоответствие private NR ABI/creation parameters или driver/NvAPI environment
возможны, но не доказаны. Унаследованный wrapper берёт application/API identity
из runtime, вызывает `Init_Ext` напрямую, ищет parameter allocator среди модулей
процесса и устанавливает строковые private параметры. Ни публичные headers,
ни успешный Init не доказывают корректность этой комбинации. Для дальнейшего
исправления нужен поддерживаемый NVIDIA NR SDK/runtime integration contract,
а не подбор неизвестных параметров или подмена identity.

Версия 310.8.x остаётся ограничением существующего wrapper, не официальным
заявлением NVIDIA о совместимости. Подпись DLL проверяется WinVerifyTrust
с chain revocation; ошибка доступа к revocation services также блокирует загрузку
и видна в логе. Официальный runtime не включён в ZIP. Прежде чем тестировать,
проверьте его источник и издателя NVIDIA через свойства Windows/Authenticode.

Нельзя вывести поддержку DLSS 5 на RTX 5060 из того, что DLSS SR доступен:
это разные функции. Также нельзя вывести несовместимость из одного PlatformError.
Проверяются версия драйвера, разрешённый NR runtime/SDK, минимальные требования
поставщика, реально доступная VRAM и собственный NGX log.

Техническая невозможность последовательных DLAA+NR **не установлена**. Код
размещает их в разных стадиях и устройствах, однако корректность передачи
цвета/depth/motion и совместимость SDK ещё не подтверждены. NR сейчас берёт цвет
в Present после ENB/UI; NRUICorrection=0 может менять HUD и эффекты NAT. Это
ограничение текущего NR placement, а не доказательство невозможности DLAA+NR.

Для вылета на `ENBSeries compiling shaders` нужны Crash Logger stack/dump,
полный SKSE/plugin/NGX log: последняя строка CreateFeature не устанавливает
место access violation. Здесь игра не запускалась, устранение этого вылета
не заявляется.

## Windows build и артефакт

Workflow `.github/workflows/windows.yml` реально конфигурирует и компилирует
DLL на `windows-2025`, затем запускает CTest и упаковывает результат. Нет
фиктивного DLL или пропуска build при ошибке. SHA зависимостей закреплены:

| Зависимость | Commit |
|---|---|
| CommonLibSSE-NG | `9955ae3b18dd0aedbebb58fa207a4b7180eb6d67` |
| Streamline | `625db07d4bca5abc67d781c42af7e7fb0761f1b6` |
| FidelityFX SDK | `7539ae8b6568ad29ed78a753404351daf3b5cca7` |
| vcpkg + baseline | `ee12231b20c95013c6638d845d04c91559a1d1ff` |

SDK headers из публичных репозиториев скачались здесь успешно. Никакие private
SDK, бинарники с неизвестными правами или секреты для source build не нужны.
Версии MSVC/Windows SDK/CMake/Ninja предоставляет образ runner и печатает build;
SHA зависимостей фиксированы, но runner image обновляется, поэтому build не
обещает побитовую воспроизводимость. Время первой сборки CommonLib может быть большим.

Локально на Windows расположите те же checkout в `.deps/` (CommonLib с openvr
submodule), выполните `.deps\vcpkg\bootstrap-vcpkg.bat -disableMetrics`, затем
`build_vs.bat`. Он находит VS через vswhere, ничего не развёртывает в игре.
Эквивалент в MSVC x64 Developer Shell:

```powershell
cmake --preset windows-release
cmake --build --preset windows-release --parallel 2
ctest --preset windows-release
cmake --install build/windows-release --prefix package
```

Пути можно переопределить через `COMMONLIB_DIR`, `STREAMLINE_DIR`, `FFX_DIR`
и `CMAKE_TOOLCHAIN_FILE` в локальном CMakeUserPresets.json. Путей дисков автора
в активных сборочных настройках больше нет. PLUGIN_DEPLOY_DIR остаётся явным
необязательным механизмом проекта, CI его не задаёт.

## Безопасный тест в MO2 Portable

1. Сделайте копию MO2-профиля, включите отдельные INI/savegames. Сохраните прежний
   мод PureDark и его конфигурацию для отката. Не меняйте основной профиль.
2. В тестовом профиле отключите Skyrim Upscaler и UpscalerBasePlugin, другие
   upscaler/FG/NGX injection плагины и старый DynamicShaderFrameGen v0.48,
   содержащий HashMismatch DLL. Не удаляйте файлы с диска. ENB NAT III оставьте.
   Повреждённый NR runtime не переносите в новую установку даже для DLAA baseline.
3. Скачайте Actions artifact и распакуйте его внешнюю ZIP-обёртку. Установите внутренний ZIP `DynamicShaderFrameGen-SE1597-test` из **успешного** Actions run
   как отдельный мод. Старый мод должен быть выключен в этом профиле; проверьте
   выигрывающий INI и возможное переопределение из Overwrite в MO2. Корень должен содержать `SKSE/Plugins` и `Shaders`, без
   дополнительного `Data/`. Сверьте COMMIT.txt и SHA256.txt.
4. Нужны уже установленные SKSE 2.0.20, SE Address Library и VC++ x64 runtime.
   Официальные NVIDIA runtime DLL приобретайте из разрешённого SDK/package
   отдельно, следуя его лицензии. Для D3D11 DLAA требуются согласованные
   `sl.interposer.dll`, `sl.common.dll`, `sl.dlss.dll`, `nvngx_dlss.dll` в
   `Data/Shaders/Upscaling/Streamline/`. Случайное смешивание версий не тестируется.
5. Игра: 1920×1080. Для теста именно DLAA выключите vanilla TAA/FXAA в INI
   тестового профиля (`bUseTAA=0`, `bFXAAEnabled=0`). Параметры NAT.ENB оставьте
   прежними и одинаковыми в обоих A/B-запусках. Не переносите изменения INI
   в основной профиль до сравнения.
6. Сначала штатный профиль из ZIP: FG=0, QualityMode=0, scale=1.0, NR=0,
   sharpness=0, без диагностических pixel-probe/overlay и legacy input/menu hooks.
   Ожидается `[DLAA] evaluated=true native=1920x1080` регулярно. Только
   `DLSS IS available` или `hooks installed` недостаточно. Если evaluated=false,
   качество не считать подтверждённым и вернуться на прежний профиль.
7. Сравните один save/камеру/освещение с PureDark DLAA: ограды, провода, волосы,
   растительность, медленная панорама, disocclusion, UI. Измерьте frametime и VRAM.
   Убедитесь, что нет DLSSG options, FSR FG contexts и искусственного роста FPS.
8. Только после DLAA baseline задайте `NeuralRenderingEnabled=1`. Остальное
   оставьте прежним, включая NRRenderScale=1.0, NRLayers=1. Не снижайте DLAA
   разрешение ради NR. Проверьте реальное создание Feature 18, успешные frames,
   лица/кожу/волосы и влияние на HUD/NAT. При отказе не подменяйте runtime/подпись:
   сохраните статус, полный NGX log, driver version и crash report (если есть).
9. Проверьте загрузку/новую игру, save/load, alt-tab, indoor/outdoor, меню,
   повторный запуск и закрытие. Откат — отключить тестовый мод и выбрать исходный
   MO2-профиль с PureDark; ничего в game root эти инструкции не патчат.

## Проверки, выполненные в Linux

Шесть существующих `tools/regress_*.py` прошли. Это исторические модели, а не
тест актуальной DLL. Новые три Python wiring/profile guards прошли; C++
`tests/render_policy.cpp` собран GCC с `-Wall -Wextra -Werror` и выполнился.
Он исполняет реальный RenderPolicy для DLAA/native, FG-off, провайдеров,
custom scale и NaN. Команды:

```sh
python3 -m unittest discover -s tests -v
g++ -std=c++17 -Wall -Wextra -Werror -Isrc tests/render_policy.cpp -o /tmp/render-policy
/tmp/render-policy
```

Полная Windows x64 компиляция **успешно выполнена** для source commit
`c617937393f1c2db00a61dfd460494a1169d7285`:
[Actions run 43395776301](https://github.com/Kreuzritter111/DynamicShaderFrameGen/actions/runs/43395776301).
На Windows также прошёл шаг Python-регрессий, build script завершил CTest без
ошибки. Проверка существования построенной DLL, установка в staging,
упаковка и upload artifact **DynamicShaderFrameGen-SE1597-test** прошли
успешно (687 KB). Внешняя обёртка GitHub artifact содержит внутренний MO2 ZIP.
Последующее дополнение аудита о HashMismatch изменяет только документацию,
а не исходники собранной DLL.
Linux-проверки **не подтверждают** компиляцию Windows API translation units,
правильность hook offsets, качество DLAA, совместимость ENB или работоспособность NR.
