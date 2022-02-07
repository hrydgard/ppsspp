# First stage
FROM alpine:latest

COPY . /src


RUN apk add build-base wget git bash cmake python3 glu-dev

# Installing SDL2 from source because current SDL2 package in alpine
# has some tricks that make PPSSPP compilation to fail
ENV SDL_VERSION=2.0.20
RUN wget https://github.com/libsdl-org/SDL/archive/refs/tags/release-${SDL_VERSION}.tar.gz && \
    tar -xf release-${SDL_VERSION}.tar.gz && cd SDL-release-${SDL_VERSION} && mkdir build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j$(getconf _NPROCESSORS_ONLN) clean && \
    make -j$(getconf _NPROCESSORS_ONLN) && \
    make -j$(getconf _NPROCESSORS_ONLN) install

RUN cd src/ffmpeg && ./linux_x86-64.sh
RUN cd src && ./b.sh --headless

# Second stage
FROM alpine:latest

# Install required dependencies to make headless to work
RUN apk add --no-cache sdl2 libstdc++ glu-dev

# Copy minimal things to make headless to work
COPY --from=0 src/build/PPSSPPHeadless usr/local/bin/

RUN PPSSPPHeadless || true