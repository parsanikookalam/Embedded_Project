CC = gcc
CFLAGS = -Wall -Iinclude
LDFLAGS = -lssl -lcrypto

SRC = src/main.c src/server.c src/telemetry.c
OBJ = $(SRC:.c=.o)
TARGET = web_server

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ) $(LDFLAGS)

clean:
	rm -f $(OBJ) $(TARGET)