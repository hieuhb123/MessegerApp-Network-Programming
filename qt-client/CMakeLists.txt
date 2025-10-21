cmake_minimum_required(VERSION 3.14)
project(MessengerQtClient LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt5 COMPONENTS Widgets Network REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/../include)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

add_executable(messenger_qt
    main.cpp
    mainwindow.cpp
)

target_link_libraries(messenger_qt PRIVATE Qt5::Widgets Qt5::Network)
