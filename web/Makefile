CC = gcc
CFLAGS = -Wall -Wextra -O2 -Iinclude -D_DEFAULT_SOURCE
LDFLAGS = -lssl -lcrypto -lsqlite3 -lcurl -lpthread -lmosquitto

SRC = src/main.c src/server.c src/telemetry.c src/persons_state.c \
      src/email_alert.c src/mqtt_pub.c src/guard_state.c src/camera_state.c \
      src/detection_state.c src/feature_flags.c src/features_part4.c
OBJ = $(SRC:.c=.o)
TARGET = web_server

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ) $(LDFLAGS)

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
