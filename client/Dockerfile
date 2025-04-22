FROM ubuntu:22.04

RUN apt-get update && \
    apt-get install -y build-essential net-tools iputils-ping && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY client.cpp .

# Сборка
RUN g++ client.cpp -o client -pthread

CMD ["./client"]
