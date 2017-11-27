TARGETS=hangman

CFLAGS=-O0 -g -Wall -Wvla -Werror -Wno-error=unused-variable

all: $(TARGETS)

hangman: hangman.cpp
	g++ $(CFLAGS) -o hangman hangman.cpp -lpthread

clean:
	rm -f $(TARGETS)