CC = gcc
CFLAGS = -Wall -Wextra -std=c17 -pthread
TARGET = torneo
SRCS = main.c servidor.c mesa.c jugador.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)


