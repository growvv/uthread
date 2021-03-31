gccflags = -g -pthread -I . -ldl
src = uthread.c uthread_sched.c uthread_socket.c timer.c	

all: test_file

test_file: 
	gcc $(gccflags) -o libmyhook.so -fPIC -shared -D_GNU_SOURCE $(src) myhook.c -ldl 
	sudo cp libmyhook.so /usr/local/lib/
	gcc $(gccflags) -o main main.c -L./ -lmyhook
	gcc $(gccflags) -o ./test/test_uthread ./test/test_uthread.c -L./ -lmyhook
	gcc $(gccflags) -o ./test/test_join_exit ./test/test_join_exit.c -L./ -lmyhook
	gcc $(gccflags) -o ./test/test_socket_io ./test/test_socket_io.c -L./ -lmyhook
	gcc $(gccflags) -o ./test/test_disk_io ./test/test_disk_io.c -L./ -lmyhook
	gcc $(gccflags) -o ./test/test_timer ./test/test_timer.c -L./ -lmyhook
