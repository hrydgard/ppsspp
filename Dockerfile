# First stage
FROM alpine:latest

COPY . /src


RUN apk add build-base wget git bash cmake python3 glu-dev sdl2-dev

RUN cd src/ffmpeg && ./linux_x86-64.sh
RUN cd src && ./b.sh --headless

# Second stage
FROM alpine:latest

# Install required dependencies to make headless to work
RUN apk add --no-cache sdl2 libstdc++ glu-dev

# Copy minimal things to make headless to work
COPY --from=0 src/build/PPSSPPHeadless usr/local/bin/

RUN PPSSPPHeadless || true