project(vladimir)

find_package(Boost COMPONENTS system program_options regex locale date_time REQUIRED)

include_directories(SYSTEM
                    ${Boost_INCLUDE_DIRS}
                    ${SuperMarx_INCLUDE_DIRS})
                    
add_library(vladimircore scraper.cpp)
target_link_libraries(vladimircore
					  supermarx-downloader
					  ${Boost_LIBRARIES}) 

add_executable(vladimir main.cpp)
target_link_libraries(vladimir
					  vladimircore
					  supermarx-api)

include_directories(${Vladimir_INCLUDE_DIRS})
