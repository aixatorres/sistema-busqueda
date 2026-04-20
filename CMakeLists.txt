cmake_minimum_required(VERSION 3.10)
project(sistema_busqueda)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_executable(servidor main.cpp)

target_link_libraries(servidor
    pthread
    sqlite3
)
