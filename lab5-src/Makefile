CXX = g++ -fPIC -g
NETLIBS= -lnsl 
CC= gcc -g

all: git-commit myhttpd daytime-server use-dlopen hello.so jj-mod jj-mod.so

daytime-server : daytime-server.o
	$(CXX) -o $@ $@.o $(NETLIBS)

myhttpd : myhttpd.o
	$(CXX) -o $@ $@.o $(NETLIBS) -lpthread -ldl

use-dlopen: use-dlopen.o
	$(CXX) -o $@ $@.o $(NETLIBS) -ldl

hello.so: hello.o
	ld -G -o hello.so hello.o

jj-mod: jj-mod.c
	$(CC) -c -fPIC -o $@.o jj-mod.c

util: util.c
	$(CC) -c -fPIC -o $@.o util.c

jj-mod.so: jj-mod.o util.o
	ld -G -o jj-mod.so jj-mod.o util.o -g
	cp jj-mod.so http-root-dir/cgi-bin/jj-mod.so
	
%.o: %.cc
	@echo 'Building $@ from $<'
	$(CXX) -o $@ -c -I. $<

.PHONY: git-commit
git-commit:
	git checkout
	git add *.cc *.h Makefile >> .local.git.out  || echo
	git commit -a -m 'Commit' >> .local.git.out || echo
	git push origin master 

.PHONY: clean
clean:
	rm -f *.o use-dlopen hello.so
	rm -f *.o daytime-server myhttpd
	rm -f http-root-dir/cgi-bin/jj-mod.so

