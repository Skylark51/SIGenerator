# Native SI Generator

## Build

```powershell
cmake -S native -B native/build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=E:\Qt\6.8.3\msvc2022_64
cmake --build native/build --config Release --parallel 4
powershell -ExecutionPolicy Bypass -File native/deploy.ps1
```

The deployable application is `native/build/Release/SIGenerator.exe`. It has no Python runtime dependency.

## Validation

```powershell
C:\Program Files\CMake\bin\ctest.exe --test-dir native/build -C Release --output-on-failure
native/build/Release/si_benchmark.exe --synthetic-100
```

The regression test covers Gaussian SCF energy, Mulliken spin density, TD-DFT excited states, and IRC direction/point extraction.

## Measured native parser timings

Measurements below are from the Windows Release build on this workstation using buffered single-pass synthetic Gaussian files. They include a cold sequential parse and a cache hit.

| Input | Cold parse | Cache hit |
| --- | ---: | ---: |
| 10 MB | 112.142 ms | 0.0813 ms |
| 50 MB | 546.180 ms | 0.0596 ms |
| 100 MB | 1076.310 ms | 0.0743 ms |

The synthetic files exercise the streaming path; production files additionally parse the final orientation, energies, frequencies, spin-density blocks, TD-DFT states, and IRC points in the same scan.
