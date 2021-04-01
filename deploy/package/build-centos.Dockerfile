ARG BASE_IMAGE
FROM ${BASE_IMAGE}

RUN set -eux; \
  yum update -y; \
  yum install -y https://apache.bintray.com/arrow/centos/8/x86_64/Packages/apache-arrow-release-1.0.0-1.el8.noarch.rpm; \
  yum install -y dnf-plugins-core epel-release; \
  yum config-manager --set-enabled powertools; \
  yum config-manager --set-enabled epel

RUN yum install -y gcc-c++-8.3.1 gcc-gfortran-8.3.1 make-1:4.2.1 autoconf-2.69 \
    libtool-2.4.6 llvm-devel-10.0.1 llvm-static-10.0.1 \
    libxml2-devel-2.9.7 libatomic-8.3.1 libuuid-devel-2.32.1 openssl-devel \
    ncurses-compat-libs-6.1 rpm-build-4.14.3 \
    python3-pip git ccache

ENV CYPHER_VERSION=0.6.2
ENV CYPHER_URL=https://github.com/cleishm/libcypher-parser/releases/download/v${CYPHER_VERSION}/libcypher-parser-${CYPHER_VERSION}.tar.gz
ENV TMP_CYPHER=/tmp/libcypher.tar.gz
ENV CYPHER_DIR=libcypher-parser-${CYPHER_VERSION}
RUN curl -fL -o ${TMP_CYPHER} ${CYPHER_URL} && tar xf ${TMP_CYPHER} \
    && cd ${CYPHER_DIR} && ./configure && make clean check && make install && \
    cd ../ && rm -r ${CYPHER_DIR} ${TMP_CYPHER}

RUN set -eux; \
  curl -Lf --output /tmp/openmpi.tar.gz https://download.open-mpi.org/release/open-mpi/v4.0/openmpi-4.0.5.tar.gz; \
  echo "572e777441fd47d7f06f1b8a166e7f44b8ea01b8b2e79d1e299d509725d1bd05  /tmp/openmpi.tar.gz" | sha256sum -c; \
  (tar xzvf /tmp/openmpi.tar.gz -C /tmp && cd /tmp/openmpi-* && ./configure --prefix=/usr/local/katana && make -j 4 install); \
  find /usr/local/katana/share/man -type f -print0 | xargs -0 gzip -9; \
  (cd /usr/local/katana/share/man/man1 && ln -s mpic++.1.gz mpiCC.1.gz && rm mpiCC.1); \
  rm -r /tmp/openmpi*

# TODO(amp): Install packages using yum if possible to improve package compatibility.
RUN pip3 install --upgrade --no-cache-dir pip setuptools cmake conan==1.33 \
    sphinxcontrib-applehelp==1.0.2 sphinxcontrib-devhelp==1.0.2 \
    sphinxcontrib-htmlhelp==1.0.3 sphinxcontrib-jsmath==1.0.1 \
    sphinxcontrib-qthelp==1.0.3 sphinxcontrib-serializinghtml==1.1.4 \
    numpy==1.18.3 pandas==0.23.3 qgrid==1.3.1 matplotlib==2.2.5 wheel==0.30.0

# TODO(amp): Install packages using yum if possible to improve package compatibility.
RUN conan profile new --detect --force default \
  && conan profile update settings.compiler.libcxx=libstdc++11 default \
  && conan remote add kmaragon https://api.bintray.com/conan/kmaragon/conan

ENV GO_URL=https://golang.org/dl/go1.15.5.linux-amd64.tar.gz
ENV TMP_GO=/tmp/go.tar.gz
RUN curl -fL -o ${TMP_GO} ${GO_URL} && tar -C /usr/local -xzf ${TMP_GO} && rm ${TMP_GO}

RUN update-alternatives --install /usr/lib64/libtinfo.so libtinfo.so /usr/lib64/libtinfo.so.6 99

ENV PATH=$PATH:/root/.local/bin
