# Keep the libc portability environment explicit and independently updatable.
# This image builds normal shared/dynamic artifacts; it is not a static release.
FROM alpine:3.24.1

RUN apk add --no-cache \
    build-base \
    cmake \
    curl-dev \
    python3 \
    zlib-dev
