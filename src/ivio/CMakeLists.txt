cmake_minimum_required (VERSION 3.8)

project(ivio)

# ivio library
add_library(${PROJECT_NAME}
    bam/reader.cpp
    bcf/reader.cpp
    bcf/writer.cpp
    fasta/reader.cpp
    fasta/reader_mt.cpp
    fasta/writer.cpp
    fastq/reader.cpp
    sam/reader.cpp
    vcf/reader.cpp
    vcf/writer.cpp
)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME}
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(${PROJECT_NAME}
#//!TODO must have a better way to link against z library
    z
)
