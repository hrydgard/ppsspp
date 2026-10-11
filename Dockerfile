FROM debian:bookworm AS build
RUN apt-get update && apt-get install -y --no-install-recommends build-essential cmake git python3 pkg-config libgl1-mesa-dev
WORKDIR /src
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target ppsspp_libretro -j2
FROM debian:bookworm-slim
COPY --from=build /src/build/ppsspp_libretro.so /usr/local/lib/libretro/ppsspp_libretro.so
