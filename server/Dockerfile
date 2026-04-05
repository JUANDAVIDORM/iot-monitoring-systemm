# Imagen base Ubuntu 24.04 (Noble) para alinear con la instancia EC2
FROM ubuntu:24.04

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       build-essential \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY server.c Makefile ./

RUN make

# Directorio para logs
RUN mkdir -p /logs

ENV PORT=5000
ENV LOG_FILE=/logs/server.log

EXPOSE 5000 5001

CMD ["/bin/sh", "-c", "./server $PORT $LOG_FILE"]
