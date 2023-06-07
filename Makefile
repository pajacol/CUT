cut: src/cut.c src/queue.h src/queue.c src/read_file.h src/read_file.c
	$(CC) -Wl,-s,-x,--hash-style=sysv -o bin/cut -O3 -fPIE -pie -std=c11 `if [ $$CC == clang ] ; then echo '-Weverything' ; fi` -Wall -Wextra -pedantic src/cut.c src/queue.c src/read_file.c

read_file_test: tests/read_file_test.c src/read_file.h src/read_file.c
	$(CC) -Wl,-s,-x,--hash-style=sysv -o bin/read_file_test -I src -O3 -fPIE -pie -std=c11 `if [ $$CC == clang ] ; then echo '-Weverything' ; fi` -Wall -Wextra -pedantic tests/read_file_test.c src/read_file.c

queue_test: tests/queue_test.c src/queue.h src/queue.c
	$(CC) -Wl,-s,-x,--hash-style=sysv -o bin/queue_test -I src -O3 -fPIE -pie -std=c11 `if [ $$CC == clang ] ; then echo '-Weverything' ; fi` -Wall -Wextra -pedantic tests/queue_test.c src/queue.c
