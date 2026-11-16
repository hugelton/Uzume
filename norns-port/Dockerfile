# Build norns (matron+crone+softcut in the single `norns` binary of current main) for linux/arm64 on Debian trixie.
# The context is assembled by build.sh (patched copy of third_party/norns + libmonome), not third_party/ itself.
FROM --platform=linux/arm64 debian:trixie-slim
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential pkg-config python3 git ca-certificates \
    libevdev-dev liblo-dev libudev-dev libcairo2-dev liblua5.3-dev libavahi-compat-libdnssd-dev \
    libasound2-dev libncurses-dev libsndfile1-dev libjack-jackd2-dev libnng-dev libgpiod-dev \
    libsdl2-dev libatomic1 \
  && rm -rf /var/lib/apt/lists/*
# jackd (dummy driver) to run matron/crone headless in the container; fonts
RUN apt-get update && apt-get install -y --no-install-recommends jackd2 python-is-python3 libglib2.0-dev && rm -rf /var/lib/apt/lists/*
COPY libmonome /src/libmonome
RUN cd /src/libmonome && ./waf configure --prefix=/usr/local && ./waf && ./waf install && ldconfig
COPY norns /src/norns
WORKDIR /src/norns
RUN cp /src/libmonome/waf /src/norns/waf  # norns ships waf 2.0.x (2018) that needs the removed python module `imp`; libmonome ships a newer one
