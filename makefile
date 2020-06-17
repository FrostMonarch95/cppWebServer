MUDUO_DIRECTORY ?= /home/build/debug-install
#MUDUO_DIRECTORY ?= $(HOME)/build/install
MUDUO_INCLUDE = $(MUDUO_DIRECTORY)/include/
TIMER_INCLUDE = timer/
LOCKER_INCLUDE = locker/
HTTP_CONN_INCLUDE = http_conn/
THREAD_POOL_INCLUDE = thread_pool/
MUDUO_LIBRARY = $(MUDUO_DIRECTORY)/lib/
SRC := $(wildcard *.c *.cpp)
HTTP_COON := http_conn/http_conn.cpp
#$(info $(SRC))
#$(info $(HTTP_COON))
SRC := $(SRC)  $(HTTP_COON)
#$(info $(SRC))
CXXFLAGS = -g -O0 -Wall -Wextra  \
	   -Wconversion -Wno-unused-parameter \
	   -Wold-style-cast -Woverloaded-virtual \
	   -Wpointer-arith -Wshadow -Wwrite-strings \
	   -march=native -rdynamic \
	   -I$(MUDUO_INCLUDE) -I $(TIMER_INCLUDE) -I $(LOCKER_INCLUDE) -I $(HTTP_CONN_INCLUDE) -I $(THREAD_POOL_INCLUDE)
 

LDFLAGS = -L$(MUDUO_LIBRARY) -lmuduo_net -lmuduo_base -lpthread -lrt

all: run
clean:
	rm -f echo core

run:$(SRC)
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

.PHONY: all clean
