ARG BASE_IMAGE

FROM ${BASE_IMAGE}

ARG PACKAGE_FILES

COPY $PACKAGE_FILES /tmp/

ENV DEBIAN_FRONTEND=noninteractive

RUN set -eux; \
  apt-get update --yes --quiet; \
  apt-get install --yes --quiet \
    apt-transport-https \
    ca-certificates \
    gnupg \
    software-properties-common; \
  RELEASE=$(lsb_release --release --short | tr -d .); \
  if [ ${RELEASE} -lt 2000 ]; then add-apt-repository -y ppa:cleishm/neo4j; fi; \
  apt-get update --yes; \
  apt-get install --yes --quiet --no-install-recommends \
    /tmp/*.deb \
    ca-certificates \
  && rm /tmp/*.deb  \
  && apt-get clean \
  && rm -rf /var/lib/apt/lists/*
