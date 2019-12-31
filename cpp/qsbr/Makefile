
default: run

qsbr_runner: qsbr_runner.cpp qsbr.h
	clang++ -std=c++17 -g -O2 -fsanitize=address -o qsbr_runner qsbr_runner.cpp

run: qsbr_runner
	./qsbr_runner

clean:
	rm -f qsbr_runner
	rm -rf qsbr_runner.dSYM
