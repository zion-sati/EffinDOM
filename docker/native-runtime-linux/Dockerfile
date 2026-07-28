FROM rockylinux:8

# The complete released Linux runtime is built in this image. Keep this on
# Rocky Linux 8 so both EffinDOM and its prebuilt dependencies retain the
# glibc 2.28 compatibility floor.
ARG NODE_VERSION=24.0.0
ARG TARGETARCH
ARG LIBDECOR_COMMIT=7807ae3480f5c6a37c5e8505d94af1e764aaf704

RUN dnf -y install dnf-plugins-core epel-release \
    && dnf config-manager --set-enabled powertools \
    && dnf -y install \
        ca-certificates \
        cairo-devel \
        clang \
        cmake \
        curl \
        binutils \
        dbus-daemon \
        dbus-devel \
        dbus-x11 \
        file \
        findutils \
        fontconfig-devel \
        gcc \
        gcc-c++ \
        git \
        libX11-devel \
        libXext-devel \
        libxkbcommon-devel \
        make \
        mesa-libEGL-devel \
        mesa-vulkan-drivers \
        meson \
        ninja-build \
        pango-devel \
        pkgconf-pkg-config \
        python3 \
        tar \
        vulkan-headers \
        vulkan-loader-devel \
        wayland-devel \
        wayland-protocols-devel \
        which \
        xorg-x11-server-Xvfb \
        xorg-x11-xauth \
        xz \
        zstd \
    && dnf clean all \
    && git clone --filter=blob:none https://gitlab.freedesktop.org/libdecor/libdecor.git /tmp/libdecor \
    && git -C /tmp/libdecor checkout "${LIBDECOR_COMMIT}" \
    && meson setup /tmp/libdecor/build /tmp/libdecor \
        --buildtype=release \
        --prefix=/usr \
        --libdir=lib64 \
        -Ddemo=false \
        -Dgtk=disabled \
    && meson compile -C /tmp/libdecor/build \
    && meson install -C /tmp/libdecor/build \
    && rm -rf /tmp/libdecor \
    && case "${TARGETARCH}" in \
         amd64) node_arch=x64 ;; \
         arm64) node_arch=arm64 ;; \
         *) echo "Unsupported Docker architecture: ${TARGETARCH}" >&2; exit 1 ;; \
       esac \
    && curl -fsSL "https://nodejs.org/dist/v${NODE_VERSION}/node-v${NODE_VERSION}-linux-${node_arch}.tar.xz" -o /tmp/node.tar.xz \
    && tar -xJf /tmp/node.tar.xz -C /opt \
    && ln -s "/opt/node-v${NODE_VERSION}-linux-${node_arch}/bin/node" /usr/local/bin/node \
    && ln -s "/opt/node-v${NODE_VERSION}-linux-${node_arch}/bin/npm" /usr/local/bin/npm \
    && rm -f /tmp/node.tar.xz \
    && mkdir -p /tmp/effindom-home \
    && chmod 777 /tmp/effindom-home

WORKDIR /work/EffinDOM
