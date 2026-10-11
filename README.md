# Скачай

Менеджер загрузок в духе IDM для Linux и Windows 7+.

- файл режется на сегменты (до 16) и качается параллельно через HTTP Range
- пауза и докачка, в том числе после перезапуска программы
- очередь с лимитом одновременных загрузок
- имя файла из `Content-Disposition` или URL, дубликаты получают `(1)`, `(2)`
- трей, предложение скачать ссылку из буфера обмена
- ссылки можно передать из командной строки: `skachay https://example.com/file.iso`

Стек: C++14, Qt 5.15 (Widgets + Network). Qt 6 не поддерживает Windows 7, поэтому Qt 5.

## Сборка

Нужны Qt 5 dev-пакеты и CMake.

    # Debian/Ubuntu: sudo apt install build-essential cmake qtbase5-dev
    # Fedora/RHEL:   sudo dnf install gcc-c++ cmake qt5-qtbase-devel
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j
    ./build/skachay

Пакеты (на Linux, из папки build):

    cpack -G DEB    # .deb
    cpack -G RPM    # .rpm (нужен rpmbuild)

## Готовые сборки (GitHub Actions)

Workflow `.github/workflows/build.yml` собирает на каждый push:

| Артефакт | Что внутри |
|---|---|
| `skachay-deb` | `.deb` (Debian 11 и новее, Ubuntu 20.04+) |
| `skachay-rpm` | `.rpm` (RHEL/Rocky/Alma 8+, Fedora) |
| `skachay-windows` | zip с `skachay.exe`, библиотеками Qt и OpenSSL (Windows 7 SP1+, x64) |

Если выставить тег `v0.1.0`, файлы автоматически попадут в Releases.

## Идеи на потом

- расширение для браузера (перехват ссылок и видео, как у IDM)
- расписание и автовыключение
- ограничение скорости
- категории с раскладкой по папкам
- 32-битная сборка для Windows 7 x86
