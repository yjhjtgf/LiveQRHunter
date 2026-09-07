cmake_minimum_required(VERSION 3.26)

add_executable(test_qrscanner test_qrscanner.cpp)
target_link_libraries(test_qrscanner PUBLIC gtest gtest_main ${PROJECT_LIBRARIES})
target_include_directories(test_qrscanner PUBLIC ../src/Core)
add_test(NAME test_qrscanner COMMAND test_qrscanner)
set_tests_properties(test_qrscanner PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_qrinfo test_qrinfo.cpp)
target_link_libraries(test_qrinfo PUBLIC gtest gtest_main)
target_include_directories(test_qrinfo PUBLIC ../src/Core)
add_test(NAME test_qrinfo COMMAND test_qrinfo)

add_executable(test_livestreamlink test_livestreamlink.cpp)
target_link_libraries(test_livestreamlink PUBLIC gtest gtest_main ${PROJECT_LIBRARIES})
target_include_directories(test_livestreamlink PUBLIC ../src/Core)
add_test(NAME test_livestreamlink COMMAND test_livestreamlink)

add_executable(test_reallive_images test_reallive_images.cpp)
target_link_libraries(test_reallive_images PUBLIC gtest gtest_main ${PROJECT_LIBRARIES})
target_include_directories(test_reallive_images PUBLIC ../src/Core)
add_test(NAME test_reallive_images COMMAND test_reallive_images)
set_tests_properties(test_reallive_images PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
