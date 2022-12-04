FROM --platform=linux/amd64 ubuntu:20.04 as builder

RUN apt-get update
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential cmake wget cmake g++ m4 xz-utils libgmp-dev unzip zlib1g-dev libboost-program-options-dev libboost-serialization-dev libboost-regex-dev libboost-iostreams-dev libtbb-dev libreadline-dev pkg-config git liblapack-dev libgsl-dev flex bison libcliquer-dev gfortran file dpkg-dev libopenblas-dev rpm clang

RUN wget https://soplex.zib.de/download/release/soplex-6.0.0.tgz -O soplex.tgz
RUN mkdir soplex
RUN tar -xzf soplex.tgz -C soplex --strip-components 1
WORKDIR /soplex/build
RUN cmake ..
RUN make -j8
RUN make install

COPY . /repo
WORKDIR /repo/build
RUN cmake .. -DPAPILO=OFF -DZIMPL=OFF -DIPOPT=OFF -DCMAKE_INSTALL_PREFIX=/install
RUN make -j8
RUN make install
RUN clang++ /repo/mayhem/fuzz_SCIPintervalNegateReal.cpp -fsanitize=fuzzer,address -I /install/include/scip/ -I /install/include/ -L /install/lib/ -lscip -o /fuzz

FROM ubuntu:20.04 as package
RUN apt-get update
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y libreadline-dev
COPY --from=builder /install /install
COPY --from=builder /fuzz /fuzz_SCIPintervalNegateReal
ENV LD_LIBRARY_PATH=/install/lib
