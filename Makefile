.PHONY: all clean

INC_DIR := include
C_FLAGS := -I$(INC_DIR)
LIBS := -lpthread -lm -lcurl
C_SRCS := watcher.c ini.c cJSON.c

APP := watcher

all: $(APP)

$(APP): $(C_SRCS:.c=.o)
	gcc -o $@ $^ $(LIBS)

$(C_SRCS:.c=.o) : %.o : src/%.c
	gcc -c -o $@ $< $(C_FLAGS)

clean:
	rm -fr $(APP) *.o
