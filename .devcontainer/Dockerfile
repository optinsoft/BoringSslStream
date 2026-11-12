FROM mcr.microsoft.com/devcontainers/dotnet:2-8.0

RUN apt-get update && apt-get install -y \
    cmake \
    build-essential \
    ninja-build \
    pkg-config \
    git \
    && rm -rf /var/lib/apt/lists/*
