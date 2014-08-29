CC=gcc
OBJ = crc16.o main.o

CFLAGS += -pedantic -fno-stack-protector -Wall

LIB = filedup.a

BIN = fd
all: $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(BIN)

$(LIB): $(OBJ)
	ar crv $(LIB) $(OBJ)

clean:
	-rm -f $(OBJ) $(LIB) $(BIN)

