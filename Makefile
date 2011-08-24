OUT        = nosr
CFLAGS    := --std=c99 -g -pedantic -Wall -Wextra -Werror -pthread $(CFLAGS) $(CPPFLAGS)
LDFLAGS   := -larchive -lpcre $(LDFLAGS)

SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)

all: $(OUT)

.c.o:
	$(CC) -c $(CFLAGS) $<

$(OUT): $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

strip: $(OUT)
	strip --strip-all $(OUT)

clean:
	$(RM) $(OBJ) $(OUT)
