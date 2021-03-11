gccflags = -g -pthread -I .
src = uthread.c uthread_sched.c 	

all: test_file

test_file: 
	gcc $(gccflags) -o ./test/test_uthread $(src) ./test/test_uthread.c 
	gcc $(gccflags) -o ./test/test_disk_io $(src) ./test/test_disk_io.c 
	gcc $(gccflags) -o ./test/test_join_exit $(src) ./test/test_join_exit.c
