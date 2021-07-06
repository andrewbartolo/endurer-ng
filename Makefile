ALL:
	mkdir -p bin
	$(CXX) -o bin/endurer endurer.cpp util.cpp -Ofast -flto -Wno-write-strings

clean:
	rm -rf bin
