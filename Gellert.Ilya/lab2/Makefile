# `make` builds host_<type> and client_<type> for every conn_<type>.cpp (see build.sh).
.PHONY: all test clean
all:
	bash ./build.sh
test:
	bash ./run_tests.sh
clean:
	rm -rf build host_* client_*
