OPENSSL_PREFIX := $(shell brew --prefix openssl@3)

# OpenSSL (Homebrew) include/library paths for macOS
OPENSSL_PREFIX = $(shell brew --prefix openssl@3)
OPENSSL_INC = -I$(OPENSSL_PREFIX)/include
OPENSSL_LIB = -L$(OPENSSL_PREFIX)/lib

all: dfc dfs

dfc: dfc.c
	gcc -Wall -Wextra $(OPENSSL_INC) -o dfc dfc.c $(OPENSSL_LIB) -lssl -lcrypto

dfs: dfs.c
	gcc -Wall -Wextra $(OPENSSL_INC) -o dfs dfs.c $(OPENSSL_LIB) -lssl -lcrypto

run_dfs: all
	./dfs ./dfs1 10001 & \
	./dfs ./dfs2 10002 & \
	./dfs ./dfs3 10003 & \
	./dfs ./dfs4 10004 & \
	wait

directories:
	mkdir -p dfs1 dfs2 dfs3 dfs4

clean:
	rm -f dfc dfs *.o
	rm -rf dfs1 dfs2 dfs3 dfs4
	killall dfs