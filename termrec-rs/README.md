# Termrec (in Rust)

To build

```
source $(pkg-config --variable=prefix yottadb)/ydb_env_set
cargo build
```

To run

```
PID_TO_WATCH=30149
sudo strace -f -x -s 8192 -e abbrev=none -e trace=write -e write=1,2 -v -p $PID_TO_WATCH |& cargo run -- record
cargo run -- list
cargo run -- play 1
cargo run -- --help
```
