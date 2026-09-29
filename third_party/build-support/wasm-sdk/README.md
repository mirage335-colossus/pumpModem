# Explicit browser SDK preparation

This is a separate `wasm32-emscripten` SDK family. It does not reuse a native
FLTK/Rev dependency prefix or the Linux source SDK's target libraries. The
initial compiler-host recipe supports Linux x86_64; the resulting browser assets
are architecture independent. Other compiler hosts need their own pinned input
recipe and qualification before being advertised.

`manifest.json` pins official emsdk 6.0.10 sources, the official compiler build
`666337b525e673e769121856d175f6f52b8ead64`, its Node runtime, and OpenSSL 3.5.7
sources by SHA-256. These inputs were obtained from the URLs in that manifest on
2026-09-28. The Emscripten compiler archive is an upstream binary toolchain, not
a claim that LLVM was rebuilt locally. OpenSSL is cross-compiled locally with
socket support, dynamic engines/modules, threads and assembly disabled. Browser
entropy uses Emscripten's `getrandom` implementation. The application continues
to compile its pinned vendored XZ sources.

Preparation requires host Python 3.12+, Perl, Make, and a Linux x86_64 host able
to run the pinned upstream compiler and Node. Application builds also require
CMake 3.21+ and Ninja or Make. This recipe does not install system packages or
modify the user's shell setup. Host-tool ABI relocation and a reusable release
SDK archive have not yet been qualified by this initial recipe.

```sh
# The only operation permitted to download inputs is explicit fetch --download.
python3 tools/build-wasm-sdk.py fetch --download --sources /path/to/wasm-inputs
# Reuse the exact inputs offline; this refuses an existing SDK destination.
python3 tools/build-wasm-sdk.py prepare --sources /path/to/wasm-inputs \
  --destination /path/to/wasm-sdk --jobs 4
./build.sh --wasm-sdk /path/to/wasm-sdk
./build.sh test web --wasm-sdk /path/to/wasm-sdk
./build.sh package --wasm-sdk /path/to/wasm-sdk
```

The prepared SDK retains all input archives, recipe metadata and OpenSSL
licensing. Compiler cache variants are populated during explicit preparation;
`build.sh` then sets `EM_FROZEN_CACHE=1`. A missing variant fails instead of
silently preparing a dependency during an application build. Keep prepared SDKs
at their original location; use a new destination for a changed recipe. Keep
build directories separate from native builds and from other SDK identities.
The wrapper rejects competing compiler/search-path environment overrides and
the toolchain checks target paths and the OpenSSL archive checksum.

The Wasm product uses a single Worker with cooperative C++ execution and
Asyncify. It does not require pthreads, SharedArrayBuffer or cross-origin
isolation headers. `tools/package-wasm.py` creates `datapump-wasm.html` with
preloaded code, Wasm and audio-worklet bytes, plus a hash inventory and explicit
runtime import list. It rejects socket/thread imports and shared Wasm memory;
this static check complements the browser's restrictive CSP and runtime checks.
The generated page needs a secure browser context for microphone access and
normal browser user consent. It makes no server-side file-access claim.

No base or release asset is published by these commands. Distributing a new SDK
or application requires the repository's normal source, checksum, browser and
release qualification procedures. An existing durable base recipe must never
be silently replaced with these local preparation outputs.
