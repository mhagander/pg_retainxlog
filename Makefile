PG_CONFIG=pg_config

CFLAGS=-I$(shell $(PG_CONFIG) --includedir) -Wall
LDFLAGS=-L$(shell $(PG_CONFIG) --libdir) -lpq

all: pg_retainxlog

pg_retainxlog: pg_retainxlog.o
	$(CC) -o pg_retainxlog pg_retainxlog.o $(LDFLAGS)

clean:
	rm -f pg_retainxlog pg_retainxlog.o
