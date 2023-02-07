#!/usr/bin/bash
cd "$(dirname "$0")"

FLAGS="-march=native -O3 -DNDEBUG -s -Wall"
#FLAGS="-O0 -ggdb -fsanitize=address -Wall"
#FLAGS="-march=native -O3 -DNDEBUG -ggdb"

INCLUDES="-I ../../io2/include \
    -I ../ \
    -DSEQAN_HAS_ZLIB  -isystem../../lib/seqan/include \
    -DBIO_HAS_ZLIB -isystem../../lib/b.i.o./include \
    -DSEQAN3_HAS_ZLIB -isystem../../lib/seqan3/include -isystem../../lib/submodules/sdsl-lite/include"
ARGS="-std=c++20 ${FLAGS} ${INCLUDES}"


mkdir -p obj/io3/fasta obj/io3/bcf obj/io3/vcf obj/src
g++ ${ARGS} -c ../io3/fasta/reader.cpp -o obj/io3/fasta/reader.o
g++ ${ARGS} -c ../io3/fasta/reader_mt.cpp -o obj/io3/fasta/reader_mt.o
g++ ${ARGS} -c ../io3/fasta/writer.cpp -o obj/io3/fasta/writer.o
g++ ${ARGS} -c ../io3/vcf/reader.cpp -o obj/io3/vcf/reader.o
g++ ${ARGS} -c ../io3/vcf/writer.cpp -o obj/io3/vcf/writer.o
g++ ${ARGS} -c ../io3/bcf/reader.cpp -o obj/io3/bcf/reader.o
g++ ${ARGS} -c ../io3/bcf/writer.cpp -o obj/io3/bcf/writer.o
g++ ${ARGS} -c src/main.cpp -o obj/src/main.o
g++ ${ARGS} -c src/bio.cpp -o obj/src/bio.o
g++ ${ARGS} -c src/io3.cpp -o obj/src/io3.o

#g++ $(find obj | grep \.o\$) -lasan -lz-ng -lz  -o benchmark
g++ $(find obj | grep \.o\$) -lz-ng -lz  -o benchmark