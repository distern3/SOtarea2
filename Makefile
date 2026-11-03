CC      = gcc
CFLAGS  = -Wall -Wextra -std=c17 -pthread -g -O2
LDFLAGS = -pthread
TARGET  = torneo
SRCS    = main.c servidor.c mesa.c jugador.c tablero.c config.c log.c
OBJS    = $(SRCS:.c=.o)
HDRS    = $(wildcard *.h)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

# Binario con ThreadSanitizer (detector de data races).
tsan: $(SRCS) $(HDRS)
	$(CC) -Wall -Wextra -std=c17 -pthread -g -O1 -fsanitize=thread -o $(TARGET)_tsan $(SRCS) $(LDFLAGS)

clean:
	rm -f $(OBJS) $(TARGET) $(TARGET)_tsan

.PHONY: all clean tsan
