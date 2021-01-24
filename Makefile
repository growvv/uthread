test_uthread: *.c
	gcc -g -o $@ $^ -pthread