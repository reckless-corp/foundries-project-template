# syntax=docker/dockerfile:1
ARG BASE=debian:trixie-slim@sha256:a99cfc517144bc59b1978475ec53b46ecabec7e43635402ee5b77cc54cd1b20a
FROM --platform=$BUILDPLATFORM ${BASE} AS tools
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl git cmake ninja-build python3 python3-venv \
    device-tree-compiler xz-utils build-essential autoconf automake libtool pkg-config \
    gcc-aarch64-linux-gnu libc6-dev-arm64-cross && rm -rf /var/lib/apt/lists/*

FROM tools AS firmware
ARG ZEPHYR_REV=8f62a4ab82b5c2121c088842d3233f75b603ce35
ENV VIRTUAL_ENV=/opt/venv PATH=/opt/venv/bin:$PATH
RUN python3 -m venv /opt/venv && pip install --no-cache-dir west==1.5.0
WORKDIR /work
RUN git init zephyr && cd zephyr && git remote add origin https://github.com/zephyrproject-rtos/zephyr \
    && git fetch --depth=1 origin ${ZEPHYR_REV} && git checkout --detach FETCH_HEAD \
    && west init -l . && west update -n --fetch-opt=--depth=1 cmsis cmsis_6 hal_stm32 \
    && pip install --no-cache-dir -r scripts/requirements-base.txt
# Only the Cortex-M toolchain is needed. The builder runs natively on x86-64.
RUN apt-get update && apt-get install -y --no-install-recommends wget && rm -rf /var/lib/apt/lists/*
RUN test "$(uname -m)" = x86_64 \
    && curl -fL --retry 3 https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v1.0.0/zephyr-sdk-1.0.0_linux-x86_64_minimal.tar.xz -o /tmp/sdk.tar.xz \
    && echo '6b69a4b54b470e3aa21cdaf95cdc808431e8ca73a7b822a33a682e5b5d2c5512  /tmp/sdk.tar.xz' | sha256sum -c - \
    && tar -xJf /tmp/sdk.tar.xz -C /opt \
    && curl -fL --retry 3 https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v1.0.0/toolchain_gnu_linux-x86_64_arm-zephyr-eabi.tar.xz -o /tmp/arm.tar.xz \
    && echo '4b467e2fb26307b464915d0c9a87e47ab4d94d560888f239a76eeb56b4d9011f  /tmp/arm.tar.xz' | sha256sum -c - \
    && mkdir -p /opt/zephyr-sdk-1.0.0/gnu \
    && tar -xJf /tmp/arm.tar.xz -C /opt/zephyr-sdk-1.0.0/gnu \
    && /opt/zephyr-sdk-1.0.0/setup.sh -c && rm /tmp/sdk.tar.xz /tmp/arm.tar.xz
ENV ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk-1.0.0
COPY firmware /work/app
RUN west build -b arduino_uno_q /work/app -d /work/build \
    && python3 /work/app/check-image.py /work/build/zephyr/zephyr.elf \
    && printf '%s\n' "zephyr=${ZEPHYR_REV}" 'sdk=1.0.0' > /work/build/zephyr/build-info.txt
RUN mkdir /work/licenses && find zephyr modules -type f \
    \( -iname 'license*' -o -name 'COPYING*' -o -name 'NOTICE*' \) \
    -exec cp --parents '{}' /work/licenses/ \;

FROM tools AS flasher
ARG OPENOCD_REV=8da0578d7bce3e134821ccf10911182788debc32
RUN dpkg --add-architecture arm64 && apt-get update \
    && apt-get install -y --no-install-recommends libgpiod-dev:arm64 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work/openocd
RUN git init && git remote add origin https://github.com/openocd-org/openocd \
    && git fetch --depth=1 origin ${OPENOCD_REV} && git checkout --detach FETCH_HEAD \
    && git submodule update --init --depth=1 jimtcl \
    && ./bootstrap nosubmodule \
    && PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig ./configure \
       --host=aarch64-linux-gnu --prefix=/opt/openocd --enable-linuxgpiod \
       --enable-internal-jimtcl --disable-internal-libjaylink --disable-werror \
       --disable-doxygen-html --disable-doxygen-pdf \
    && make -j4 && make install \
    && mkdir -p /runtime/lib /runtime/share/licenses \
    && cp -L /usr/lib/aarch64-linux-gnu/libgpiod.so.3 /runtime/lib/ \
    && cp COPYING /runtime/share/licenses/OpenOCD.txt \
    && cp jimtcl/LICENSE /runtime/share/licenses/JimTcl.txt \
    && cp /usr/share/doc/libgpiod3*/copyright /runtime/share/licenses/libgpiod.txt \
    && printf '%s\n' "openocd=${OPENOCD_REV}" > /runtime/share/openocd-build.txt
COPY gpio-check.c /work/gpio-check.c
RUN aarch64-linux-gnu-gcc -Os -Wall -Wextra -Werror /work/gpio-check.c -o /runtime/gpio-check \
    && aarch64-linux-gnu-strip /runtime/gpio-check /opt/openocd/bin/openocd \
    && mkdir -p /runtime/share/src \
    && git archive --format=tar HEAD -o /tmp/openocd-source.tar \
    && git -C jimtcl archive --format=tar --prefix=jimtcl/ HEAD -o /tmp/jimtcl-source.tar \
    && tar -Af /tmp/openocd-source.tar /tmp/jimtcl-source.tar \
    && gzip -9 /tmp/openocd-source.tar \
    && mv /tmp/openocd-source.tar.gz /runtime/share/src/

FROM tools AS controller
COPY controller /work/controller
COPY firmware/src/protocol.c firmware/src/protocol.h /work/protocol/
RUN aarch64-linux-gnu-gcc -std=c11 -Os -Wall -Wextra -Werror -I/work/protocol \
    /work/controller/hatctl.c /work/protocol/protocol.c -o /work/uno-q-hatctl \
    && aarch64-linux-gnu-strip /work/uno-q-hatctl

FROM ${BASE}
# No target-architecture RUN instructions: an x86 builder needs no QEMU.
COPY --from=flasher /opt/openocd /opt/openocd
COPY --from=flasher /runtime/lib /usr/local/lib
COPY --from=flasher /runtime/share /usr/local/share
COPY --from=flasher /runtime/gpio-check /usr/local/bin/uno-q-gpio-check
COPY --from=controller /work/uno-q-hatctl /usr/local/bin/uno-q-hatctl
COPY --from=firmware /work/build/zephyr/zephyr.elf /firmware/zephyr.elf
COPY --from=firmware /work/build/zephyr/build-info.txt /firmware/build-info.txt
COPY --from=firmware /work/licenses /usr/local/share/licenses/firmware
COPY Dockerfile /usr/local/share/src/Dockerfile
COPY openocd /app/openocd
COPY --chmod=755 install.sh /app/install.sh
ENV LD_LIBRARY_PATH=/usr/local/lib PATH=/opt/openocd/bin:$PATH
LABEL org.opencontainers.image.title="UNO Q cowboy hat" \
      org.opencontainers.image.description="Standalone Zephyr firmware and boot-time SWD installer for Arduino UNO Q"
ENTRYPOINT ["/app/install.sh"]
CMD ["install"]
