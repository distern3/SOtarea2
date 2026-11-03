# Entorno Linux fijo con gcc y valgrind. El código se monta como volumen
# (ver compose.yaml); el programa también compila nativo con `make`.
FROM debian:stable-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential valgrind && rm -rf /var/lib/apt/lists/*
WORKDIR /app
