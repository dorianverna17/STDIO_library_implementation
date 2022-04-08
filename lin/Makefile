build:
	gcc -fPIC -c so_stdio.c so_stdio.h
	gcc -shared so_stdio.o -o libso_stdio.so

clean:
	rm so_stdio.h.gch
	rm so_stdio.o
	rm libso_stdio.so