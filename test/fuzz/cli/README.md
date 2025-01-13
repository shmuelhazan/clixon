# Clixon fuzzing

This dir contains code for fuzzing clixon cli. 

## Prereqs

Install AFL, see [..](..)

Build and install a clixon system (in particular the backend, the CLI will be replaced)

Build and install CLIgen statically:
```
  ./configure LINKAGE=static INSTALLFLAGS="" CC=/usr/bin/afl-clang-fast
  make
  sudo make install
```

## Build

Build clixon cli statically with the afl-clang compiler:

```
  CC=/usr/bin/afl-clang-fast LINKAGE=static INSTALLFLAGS="" ./configure # Dont care about restconf
  make clean
  cd lib
  make
  sudo make install
  cd ../apps/cli
  make clixon_cli
  sudo make install
```

To link an example plugin properly it gets a little more complex::

- First, you need to identify which example plugins you want to link. Add these to `EXTRAS` variable
- Configure and compile those plugins, where the `clixon_plugin_init()` function is removed.
- Configure and compile the cli WITH the `EXTRAS` variable set.

Below is an example of how to do this for the main example. You can replace the main example plugins with another application:
```
  CC=/usr/bin/afl-clang-fast CFLAGS="-O2 -Wall -DCLIXON_STATIC_PLUGINS" LINKAGE=static INSTALLFLAGS="" ./configure

  make clean
  make
  sudo make install
  
  cd example/main # Compile and install application plugins (here main example)
  make clean
  make
  sudo make install 
  cd ..

  cd apps/cli # Compile and install clixon_cli with pre-compiled plugins
  rm clixon_cli
  EXTRAS="../../example/main/example_cli.o" make clixon_cli
  sudo make install
```

## Run tests

Run the script `runfuzz.sh` to run one test with a cli spec and an input string, eg:
```
  ./runfuzz.sh
```

After (or during) the test, investigate results in the output dir.
