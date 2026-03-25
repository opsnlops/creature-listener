
FROM debian:trixie AS build

RUN apt-get update && apt-get upgrade -y

RUN apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        debhelper \
        devscripts \
        fakeroot \
        git \
        libasound2-dev \
        libcurl4-openssl-dev \
        libabsl-dev \
        libprotobuf-dev \
        libssl-dev \
        ninja-build \
        pkg-config \
        protobuf-compiler

# Copy source into a directory named for the source package
RUN mkdir -p /build/creature-listener
COPY src/ /build/creature-listener/src/
COPY debian/ /build/creature-listener/debian/
COPY CMakeLists.txt README.md LICENSE /build/creature-listener/

# Build the .deb
RUN cd /build/creature-listener && \
    dpkg-buildpackage -us -uc -b

# Collect artifacts
RUN mkdir -p /package && cp /build/*.deb /package/


# Package stage — just the .deb files
FROM scratch AS package
COPY --from=build /package/ /package/
