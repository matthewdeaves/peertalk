# PeerTalk Development Environment
# Includes Retro68 toolchain for Classic Mac cross-compilation
#
# Build: docker build -t peertalk-dev -f docker/Dockerfile .
# Run:   docker run -it -v $(pwd):/workspace peertalk-dev
#
# Expected size: ~3-4GB (toolchain ~2GB, base + tools ~1-2GB)

FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Install Retro68 build dependencies (only needed for building toolchain)
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    libgmp-dev \
    libmpfr-dev \
    libmpc-dev \
    libboost-all-dev \
    bison \
    flex \
    texinfo \
    ruby \
    git \
    ca-certificates \
    unzip \
    && rm -rf /var/lib/apt/lists/*

# Copy MPW Interfaces for MacTCP/OT/AppleTalk headers
COPY resources/retro68/MPW_Interfaces.zip /tmp/

# Clone Retro68
WORKDIR /opt
RUN git clone --depth 1 https://github.com/autc04/Retro68.git && \
    cd Retro68 && \
    git submodule update --init --recursive

# Extract MPW Interfaces
RUN mkdir -p /opt/Retro68/InterfacesAndLibraries && \
    unzip -o /tmp/MPW_Interfaces.zip -d /opt/Retro68/InterfacesAndLibraries && \
    rm /tmp/MPW_Interfaces.zip

# Build toolchain
WORKDIR /opt/Retro68-build
RUN ../Retro68/build-toolchain.bash

# Cleanup intermediate build artifacts
RUN rm -rf gcc-build gcc-build-ppc binutils-build binutils-build-ppc \
    build-host build-target build-target-ppc build-target-carbon

# --- Runtime Stage (smaller final image) ---
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install ONLY what's needed for using the toolchain and running builds
RUN apt-get update && apt-get install -y --no-install-recommends \
    # Core build tools
    build-essential \
    make \
    cmake \
    # Boost runtime libs (Retro68 linker depends on boost_filesystem)
    libboost-filesystem1.74.0 \
    # HFS disk utilities for Mac
    hfsutils \
    # Coverage
    lcov \
    # Python for scripts
    python3 \
    python3-pip \
    python3-venv \
    # Static analysis
    cppcheck \
    pmccabe \
    # Shell math
    bc \
    # Node.js for jscpd
    nodejs \
    npm \
    && rm -rf /var/lib/apt/lists/* \
    && apt-get clean

# Install Python tools
RUN python3 -m venv /opt/venv && \
    /opt/venv/bin/pip install --no-cache-dir --upgrade pip && \
    /opt/venv/bin/pip install --no-cache-dir mcp>=1.0.0 lizard flawfinder click rich

ENV PATH="/opt/venv/bin:${PATH}"

# Install jscpd
RUN npm install -g jscpd@3 && npm cache clean --force

# Copy only the built toolchain from builder stage
COPY --from=builder /opt/Retro68-build/toolchain /opt/Retro68-build/toolchain

# Copy Retro68 source for LaunchAPPLServer and Samples
COPY --from=builder /opt/Retro68/LaunchAPPL /opt/Retro68/LaunchAPPL
COPY --from=builder /opt/Retro68/Samples /opt/Retro68/Samples

# Set up environment
ENV RETRO68=/opt/Retro68-build/toolchain
ENV PATH="${RETRO68}/bin:${PATH}"

# Prebuild LaunchAPPLServer for 68k
RUN cd /opt/Retro68/LaunchAPPL && \
    mkdir -p build-m68k && cd build-m68k && \
    cmake .. -DCMAKE_TOOLCHAIN_FILE=/opt/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

# Prebuild LaunchAPPLServer for PPC (if toolchain exists)
RUN cd /opt/Retro68/LaunchAPPL && \
    if [ -f /opt/Retro68-build/toolchain/powerpc-apple-macos/cmake/retro68.toolchain.cmake ]; then \
        mkdir -p build-ppc && cd build-ppc && \
        cmake .. -DCMAKE_TOOLCHAIN_FILE=/opt/Retro68-build/toolchain/powerpc-apple-macos/cmake/retro68.toolchain.cmake -DCMAKE_BUILD_TYPE=Release && \
        make -j$(nproc); \
    fi

# Prebuild sample apps for 68k
RUN for sample in Dialog HelloWorld Launcher Raytracer; do \
        cd /opt/Retro68/Samples/$sample && \
        mkdir -p build-m68k && cd build-m68k && \
        cmake .. -DCMAKE_TOOLCHAIN_FILE=/opt/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake -DCMAKE_BUILD_TYPE=Release && \
        make -j$(nproc) || true; \
    done

# Prebuild sample apps for PPC (if toolchain exists)
RUN if [ -f /opt/Retro68-build/toolchain/powerpc-apple-macos/cmake/retro68.toolchain.cmake ]; then \
        for sample in Dialog HelloWorld Launcher Raytracer; do \
            cd /opt/Retro68/Samples/$sample && \
            mkdir -p build-ppc && cd build-ppc && \
            cmake .. -DCMAKE_TOOLCHAIN_FILE=/opt/Retro68-build/toolchain/powerpc-apple-macos/cmake/retro68.toolchain.cmake -DCMAKE_BUILD_TYPE=Release && \
            make -j$(nproc) || true; \
        done; \
    fi

WORKDIR /workspace

CMD ["/bin/bash"]
