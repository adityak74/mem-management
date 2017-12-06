# OSS-Shell
----------------------

### To run the project run the below command on you terminal :
```
$ ./oss
```

### Executables :
```
oss and user
```

### Project Structure:

```
oss-shell/
	|README.md
	|src/
		|oss.c
		|user.c
		|shm_header.h
	|Makefile
```

### Compiling:

```
$ make
```
### Running:

```
./master -s<child_process> -l<filename> -t<timeout>
```

### Help:

```
./oss -h
-h: Prints this help message.
-s: Allows you to set the number of child process to run.
	The default value is 5. The max is 20.
-l: filename for the log file.
	The default value is 'default.log' .
-t: Allows you set the wait time for the master process until it kills the slaves and itself.
	The default value is 20.
```

### Clean the project:

```
$ make clean
```

### Clean the project only *.o:

```
$ make cleanobj
```

### Log:

```
default.log - file shows the processID.
Sample message:
Master: Child pid is terminating at my time xx.yy because it reached mm.nn in slave
```

***Memory Management*** : Project #6 as a part of CS4760. 
