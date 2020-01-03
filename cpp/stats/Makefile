
default: run

stats_runner: stats_runner.cpp stats.h stats.cpp
	clang++ -std=c++17 -g -O2 -fsanitize=address \
	    -o stats_runner \
			stats.cpp stats_runner.cpp

run: stats_runner
	./stats_runner

clean:
	rm -f stats_runner
	rm -rf stats_runner.dSYM
