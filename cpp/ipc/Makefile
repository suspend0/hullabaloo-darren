
default: run

ipc_runner: ipc_runner.cpp api.h marshalling.h small_vector.h
	clang++ -std=c++17 -g -O2 -fsanitize=address \
	    -o ipc_runner ipc_runner.cpp

run: ipc_runner
	./ipc_runner

clean:
	rm -f ipc_runner
	rm -rf ipc_runner.dSYM
