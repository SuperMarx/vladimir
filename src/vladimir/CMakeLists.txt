project(vladimir)

find_package(jsoncpp REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(Boost COMPONENTS system program_options regex locale date_time filesystem REQUIRED)

include_directories(SYSTEM
                    ${Boost_INCLUDE_DIRS}
					${jsoncpp_INCLUDE_DIRS}
					${yaml-cpp_INCLUDE_DIRS}
                    ${SuperMarx_INCLUDE_DIRS})
                    
add_library(vladimircore scraper.cpp)
target_link_libraries(vladimircore
					  supermarx-downloader
					  ${jsoncpp_LIBRARIES}
					  ${yaml-cpp_LIBRARIES}
					  ${Boost_LIBRARIES}) 

add_executable(vladimir main.cpp)
target_link_libraries(vladimir
					  vladimircore
					  supermarx-api)

include_directories(${Vladimir_INCLUDE_DIRS})
