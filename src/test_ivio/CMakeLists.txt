cmake_minimum_required (VERSION 3.8)

project(test_ivio)

# fmindex-collectionunittests
add_executable(${PROJECT_NAME}
    main.cpp
    fasta_reader.cpp
)
target_link_libraries(${PROJECT_NAME}
    Catch2::Catch2
    ivio::ivio
)


add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
