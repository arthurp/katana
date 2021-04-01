ARG BASE_IMAGE

FROM ${BASE_IMAGE}

ARG PACKAGE_FILES

COPY $PACKAGE_FILES /tmp/

RUN yum update -y \
  && yum install -y ca-certificates \
  && yum localinstall -y /tmp/*.rpm \
  && rm /tmp/*.rpm

RUN cd /etc/ssl/certs && ln -s /etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem ca-certificates.crt
