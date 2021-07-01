ALL:
	mkdir -p bin
	$(CXX) -o bin/endurer endurer.cpp util.cpp -Ofast -Wno-write-strings

clean:
	rm -rf bin
