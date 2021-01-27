src = uthread.c uthread_sched.c 

test_uthread: $(src) test_uthread.c
	gcc -g -o test_uthread $(src) test_uthread.c -pthread
test_uthread2 : $(src) test_uthread2.c
	gcc -g -o test_uthread2 $(src) test_uthread2.c -pthread