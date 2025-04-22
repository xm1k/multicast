FROM ubuntu:22.04

# Устанавливаем компилятор и необходимые утилиты, включая терминальные данные
RUN apt-get update && \
    apt-get install -y build-essential net-tools iputils-ping ncurses-term && \
    rm -rf /var/lib/apt/lists/*

# Задаём переменную окружения TERM, чтобы clear не выдавал ошибок
ENV TERM=xterm

WORKDIR /app
COPY server.cpp .

# Сборка
RUN g++ server.cpp -o server -pthread

EXPOSE 50000/udp 50001/udp

CMD ["./server"]
