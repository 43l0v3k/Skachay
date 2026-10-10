cmake_minimum_required(VERSION 3.16)
project(skachay VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt 5.15 — последняя ветка Qt с поддержкой Windows 7
find_package(Qt5 5.12 REQUIRED COMPONENTS Widgets Network)

add_executable(skachay WIN32
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/dialogs.cpp
    src/dialogs.h
    src/downloaditem.cpp
    src/downloaditem.h
    src/downloadmanager.cpp
    src/downloadmanager.h
    src/util.h
)
target_compile_definitions(skachay PRIVATE SKACHAY_VERSION="${PROJECT_VERSION}")
target_link_libraries(skachay PRIVATE Qt5::Widgets Qt5::Network)

# ---------------------------------------------------------------- install (Linux)
include(GNUInstallDirs)
install(TARGETS skachay RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(UNIX AND NOT APPLE)
    install(FILES packaging/skachay.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/skachay.svg
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()

# ---------------------------------------------------------------- CPack (.deb / .rpm)
set(CPACK_PACKAGE_NAME "skachay")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "skachay@example.com")
set(CPACK_PACKAGE_SUMMARY "Менеджер загрузок: многопоточность, докачка, очередь")
set(CPACK_PACKAGE_DESCRIPTION "Менеджер загрузок в духе IDM: режет файл на сегменты, качает параллельно, умеет паузу и докачку.")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

# deb
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
# Qt подгружает libssl через dlopen, автоопределение зависимостей его не видит
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libssl1.1 | libssl3")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

# rpm
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Internet")
set(CPACK_RPM_PACKAGE_AUTOREQ ON)
set(CPACK_RPM_PACKAGE_REQUIRES "openssl-libs")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    /usr/share/applications /usr/share/icons /usr/share/icons/hicolor
    /usr/share/icons/hicolor/scalable /usr/share/icons/hicolor/scalable/apps)

include(CPack)
