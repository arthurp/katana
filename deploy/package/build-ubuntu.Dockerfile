ARG BASE_IMAGE
FROM ${BASE_IMAGE}

ENV DEBIAN_FRONTEND=noninteractive

# Setup environment for OpenMPI backport build
# Preset timezone info because otherwise install will ask
ENV TZ=America/Chicago
RUN set -eux; \
  apt-get update --quiet; \
  ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone; \
  apt-get install --yes --quiet \
    packaging-dev debian-keyring devscripts equivs openssh-client \
    dpkg-dev debhelper libevent-dev zlib1g-dev libhwloc-dev pkg-config \
    libibverbs-dev libpsm-infinipath1-dev libnuma-dev default-jdk flex chrpath

# Patches to fix the OpenMPI package to build in the environments we want to support
COPY debian/openmpi-ubuntu-*.patch /

ENV OPENMPI_DSC_URL=http://archive.ubuntu.com/ubuntu/pool/universe/o/openmpi/openmpi_4.0.3-6ubuntu2.dsc

# Download, patch, and build OpenMPI ubuntu package source (unless we are >= 20.04)
RUN set -eux; \
  RELEASE=$(lsb_release --release --short | tr -d .); \
  # No need to build on 20.04 on
  if [ ${RELEASE} -gt 2000 ]; then exit 0; fi; \
  # Download 20.04 source package
  dget --allow-unauthenticated --extract ${OPENMPI_DSC_URL}; \
  cd openmpi-4.0.3; \
  # Patch to allow backport
  if [ ${RELEASE} -lt 1800 ]; then PATCHVERSION=16.04; \
  elif [ ${RELEASE} -lt 2000 ]; then PATCHVERSION=18.04; \
  else PATCHVERSION=""; fi; \
  if [ "${PATCHVERSION}" ]; then patch -p1 -i /openmpi-ubuntu-${PATCHVERSION}.patch; fi;\
  # Mark the package as modified
  debchange --local '~katanabackports' --distribution "$(lsb_release --codename --short)" 'Backported by Katana Graph.'; \
  if [ ${RELEASE} -lt 1800 ]; then debchange --append 'No OpenFabrics Interfaces support.'; fi; \
  if [ ${RELEASE} -lt 2000 ]; then debchange --append 'No Fortran support. No Intel OPA / PSM2 support. Internal PMIx.'; fi; \
  # Install build dependencies
  mk-build-deps --install --remove --tool "apt-get -o Debug::pkgProblemResolver=yes --no-install-recommends --yes --quiet"; \
  apt-get remove --yes --quiet openmpi-build-deps; \
  # Build the packages
  dpkg-buildpackage -us -uc -sa; \
  mkdir /pkg; \
  cp -pv /*.deb /pkg

# Install the OpenMPI packages for use in Katana build
RUN set -eux; \
  RELEASE=$(lsb_release --release --short | tr -d .); \
  # Install distribution package for 20.04 and on
  if [ ${RELEASE} -gt 2000 ]; then apt-get install --yes --quiet libopenmpi-dev; \
  # Install our own packages otherwise
  else apt-get install --yes --quiet ./pkg/*openmpi*.deb; fi

# Extend environment for Katana build
RUN set -eux; \
  apt-get install --yes --quiet \
    apt-transport-https \
    ca-certificates \
    curl \
    gnupg \
    software-properties-common; \
  CODENAME=$(lsb_release --codename --short); \
  RELEASE=$(lsb_release --release --short | tr -d .); \
  curl -fL https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -; \
  apt-add-repository -y "deb http://apt.llvm.org/${CODENAME}/ llvm-toolchain-${CODENAME}-10 main"; \
  if [ ${RELEASE} -lt 1800 ]; then add-apt-repository -y ppa:ubuntu-toolchain-r/test; fi; \
  if [ ${RELEASE} -lt 2000 ]; then add-apt-repository -y ppa:cleishm/neo4j; fi; \
  apt-get update --quiet

RUN set -eux; \
  RELEASE=$(lsb_release --release --short | tr -d .); \
  apt-get install --yes --quiet \
    ccache \
    g++ \
    gcc \
    git \
    libcypher-parser-dev \
    libxml2-dev \
    llvm-10-dev \
    pkg-config \
    python3-pip \
    uuid-dev \
    libnuma-dev \
    zlib1g-dev; \
  if [ ${RELEASE} -lt 1800 ]; then \
    apt-get install --yes --quiet g++-7 gcc-7; \
    # as suggested by https://gist.github.com/jlblancoc/99521194aba975286c80f93e47966dc5
    ls -la /usr/bin/ | grep -oP "[\S]*(gcc|g\+\+)(-[a-z]+)*[\s]" | xargs bash -c 'for link in ${@:1}; do ln -s -f "/usr/bin/${link}-${0}" "/usr/bin/${link}"; done' 7 ; \
  fi;

ENV GO_URL=https://golang.org/dl/go1.15.5.linux-amd64.tar.gz
ENV TMP_GO=/tmp/go.tar.gz
RUN curl -fL -o ${TMP_GO} ${GO_URL} && tar -C /usr/local -xzf ${TMP_GO} && rm ${TMP_GO}

# These python dependencies, except for pandas and matplotlib, are pinned to
# versions that support Python 3.5, the system python version for Ubuntu 16.04.
# For pandas and matplotlib there is no version that supports both Python 3.5 and
# Python 3.8 (Ubuntu 20.04), so pick and choose based on the distro.
# TODO(amp): Reduce pinning once we don't need to support Python < 3.8
# TODO(amp): Install packages using apt if possible to improve package compatibility.
RUN set -eux; \
  RELEASE=$(lsb_release --release --short | tr -d .); \
  if [ ${RELEASE} -lt 2000 ]; then \
    PANDAS_VER=0.23.3; \
    MATPLOTLIB_VER=2.2.5; \
  else \
    PANDAS_VER=1.2.1; \
    MATPLOTLIB_VER=3.3.2; \
  fi; \
  pip3 install --upgrade --no-cache-dir cmake conan==1.33 pip==20.3.4 setuptools==50.3.2 ; \
  pip3 install \
    sphinxcontrib-applehelp==1.0.2 sphinxcontrib-devhelp==1.0.2 \
    sphinxcontrib-htmlhelp==1.0.3 sphinxcontrib-jsmath==1.0.1 \
    sphinxcontrib-qthelp==1.0.3 sphinxcontrib-serializinghtml==1.1.4 \
    numpy==1.18.3 pandas==${PANDAS_VER} qgrid==1.3.1 matplotlib==${MATPLOTLIB_VER} wheel==0.30.0

# TODO(amp): Install packages using apt if possible to improve package compatibility.
RUN conan profile new --detect --force default \
  && conan profile update settings.compiler.libcxx=libstdc++11 default \
  && conan remote add kmaragon https://api.bintray.com/conan/kmaragon/conan

ENV PATH=$PATH:/root/.local/bin
