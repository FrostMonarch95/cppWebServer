compiler = g++
target = run
sources := $(wildcard *.c *.cpp)
sources := $(filter-out 15-2pool_cgi.cpp,$(sources))
sources := $(filter-out 11-3nonactive_conn.cpp,$(sources))
library = -lpthread
olist := $(patsubst %.cpp,%.o,$(sources))
$(target):$(olist)
	$(compiler) $(olist) -o run $(library)
%.o:%.cpp 
	$(compiler) $< -c 
clean:
	rm -rf *.o run
