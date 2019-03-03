.PHONY:all
all:server upload 
cc=g++
server:SharedFileServer.cc PoolThread.hpp SharedFileServer.hpp utils.hpp -lpthread 
	$(cc) -o $@ $^  -std=c++11
upload:upload.cc utils.hpp -lpthread 
	$(cc) -o $@ $^  -std=c++11
.PHONY:clean
clean:
	rm -rf server upload
