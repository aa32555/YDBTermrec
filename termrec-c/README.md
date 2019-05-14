# Termrec (in C)

To build

```
source $(pkg-config --variable=prefix yottadb)/ydb_env_set
mkdir build
cd build
cmake ..
make
```

To run

```
PID_TO_WATCH=5679
strace -p $PID_TO_WATCH -e write,read -xx -s 4096 |& ./termrec
```

Note that I've observed different behavior on different systems, in regards to output of strace, so you may need to adjust flags.
