.PHONY: all clean

C_FLAGS :=
LIBS := -lpthread
C_SRCS := watcher.c

APP := watcher

all: $(APP)

$(APP): $(C_SRCS:.c=.o)
	gcc -o $@ $^ $(LIBS)

$(C_SRCS:.c=.o) : %.o : src/%.c
	gcc -c -o $@ $< $(C_FLAGS)

clean:
	rm -fr $(APP) *.o
