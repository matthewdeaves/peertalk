# PeerTalk Development Environment
# Includes Retro68 toolchain for Classic Mac cross-compilation
#
# Build: docker build -t peertalk-dev -f docker/Dockerfile .
# Run:   docker run -it -v $(pwd):/workspace peertalk-dev

FROM ubuntu:22.04 AS builder

# Prevent interactive prompts during build
ENV DEBIAN_FRONTEND=noninteractive

# Install Retro68 build dependencies
RUN apt-get update && apt-get install -y \
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
    unzip \
    && rm -rf /var/lib/apt/lists/*

# Copy MPW Interfaces for MacTCP/OT/AppleTalk headers
# This file contains Apple's Universal Interfaces including networking headers
COPY resources/retro68/MPW_Interfaces.zip /tmp/

# Clone Retro68
WORKDIR /opt
RUN git clone https://github.com/autc04/Retro68.git && \
    cd Retro68 && \
    git submodule update --init --recursive

# Extract MPW Interfaces (includes MacTCP.h, OpenTransport.h, ADSP.h, etc.)
RUN mkdir -p /opt/Retro68/InterfacesAndLibraries && \
    unzip -o /tmp/MPW_Interfaces.zip -d /opt/Retro68/InterfacesAndLibraries && \
    rm /tmp/MPW_Interfaces.zip

# Build toolchain (this takes a while - ~30-60 minutes)
WORKDIR /opt/Retro68-build
RUN ../Retro68/build-toolchain.bash

# Cleanup intermediate artifacts (~5-6GB savings)
RUN rm -rf gcc-build gcc-build-ppc binutils-build binutils-build-ppc \
    build-host build-target build-target-ppc build-target-carbon

# --- Runtime Stage (smaller final image) ---
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install runtime dependencies and static analysis tools
RUN apt-get update && apt-get install -y \
    build-essential \
    make \
    git \
    python3 \
    python3-pip \
    python3-venv \
    curl \
    jq \
    hfsutils \
    lcov \
    bc \
    universal-ctags \
    clang-format \
    cmake \
    libboost-all-dev \
    # Static analysis tools
    cppcheck \
    pmccabe \
    valgrind \
    # Node.js for jscpd
    nodejs \
    npm \
    && rm -rf /var/lib/apt/lists/*

# Install Python-based analysis tools
RUN pip3 install --break-system-packages \
    mcp>=1.0.0 \
    lizard \
    flawfinder

# Install Node-based analysis tools
RUN npm install -g jscpd@3

# Install GitHub CLI
RUN curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg | dd of=/usr/share/keyrings/githubcli-archive-keyring.gpg \
    && echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" | tee /etc/apt/sources.list.d/github-cli.list > /dev/null \
    && apt-get update && apt-get install -y gh \
    && rm -rf /var/lib/apt/lists/*

# Copy only the built toolchain from builder stage
COPY --from=builder /opt/Retro68-build/toolchain /opt/Retro68-build/toolchain

# Copy Retro68 source for LaunchAPPLServer and Samples
COPY --from=builder /opt/Retro68/LaunchAPPL /opt/Retro68/LaunchAPPL
COPY --from=builder /opt/Retro68/Samples /opt/Retro68/Samples

# Set up environment
ENV RETRO68=/opt/Retro68-build/toolchain
ENV PATH="${RETRO68}/bin:${PATH}"

# Prebuild LaunchAPPLServer for 68k (m68k toolchain is always available)
RUN cd /opt/Retro68/LaunchAPPL && \
    mkdir -p build-m68k && cd build-m68k && \
    cmake .. -DCMAKE_TOOLCHAIN_FILE=/opt/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

# Prebuild LaunchAPPLServer for PPC (if powerpc toolchain exists)
RUN cd /opt/Retro68/LaunchAPPL && \
    if [ -f /opt/Retro68-build/toolchain/powerpc-apple-macos/cmake/retro68.toolchain.cmake ]; then \
        mkdir -p build-ppc && cd build-ppc && \
        cmake .. -DCMAKE_TOOLCHAIN_FILE=/opt/Retro68-build/toolchain/powerpc-apple-macos/cmake/retro68.toolchain.cmake -DCMAKE_BUILD_TYPE=Release && \
        make -j$(nproc); \
    else \
        echo "PowerPC toolchain not found - skipping PPC build"; \
    fi

# Prebuild sample apps for 68k (m68k toolchain is always available)
RUN for sample in Dialog HelloWorld Launcher Raytracer; do \
        echo "Building $sample for 68k..."; \
        cd /opt/Retro68/Samples/$sample && \
        mkdir -p build-m68k && cd build-m68k && \
        cmake .. -DCMAKE_TOOLCHAIN_FILE=/opt/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake -DCMAKE_BUILD_TYPE=Release && \
        make -j$(nproc) || true; \
    done

# Prebuild sample apps for PPC (if powerpc toolchain exists)
RUN if [ -f /opt/Retro68-build/toolchain/powerpc-apple-macos/cmake/retro68.toolchain.cmake ]; then \
        for sample in Dialog HelloWorld Launcher Raytracer; do \
            echo "Building $sample for PPC..."; \
            cd /opt/Retro68/Samples/$sample && \
            mkdir -p build-ppc && cd build-ppc && \
            cmake .. -DCMAKE_TOOLCHAIN_FILE=/opt/Retro68-build/toolchain/powerpc-apple-macos/cmake/retro68.toolchain.cmake -DCMAKE_BUILD_TYPE=Release && \
            make -j$(nproc) || true; \
        done; \
    else \
        echo "PowerPC toolchain not found - skipping PPC sample builds"; \
    fi

# Create workspace
WORKDIR /workspace

# Default command
CMD ["/bin/bash"]
