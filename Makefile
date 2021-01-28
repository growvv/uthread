gccflags = -g -pthread -I./
src = uthread.c uthread_sched.c 	

test_file: 
	gcc $(gccflags) -o ./test/test_uthread $(src) ./test/test_uthread.c 
	gcc $(gccflags) -o ./test/test_uthread2 $(src) ./test/test_uthread2.c 