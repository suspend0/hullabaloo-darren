
default: run

parser_runner: parser_runner.cpp parser.h parser.cpp
	clang++ -std=c++17 -g -O2 -fsanitize=address \
	    -o parser_runner \
			parser.cpp parser_runner.cpp

run: parser_runner
	./parser_runner

clean:
	rm -f parser_runner
	rm -rf parser_runner.dSYM
