ARG BASE_IMAGE=debian:12-slim
FROM ${BASE_IMAGE}

ARG DISTRO
RUN if [ "${DISTRO}" = "arch" ]; then \
      pacman --disable-sandbox -Syu --noconfirm --needed \
        bash ca-certificates cairo dbus fontconfig libdecor libx11 libxext noto-fonts-cjk \
        vulkan-icd-loader xorg-server-xvfb xorg-xauth; \
    else \
      apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        bash ca-certificates dbus-x11 fonts-noto-cjk libdecor-0-0 libdecor-0-plugin-1-cairo \
        libexpat1 libfontconfig1 libvulkan1 libx11-6 libxext6 xauth xvfb && \
      rm -rf /var/lib/apt/lists/*; \
    fi

COPY docker/native-relocation/launch-relocated.sh /usr/local/bin/launch-relocated
RUN chmod +x /usr/local/bin/launch-relocated
ENTRYPOINT ["/usr/local/bin/launch-relocated"]
